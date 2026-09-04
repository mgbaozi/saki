# Vendor BSP origin

- Hardware: 正点原子 ATK-DNESP32S3B3 / ESP32S3 BOX3
- Source example: `v_5_5_x_examples/extend-idf/LVGL/01_lvgl_transplant` from the
  manufacturer examples bundle; its local root is configured outside this repository.
- Imported: 2026-09-02
- ESP-IDF baseline: 5.5.3
- LVGL baseline: 8.4.0

Imported modules:

- `MYIIC`: shared board I²C initialization
- `MYSPI`: LCD SPI initialization
- `AW9523B`: IO expander and LCD backlight control
- `LCD`: ST7789 LCD driver
- `TOUCH`: CHSC5432/CHSC5xxx touch driver

The original copyright headers are retained. Saki moves the component under the descriptive `vendor_box3_bsp` directory and defines `SPI_LCD_TYPE=1` in the component build so this board uses its 2.4-inch 320×240 panel instead of the vendor source's undefined-macro 240×240 fallback. The original vendor example remains unchanged.
