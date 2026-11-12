# Repository instructions

GrumpyScreen shares its C++ UI in `src/` between printer firmware and Android.
Android Java code lives in `android/app/src/`; see `android/README.md` for builds.

## Coding style

- Use braces for every C/C++ and Java `if`, `else`, `for`, `while` and `do`
  body, even a single statement. Every `else if` branch body needs braces too.
- Conventional `} else if (condition) { ... }` chains are allowed. This does
  not permit `else if (condition) statement;`.
- Match nearby indentation and keep unrelated formatting out of a change.

## Quality checks

Read `.github/workflows/quality.yml` when changing source or quality tooling.
Run the braces check before finishing changes to `src/` or Android Java.
From the repository root, set up a virtual environment (or reuse an existing
one with `tools/requirements-braces.txt` installed):

```sh
python3 -m venv /tmp/grumpyscreen-braces
/tmp/grumpyscreen-braces/bin/pip install -r tools/requirements-braces.txt
/tmp/grumpyscreen-braces/bin/python tools/check_braces.py
```

When changing the checker, its dependencies or its workflow, also run:

```sh
/tmp/grumpyscreen-braces/bin/python -m unittest discover -s tools -p 'test_check_braces.py'
```

Run relevant existing tests for behavior changes. The native C++ suite is
`make GUPPY_SDL=true BUILD_DIR=build/sdl test`; Android configuration tests are
`bash android/tools/test-printer-config.sh`. Report which checks ran and any
checks that could not run, with the reason.
