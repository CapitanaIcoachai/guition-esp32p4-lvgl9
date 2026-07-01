# guition-esp32p4-lvgl9

A **working Arduino baseline** for the **Guition JC1060P470C** 7" board
(1024x600 IPS, **ESP32-P4**) running **LVGL 9**, plus the three build fixes that
otherwise cost hours.

- Display: **JD9165** controller over **MIPI-DSI**
- Touch: **GT911** over I2C
- **PSRAM** for the LVGL draw buffers

The sketch is a minimal "hello LVGL 9": it initializes the panel, the touch
controller and LVGL with a double PSRAM draw buffer, then shows a label and a
button (tap to increment a counter). It is a clean, standalone starting point
— add your own UI on top.

---

## Repository layout

```
guition_esp32p4_lvgl9.ino   Minimal hello-LVGL9 sketch (display + touch + demo)
pins_config.h               Board pin map (JD9165 + GT911, 1024x600)
lv_conf.h                   Clean LVGL v9 config (COLOR_DEPTH 16, ASM NONE, Montserrat)
src/lcd/                    <-- YOU add the JD9165 vendor driver here
src/touch/                  <-- YOU add the GT911 vendor driver here
LICENSE
.gitignore
```

The `src/lcd` and `src/touch` folders are **empty on purpose** — see below.

---

## Getting the vendor drivers (not included)

The JD9165 panel init sequence and GT911 bring-up are board specific and
proprietary to the Guition demo, so they are **not** shipped here. The sketch
references them through two small wrapper classes:

```cpp
class jd9165_lcd {
  jd9165_lcd(int8_t rst_pin);
  void begin();
  void lcd_draw_bitmap(uint16_t x1, uint16_t y1,
                       uint16_t x2, uint16_t y2, uint8_t *color_data);
};

class gt911_touch {
  gt911_touch(int8_t sda, int8_t scl, int8_t rst = -1, int8_t irq = -1);
  void begin();
  bool getTouch(uint16_t *x, uint16_t *y);   // true while pressed
};
```

Get the driver sources from the **vendor demo package** for the JC1060P470C
(the Guition/manufacturer Arduino or ESP-IDF demo, typically shipped on the
product page or a vendor GitHub repo). Copy the JD9165 + GT911 files into:

```
src/lcd/jd9165_lcd.{h,cpp}   (+ any esp_lcd_jd9165.* it needs)
src/touch/gt911_touch.{h,cpp} (+ any esp_lcd_touch*.* it needs)
```

If the vendor class API differs, wrap it so it matches the interface above.

---

## Build & flash

**Toolchain:** arduino-cli + ESP32 Arduino core with ESP32-P4 support, LVGL 9.x.

**Exact FQBN:**

```
esp32:esp32:esp32p4:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled
```

```bash
arduino-cli compile --upload \
  -b "esp32:esp32:esp32p4:USBMode=default,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=enabled" \
  -p /dev/cu.usbmodemXXXX .
```

Put `lv_conf.h` **next to** the `lvgl` library folder (i.e.
`Arduino/libraries/lv_conf.h`), which is where LVGL looks for it by default.

---

## The 3 fixes that cost hours

### 1) Neutralize LVGL's Arm `.S` files (RISC-V "unrecognized opcode")

LVGL 9 ships Arm assembly blend routines — `lv_blend_neon.S` and
`lv_blend_helium.S`. On the **RISC-V** ESP32-P4 the build system still feeds
those `.S` files to the RISC-V assembler and the compile dies with:

```
Error: unrecognized opcode `typedef ...'
```

**Fix:** neutralize the `.S` files so they compile to nothing on this target.
Keep a backup, because it must be **re-applied every time LVGL is reinstalled**.
The files live under:

```
Arduino/libraries/lvgl/src/draw/sw/blend/neon/lv_blend_neon.S
Arduino/libraries/lvgl/src/draw/sw/blend/helium/lv_blend_helium.S
```

Simplest reliable approach — back them up and empty them:

```bash
LVGL_BLEND="$HOME/Documents/Arduino/libraries/lvgl/src/draw/sw/blend"
for f in neon/lv_blend_neon.S helium/lv_blend_helium.S; do
  [ -f "$LVGL_BLEND/$f" ] && cp "$LVGL_BLEND/$f" "$LVGL_BLEND/$f.orig" && : > "$LVGL_BLEND/$f"
done
```

(This pairs with fix #2 — with ASM set to NONE, these routines are never
needed at runtime anyway.)

### 2) `lv_conf.h` v9 with software-ASM disabled

In `lv_conf.h`, force the software renderer to use **no** CPU-specific
assembly:

```c
#define LV_USE_DRAW_SW_ASM      LV_DRAW_SW_ASM_NONE
#define LV_USE_NATIVE_HELIUM_ASM 0
```

Also confirmed in the shipped `lv_conf.h`:
`LV_COLOR_DEPTH 16` (RGB565, matches the JD9165 flush path) and Montserrat
14/28/48 enabled for the demo fonts.

### 3) `PSRAM=enabled` + allocate the draw buffers from PSRAM

If PSRAM is off (the Arduino **Tools** default resets it on core reinstall),
`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` returns `NULL`, the draw buffer
allocation fails, and you get a **black screen**. Keep `PSRAM=enabled` in the
FQBN, and allocate the two full-screen RGB565 draw buffers from PSRAM:

```c
const size_t buf_bytes = (size_t)LCD_H_RES * LCD_V_RES * sizeof(uint16_t);
draw_buf_1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
draw_buf_2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
```

---

## Important: UART crashes the MIPI-DSI on the ESP32-P4

On this board, bringing up the **UART** peripheral crashes the **MIPI-DSI**
link (flickering / boot loop). This was reproduced across multiple pin and
USB-mode combinations. Do **not** use UART for board peripherals.

Use the **native ESP-IDF I2C / I2S drivers** instead — they coexist with the
display (the GT911 touch driver already uses native IDF I2C, which is exactly
why touch and display run together without issue).

---

## License

MIT — see [LICENSE](LICENSE).
