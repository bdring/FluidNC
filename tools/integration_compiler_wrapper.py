import os
import subprocess
import sys
from pathlib import Path


def ensure_fluidnc_link() -> None:
    source_root = Path(os.environ["FLUIDNC_SOURCE_ROOT"])
    cwd = Path.cwd()
    link = cwd / "FluidNC"

    if link.exists() or link.is_symlink():
        return

    if os.name == "nt":
        subprocess.check_call(["cmd", "/c", "mklink", "/J", str(link), str(source_root)])
    else:
        os.symlink(source_root, link, target_is_directory=True)


def _maybe_abs_path(value: str, project_root: Path) -> str:
    path = Path(value)
    if path.is_absolute():
        return value

    candidate = project_root / path
    if candidate.exists():
        return str(candidate)
    return value


def _abs_output_path(value: str, project_root: Path) -> str:
    path = Path(value)
    if path.is_absolute():
        return value
    return str(project_root / path)


def _rewrite_args(args: list[str], project_root: Path, tool: str) -> list[str]:
    rewritten: list[str] = []
    i = 0
    path_flags = {"-o", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote"}
    seen_archive = False
    while i < len(args):
        arg = args[i]
        if arg in path_flags and i + 1 < len(args):
            rewritten.append(arg)
            rewritten.append(_abs_output_path(args[i + 1], project_root) if arg in {"-o", "-MF", "-MT", "-MQ"} else _maybe_abs_path(args[i + 1], project_root))
            i += 2
            continue

        if arg.startswith("-I") and len(arg) > 2:
            rewritten.append("-I" + _maybe_abs_path(arg[2:], project_root))
        elif arg.startswith("-isystem") and len(arg) > 8:
            rewritten.append("-isystem" + _maybe_abs_path(arg[8:], project_root))
        elif arg.startswith("-iquote") and len(arg) > 7:
            rewritten.append("-iquote" + _maybe_abs_path(arg[7:], project_root))
        elif arg.startswith("-o") and len(arg) > 2:
            rewritten.append("-o" + _abs_output_path(arg[2:], project_root))
        elif arg.startswith("-MF") and len(arg) > 3:
            rewritten.append("-MF" + _abs_output_path(arg[3:], project_root))
        elif arg.startswith("-MT") and len(arg) > 3:
            rewritten.append("-MT" + _abs_output_path(arg[3:], project_root))
        elif arg.startswith("-MQ") and len(arg) > 3:
            rewritten.append("-MQ" + _abs_output_path(arg[3:], project_root))
        elif not arg.startswith("-"):
            if tool in {"ar", "ranlib"} and not seen_archive:
                if arg.isalpha() and "\\" not in arg and "/" not in arg and ":" not in arg:
                    rewritten.append(arg)
                else:
                    rewritten.append(_abs_output_path(arg, project_root))
                    seen_archive = True
            else:
                rewritten.append(_maybe_abs_path(arg, project_root))
        else:
            rewritten.append(arg)
        i += 1
    return rewritten


def main() -> int:
    project_root = Path(os.environ["FLUIDNC_PROJECT_ROOT"])
    ensure_fluidnc_link()

    tool = "g++"
    if len(sys.argv) > 1:
        tool = sys.argv[1]

    compiler = os.environ["FLUIDNC_REAL_GPP"]
    if tool == "gcc":
        compiler = os.environ["FLUIDNC_REAL_GCC"]
    elif tool == "ar":
        compiler = os.environ["FLUIDNC_REAL_AR"]
    elif tool == "ranlib":
        compiler = os.environ["FLUIDNC_REAL_RANLIB"]

    args = [compiler, *_rewrite_args(sys.argv[2:], project_root, tool)]
    return subprocess.call(args)


if __name__ == "__main__":
    raise SystemExit(main())
