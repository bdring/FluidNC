#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
exec "$project_dir/.venv-gamepad/bin/python" "$project_dir/tools/gamepad_motor_controller_xy.py" "$@"
