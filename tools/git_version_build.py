Import("env")

import subprocess
from pathlib import Path


def main() -> None:
    project_dir = Path(env.subst("$PROJECT_DIR"))
    git_version = project_dir / "git-version.py"
    subprocess.check_call([env.subst("$PYTHONEXE"), str(git_version)])


main()
