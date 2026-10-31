# Languages

The language is picked under Settings > Language, and saved as `language` in the `[ui]` config section. It applies right away, with no restart.

Each language is a JSON file in `languages/`, next to the `grumpyscreen` binary:

```json
{
  "_language": "Italiano",
  "Back": "Indietro",
  "Page {} / {}": "Pagina {} / {}"
}
```

- `_language` is the name shown in the dropdown.
- Each key is the English text from the code. A key with no entry shows in English.
- `{}` placeholders must be kept, in the same order.

Only the active language is held in memory. A switch reads the new file, retranslates the screen, then frees the old table.

## Adding a language

1. Copy `languages/it.json` to `languages/<code>.json`, and translate the values.
2. Run `python3 tools/check_languages.py`, which also runs in `make test`. It checks that keys and placeholders match, and that no two strings share a translation.

Letters from U+00C0 to U+00FF (Western European) are built in. For other alphabets, add their fonts in `src/theme.cpp`; `tools/gen_accent_fonts.py` generates the current ones.

## Adding text to the code

Text passed to these LVGL calls is translated automatically by `src/i18n_hooks.h`: `lv_label_set_text`, `lv_label_set_text_fmt`, `lv_tabview_add_tab`, `lv_table_set_cell_value`, `lv_textarea_set_placeholder_text`, `lv_msgbox_create`, `lv_btnmatrix_set_map`.

- Add the new English string as a key in `languages/it.json`. `check_languages.py` regenerates `en.json` from it.
- Text built with `fmt::format` needs `I18n::tr` on the format string, as the finished text won't match a key.
- Don't keep the pointer `I18n::tr` returns, since a switch frees it.
- For text that must not be translated, such as a hidden label holding a command, call the function in parentheses: `(lv_label_set_text)(obj, text)`.

## Limits

- Text built with `fmt::format` that's already on screen, like a page count, updates the next time its panel refreshes it.
- Printer data, Klipper messages and macro names aren't in the files. Anything that exactly matches a key is translated, so a macro named `Stop` would show as `Ferma`.
