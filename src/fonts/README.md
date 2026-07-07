# Bundled fonts

The UI fonts are 1-bit bitmap subsets generated from open-source TTFs with
`lv_font_conv` (see the `Opts:` header comment in each `.c`). Both source fonts
are licensed under the **SIL Open Font License 1.1** — full texts in
[`licenses/`](licenses/).

| Files | Font | Copyright | License | Source |
|-------|------|-----------|---------|--------|
| `b612_48.c`, `b612_28.c`, `b612_16.c`, `b612_14.c` | B612 | © 2012 The B612 Project Authors | OFL-1.1 ([OFL-B612.txt](licenses/OFL-B612.txt)) | github.com/polarsys/b612 |
| `dseg7_48.c` | DSEG7 Classic | © 2017 keshikan | OFL-1.1, RFN "DSEG" ([OFL-DSEG.txt](licenses/OFL-DSEG.txt)) | github.com/keshikan/DSEG |

## OFL compliance notes

- The OFL text and copyright notices are retained in `licenses/`. This satisfies
  the OFL requirement to distribute the licence with the font (including with
  derived/embedded copies such as these bitmap subsets).
- **Reserved Font Name (DSEG):** the OFL forbids a *Modified Version* from being
  presented under the reserved name "DSEG". These are unnamed bitmap glyph arrays
  embedded in firmware — no font name is shown to any user — so the reserved name
  is not used as a font name. The C identifier `dseg7_48` is just a symbol name.
  B612 has no reserved font name.
- OFL does **not** require attribution in the product UI, only that the licence
  accompanies the font files (done here).

## Regenerating

See the memory note `ui-fonts` / the `Opts:` line in each file. In short:

```
lv_font_conv --font <TTF> --size <px> --bpp 1 --format lvgl \
    --range 0x20-0x7F --range 0xB0 --lv-include lvgl.h -o <out>.c
```

DSEG7 is digits-only: `--symbols "0123456789.-"`.
