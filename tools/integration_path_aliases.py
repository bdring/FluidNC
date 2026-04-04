Import("env")

from pathlib import Path
import os
import shutil
import subprocess


def _remove_path(path: Path) -> None:
    if not path.exists() and not path.is_symlink():
        return
    if path.is_dir() and not path.is_symlink():
        shutil.rmtree(path)
    else:
        path.unlink()


def _create_directory_link(link: Path, target: Path) -> None:
    if link.exists() or link.is_symlink():
        try:
            if link.resolve() == target.resolve():
                return
        except OSError:
            pass
        _remove_path(link)

    link.parent.mkdir(parents=True, exist_ok=True)

    if os.name == "nt":
        subprocess.check_call(["cmd", "/c", "mklink", "/J", str(link), str(target)])
    else:
        os.symlink(target, link, target_is_directory=True)


project_dir = Path(env.subst("$PROJECT_DIR"))
build_dir = Path(env.subst("$BUILD_DIR"))
tools_dir = project_dir / "tools"

source_root = project_dir / "FluidNC"

def _mirror_source_tree(source_tree: Path, build_base: Path) -> None:
    if not source_tree.exists():
        return

    _create_directory_link(build_base / "FluidNC", source_root)
    for directory in source_tree.rglob("*"):
        if directory.is_dir():
            relative = directory.relative_to(source_tree)
            _create_directory_link(build_base / relative / "FluidNC", source_root)


_mirror_source_tree(source_root / "capture", build_dir / "capture")
_mirror_source_tree(source_root / "src", build_dir / "src")
_mirror_source_tree(source_root / "esp32", build_dir)
_mirror_source_tree(source_root / "esp32", build_dir / "esp32")
_mirror_source_tree(source_root / "tests", build_dir / "test")

env["ENV"]["FLUIDNC_PROJECT_ROOT"] = str(project_dir)
env["ENV"]["FLUIDNC_SOURCE_ROOT"] = str(source_root)
env["ENV"]["FLUIDNC_PYTHON"] = env.subst("$PYTHONEXE")
env["ENV"]["FLUIDNC_REAL_GPP"] = env.WhereIs("g++") or "g++"
env["ENV"]["FLUIDNC_REAL_GCC"] = env.WhereIs("gcc") or "gcc"
env["ENV"]["FLUIDNC_REAL_AR"] = env.WhereIs("ar") or "ar"
env["ENV"]["FLUIDNC_REAL_RANLIB"] = env.WhereIs("ranlib") or "ranlib"
env.PrependENVPath("PATH", str(tools_dir))
