Import("env")

from pathlib import Path


def _read(path: Path) -> str:
	return path.read_text(encoding="utf-8")


def _write(path: Path, content: str) -> None:
	path.write_text(content, encoding="utf-8")


def _replace_once(content: str, old: str, new: str, label: str) -> str:
	if new in content:
		return content
	if old not in content:
		print(f"[patch_arduinopico] Skip {label}: pattern not found")
		return content
	return content.replace(old, new, 1)


def _patch_file(path: Path, patch_fn, label: str) -> None:
	if not path.exists():
		print(f"[patch_arduinopico] Missing {label}: {path}")
		return
	before = _read(path)
	after = patch_fn(before)
	if after != before:
		_write(path, after)
		print(f"[patch_arduinopico] Patched {label}")


def _patch_freertos_lwip_h(content: str) -> str:
	old = """// Return true if __real_LWIP ops are safe (i.e. this is the LWIP thread)\nextern \"C\" bool __isLWIPThread();\n"""
	new = """// Return true if __real_LWIP ops are safe (i.e. this is the LWIP thread)\nextern \"C\" bool __isLWIPThread();\n\n// Return true once LWIP worker queue is initialized and ready for ISR callbacks.\nextern \"C\" bool __lwipReady();\n"""
	return _replace_once(content, old, new, "freertos-lwip.h declaration")


def _patch_freertos_lwip_cpp(content: str) -> str:
	old1 = """static void lwipThread(void *params);\nstatic TaskHandle_t __lwipTask;\nstatic QueueHandle_t __lwipQueue;\n"""
	new1 = """static void lwipThread(void *params);\nstatic TaskHandle_t __lwipTask;\nstatic QueueHandle_t __lwipQueue;\n\nextern \"C\" bool __lwipReady() {\n    return __lwipQueue != nullptr;\n}\n"""
	content = _replace_once(content, old1, new1, "freertos-lwip.cpp readiness function")

	old2 = """extern \"C\" void __lwip(__lwip_op op, void *req, bool fromISR) {\n    LWIPWork w;\n"""
	new2 = """extern \"C\" void __lwip(__lwip_op op, void *req, bool fromISR) {\n    if (!__lwipQueue) {\n        if (fromISR) {\n            return;\n        }\n        __startLWIPThread();\n        if (!__lwipQueue) {\n            panic(\"LWIP queue unavailable\");\n        }\n    }\n\n    LWIPWork w;\n"""
	return _replace_once(content, old2, new2, "freertos-lwip.cpp queue guard")


def _patch_cyw43_driver(content: str) -> str:
	content = _replace_once(
		content,
		"#include <lwip_wrap.h>\n",
		"#include <lwip_wrap.h>\n#include \"../freertos/freertos-lwip.h\"\n",
		"cyw43 include",
	)

	old_irq = """    uint32_t events = gpio_get_irq_event_mask(CYW43_PIN_WL_HOST_WAKE);\n    if (events & GPIO_IRQ_LEVEL_HIGH) {\n        // As we use a high level interrupt, it will go off forever until it's serviced\n"""
	new_irq = """    uint32_t events = gpio_get_irq_event_mask(CYW43_PIN_WL_HOST_WAKE);\n    if (events & GPIO_IRQ_LEVEL_HIGH) {\n        if (!__lwipReady()) {\n            return;\n        }\n        // As we use a high level interrupt, it will go off forever until it's serviced\n"""
	content = _replace_once(content, old_irq, new_irq, "cyw43 irq guard")

	old_irq_core = """static void cyw43_gpio_irq_handler() {\n#ifndef NDEBUG\n    assert(get_core_num() == 0);\n#endif\n    uint32_t events = gpio_get_irq_event_mask(CYW43_PIN_WL_HOST_WAKE);\n"""
	new_irq_core = """static void cyw43_gpio_irq_handler() {\n#ifndef NDEBUG\n    assert(get_core_num() == 0);\n#endif\n    if (get_core_num() != 0) {\n        return;\n    }\n    uint32_t events = gpio_get_irq_event_mask(CYW43_PIN_WL_HOST_WAKE);\n"""
	content = _replace_once(content, old_irq_core, new_irq_core, "cyw43 irq core guard")

	old_init_core = """static uint32_t cyw43_irq_init(__unused void *param) {\n#ifndef NDEBUG\n    assert(get_core_num() == 0);\n#endif\n    gpio_add_raw_irq_handler_with_order_priority(CYW43_PIN_WL_HOST_WAKE, cyw43_gpio_irq_handler, CYW43_GPIO_IRQ_HANDLER_PRIORITY);\n"""
	new_init_core = """static uint32_t cyw43_irq_init(__unused void *param) {\n#ifndef NDEBUG\n    assert(get_core_num() == 0);\n#endif\n    if (get_core_num() != 0) {\n        panic("cyw43_irq_init must run on core 0");\n    }\n    gpio_add_raw_irq_handler_with_order_priority(CYW43_PIN_WL_HOST_WAKE, cyw43_gpio_irq_handler, CYW43_GPIO_IRQ_HANDLER_PRIORITY);\n"""
	content = _replace_once(content, old_init_core, new_init_core, "cyw43 init core guard")

	old_drv_init_core = """extern "C" bool __wrap_cyw43_driver_init(async_context_t *context) {\n    assert(get_core_num() == 0);\n    _cyw43_arch_mutex = xSemaphoreCreateRecursiveMutex();\n"""
	new_drv_init_core = """extern "C" bool __wrap_cyw43_driver_init(async_context_t *context) {\n    assert(get_core_num() == 0);\n    if (get_core_num() != 0) {\n        panic("cyw43_driver_init must run on core 0");\n    }\n    _cyw43_arch_mutex = xSemaphoreCreateRecursiveMutex();\n"""
	content = _replace_once(content, old_drv_init_core, new_drv_init_core, "cyw43 driver init core guard")

	old_dispatch = """extern \"C\" void __wrap_cyw43_schedule_internal_poll_dispatch(__unused void (*func)()) {\n    lwip_callback(cb_cyw43_do_poll, nullptr);\n}\n"""
	new_dispatch = """extern \"C\" void __wrap_cyw43_schedule_internal_poll_dispatch(__unused void (*func)()) {\n    if (!__lwipReady()) {\n        return;\n    }\n    lwip_callback(cb_cyw43_do_poll, nullptr);\n}\n"""
	content = _replace_once(content, old_dispatch, new_dispatch, "cyw43 dispatch guard")

	old_sleep = """    if (xSemaphoreTakeFromISR(_cyw43_sleep_poll_binary, &pxHigherPriorityTaskWoken)) {\n        lwip_callback(cb_cyw43_do_poll, nullptr, &_sleepIRQBuffer);\n    }\n"""
	new_sleep = """    if (xSemaphoreTakeFromISR(_cyw43_sleep_poll_binary, &pxHigherPriorityTaskWoken)) {\n        if (__lwipReady()) {\n            lwip_callback(cb_cyw43_do_poll, nullptr, &_sleepIRQBuffer);\n        }\n    }\n"""
	return _replace_once(content, old_sleep, new_sleep, "cyw43 sleep guard")


def _patch_freertos_main_cpp(content: str) -> str:
	content = _replace_once(
		content,
		"#include <USB.h>\n",
		"#include <USB.h>\n#include \"hardware/irq.h\"\n",
		"freertos-main include irq",
	)

	old_core1 = """static void __core1(void *params) {\n    (void) params;\n#if !defined(NO_USB) && !defined(USE_TINYUSB)\n"""
	new_core1 = """static void __core1(void *params) {\n    (void) params;\n    irq_set_enabled(IO_IRQ_BANK0, false);\n#if !defined(NO_USB) && !defined(USE_TINYUSB)\n"""
	return _replace_once(content, old_core1, new_core1, "freertos-main core1 irq disable")


framework_dir = env.PioPlatform().get_package_dir("framework-arduinopico")
if not framework_dir:
	print("[patch_arduinopico] framework-arduinopico package not found")
else:
	root = Path(framework_dir)
	_patch_file(
		root / "cores" / "rp2040" / "freertos" / "freertos-lwip.h",
		_patch_freertos_lwip_h,
		"freertos-lwip.h",
	)
	_patch_file(
		root / "cores" / "rp2040" / "freertos" / "freertos-lwip.cpp",
		_patch_freertos_lwip_cpp,
		"freertos-lwip.cpp",
	)
	_patch_file(
		root / "cores" / "rp2040" / "sdkoverride" / "cyw43_driver_freertos.cpp",
		_patch_cyw43_driver,
		"cyw43_driver_freertos.cpp",
	)
	_patch_file(
		root / "cores" / "rp2040" / "freertos" / "freertos-main.cpp",
		_patch_freertos_main_cpp,
		"freertos-main.cpp",
	)
