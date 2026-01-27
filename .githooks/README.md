# Local Git hooks (optional)

This repo includes optional local Git hooks under `.githooks/`.

They are **not installed automatically** by Git. To enable them:

```bash
git config core.hooksPath .githooks
```

## Included hooks

- `pre-commit`: Runs PlatformIO unit tests (`pio test -e tests_nosan`) when you commit.
  - **Non-blocking**: commits are allowed even if tests fail.
  - Set `FLUIDNC_SKIP_TESTS=1` to skip locally.

To disable hooks:

```bash
git config --unset core.hooksPath
```
