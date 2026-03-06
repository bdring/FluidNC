# Local Git hooks (optional)

This repo includes optional local Git hooks under `.githooks/`.

They are **not installed automatically** by Git. To enable them:

```bash
git config core.hooksPath .githooks
```

## Included hooks

- `pre-commit`: Runs PlatformIO unit tests (`pio test -e tests_nosan`) when you commit.
  - **Blocking**: commits are rejected if tests fail.
  - Bypass locally with `FLUIDNC_SKIP_TESTS=1 git commit` or `git commit --no-verify`.

To disable hooks:

```bash
git config --unset core.hooksPath
```
