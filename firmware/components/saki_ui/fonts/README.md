# Saki CJK font

`saki_font_cjk_16.c` is a 16 px, 2 bpp LVGL fallback font containing all
7,445 symbols in Python's GB2312 mapping (including 6,763 Han characters).
Latin text continues to use Montserrat.

Source font: `NotoSansCJKsc-Regular.otf` from the official
[`notofonts/noto-cjk`](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf)
repository, licensed under the SIL Open Font License 1.1. Its expected SHA-256
is `2c76254f6fc379fddfce0a7e84fb5385bb135d3e399294f6eeb6680d0365b74b`.
The full license is retained in `licenses/NotoSansCJK-OFL-1.1.txt`.

Regenerate from the project root with:

```sh
python3 scripts/generate_cjk_font.py \
  --font /path/to/NotoSansCJKsc-Regular.otf \
  --output firmware/components/saki_ui/fonts/saki_font_cjk_16.c
```

The script pins `lv_font_conv` 1.5.3 and verifies the input font hash.
