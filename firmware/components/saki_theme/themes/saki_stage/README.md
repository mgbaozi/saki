# Saki Stage theme source

`source/status-sheet.png` is an AI-assisted, unofficial fan-art sprite sheet created for this
project with OpenAI's built-in image generation tool. It uses a unified chibi treatment for
character likenesses inspired by *BanG Dream! It's MyGO!!!!!* and *Ave Mujica*. No official image,
logo, or extracted game/anime asset is embedded, but the depicted character likenesses remain the
property of their respective rights holders and are not licensed under Apache-2.0. See the root
`NOTICE` before redistributing this pack.

Source SHA-256:
`8b41866e0c2ebc5b000f1fd89eb89a413fe80ff4564b600aa3206b85d772af8c`.

`source/status-sheet.png` is the canonical high-resolution master: 1536×1024 RGBA, 2,533,505
bytes. Keep it unchanged and versioned with the pack. The 48×48 RGB565A8 arrays under
`generated/` are display-specific derivatives only; future screen sizes and pack exports must be
regenerated from this PNG, never enlarged from generated C data, screenshots, or device captures.

Regenerate the bounded 48×48 RGB565A8 LVGL resources with Pillow 12.3.0. The sheet keeps fully
transparent gutters between its three columns and rows; outer margins and cell widths may differ.
The converter detects those gutters from alpha instead of geometrically dividing the canvas, then
discards source pixels below alpha 8 before finding content bounds so invisible generation residue
cannot shrink the character on the device:

```sh
python3 scripts/generate_theme_assets.py \
  --pack saki_stage \
  --input firmware/components/saki_theme/themes/saki_stage/source/status-sheet.png \
  --output-c firmware/components/saki_theme/themes/saki_stage/generated/saki_stage_assets.c \
  --output-h firmware/components/saki_theme/themes/saki_stage/generated/saki_stage_assets.h
```

The generated C and header are build inputs and must not be edited manually. Runtime code selects
assets through the semantic theme-pack registry; it must not reference Agent product names.
