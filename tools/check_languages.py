#!/usr/bin/env python3
"""Regenerates languages/en.json from it.json and checks every language file:
same keys as English, same {} placeholders, and no two English strings sharing
a translation, which live switching could not tell apart."""
import json
import pathlib
import re
import sys

LANGUAGES = pathlib.Path(__file__).resolve().parent.parent / "languages"
NAME_KEY = "_language"


def placeholders(text):
    return re.findall(r"\{[^}]*\}", text)


def main():
    source = json.loads((LANGUAGES / "it.json").read_text(encoding="utf-8"))
    english = {NAME_KEY: "English", **{k: k for k in source if k != NAME_KEY}}
    (LANGUAGES / "en.json").write_text(
        json.dumps(english, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")

    problems = []
    for path in sorted(LANGUAGES.glob("*.json")):
        table = json.loads(path.read_text(encoding="utf-8"))
        if NAME_KEY not in table:
            problems.append(f"{path.name}: missing {NAME_KEY}")
        table.pop(NAME_KEY, None)
        for key in english.keys() - table.keys() - {NAME_KEY}:
            problems.append(f"{path.name}: missing {key!r}")
        for key in table.keys() - english.keys():
            problems.append(f"{path.name}: unknown {key!r}")
        seen = {}
        for key, value in table.items():
            if placeholders(key) != placeholders(value):
                problems.append(f"{path.name}: placeholders differ in {key!r}")
            if value in seen:
                problems.append(f"{path.name}: {key!r} and {seen[value]!r} share {value!r}")
            seen[value] = key

    print("\n".join(problems) or f"{len(english) - 1} strings OK")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
