# Grumpy Screen for Klipper

Grumpyscreen is a fork of GuppyScreen specifically for Simple AF and OpenCentauri Cosmos

## Features

:white_check_mark: Setup Wifi  
:white_check_mark: Print Status  
:white_check_mark: Extrude/Retract  
:white_check_mark: Temperature Control  
:white_check_mark: Fans/LED/Move Control  
:white_check_mark: Print Fine Tune (Speed, Flow, Baby Stepping, Pressure Advance)  
:white_check_mark: Start a Print from Usb or Reprint  
:white_check_mark: Exclude Object  
:white_check_mark: Console Logging  
:white_check_mark: Spoolman Integration  
:white_check_mark: MMU support (AFC, Happy Hare)

## Code quality

Use braces for every `if`, `else`, `for`, `while` and `do` body, including
single statements. Ordinary `else if` chains are allowed. The `Braces` PR
check covers C/C++ sources and headers in `src/`, Java sources under
`android/app/src/`, and Java tests in `android/tests/`.

Run the same syntax check locally with Python 3.10 or newer:

```sh
python3 -m venv /tmp/grumpyscreen-braces
/tmp/grumpyscreen-braces/bin/pip install -r tools/requirements-braces.txt
/tmp/grumpyscreen-braces/bin/python tools/check_braces.py
/tmp/grumpyscreen-braces/bin/python -m unittest discover -s tools -p 'test_check_braces.py'
```

The check needs no submodules, Android SDK or firmware toolchain. Configure
`Braces` as a required status check in the repository's branch protection
or ruleset to prevent merging a failing PR.

## Credits

- [Guppyscreen](https://github.com/ballaswag/guppyscreen)
- [OpenKE](https://github.com/coreflake1/guppyscreen)
- [Material Design Icons](https://pictogrammers.com/library/mdi/)
- [Material Design WebFont](https://github.com/Templarian/MaterialDesign-Webfont)
- [Moonraker](https://github.com/Arksine/moonraker)  
- [KlipperScreen](https://github.com/KlipperScreen/KlipperScreen)  
- [Fluidd](https://github.com/fluidd-core/fluidd)  
