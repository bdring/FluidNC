from pathlib import Path


ROOT = Path(__file__).parents[2]
CHANNEL_HEADER = ROOT / "FluidNC" / "src" / "Channel.h"
CHANNEL_SOURCE = ROOT / "FluidNC" / "src" / "Channel.cpp"
SYS_STATS = ROOT / "FluidNC" / "esp32" / "SysStats.cpp"


def test_dynamic_channel_queue_mutex_is_destroyed_and_counted():
    header = CHANNEL_HEADER.read_text(encoding="utf-8")
    source = CHANNEL_SOURCE.read_text(encoding="utf-8")
    stats = SYS_STATS.read_text(encoding="utf-8")

    assert "virtual ~Channel();" in header
    assert "SemaphoreHandle_t _queue_mutex = nullptr;" in header
    assert "xSemaphoreCreateMutex()" not in header
    assert "create_channel_queue_mutex_or_throw" in source
    numbered_constructor = source[
        source.index("Channel::Channel(const char* name, objnum_t num") : source.index("Channel::~Channel()")
    ]
    assert numbered_constructor.index("_name += std::to_string(num);") < numbered_constructor.index(
        "create_channel_queue_mutex_or_throw()"
    )
    destructor = source[source.index("Channel::~Channel()") : source.index("void Channel::pause()")]
    assert "vSemaphoreDelete(_queue_mutex);" in destructor
    assert "_queue_mutex = nullptr;" in destructor
    assert "channel_queue_mutex_created" in source
    assert "channel_queue_mutex_destroyed" in source
    assert '"Channel queue mutexes created"' in stats
    assert '"Channel queue mutexes destroyed"' in stats
