/**
 * guition_esp32p4_lvgl9.ino
 *
 * Minimal "hello LVGL 9" baseline for the Guition JC1060P470C board:
 *   - 7" 1024x600 IPS panel, JD9165 controller over MIPI-DSI
 *   - GT911 capacitive touch over I2C
 *   - ESP32-P4 + PSRAM
 *   - LVGL 9.x, double draw buffer allocated from PSRAM
 *
 * The demo draws a title label and a button; tapping the button increments a
 * counter, proving that display, flush and touch input all work end to end.
 *
 * -------------------------------------------------------------------------
 * VENDOR DRIVERS ARE NOT INCLUDED.
 * -------------------------------------------------------------------------
 * The JD9165 panel init sequence and the GT911 bring-up are board specific
 * and proprietary to the Guition demo. This sketch only *references* them via
 * two small C++ wrapper classes with the interface below. Grab the driver
 * sources from the vendor demo package (see README.md, section "Getting the
 * vendor drivers") and drop them under:
 *
 *   src/lcd/jd9165_lcd.{h,cpp}   -> class jd9165_lcd
 *   src/touch/gt911_touch.{h,cpp}-> class gt911_touch
 *
 * Expected wrapper interfaces (adapt names to your copy if needed):
 *
 *   class jd9165_lcd {
 *   public:
 *     jd9165_lcd(int8_t rst_pin);
 *     void begin();
 *     void lcd_draw_bitmap(uint16_t x1, uint16_t y1,
 *                          uint16_t x2, uint16_t y2, uint8_t *color_data);
 *   };
 *
 *   class gt911_touch {
 *   public:
 *     gt911_touch(int8_t sda, int8_t scl, int8_t rst = -1, int8_t irq = -1);
 *     void begin();
 *     bool getTouch(uint16_t *x, uint16_t *y);  // true while pressed
 *   };
 *
 * IMPORTANT (see README): the ESP32-P4 UART peripheral crashes the MIPI-DSI
 * link. Use the NATIVE ESP-IDF I2C / I2S drivers for any peripheral. The
 * GT911 driver already uses native IDF I2C and coexists with the display.
 *
 * License: MIT. See LICENSE.
 */

#include <Arduino.h>
#include "lvgl.h"
#include "pins_config.h"

// --- Vendor board drivers (provide your own copies; see header note) ---
#include "src/lcd/jd9165_lcd.h"
#include "src/touch/gt911_touch.h"

#include "esp_heap_caps.h"

// ---------------------------------------------------------------------------
// Driver instances
// ---------------------------------------------------------------------------
static jd9165_lcd   lcd(LCD_RST);
static gt911_touch  touch(TP_I2C_SDA, TP_I2C_SCL, TP_RST, TP_INT);

static lv_display_t *disp        = nullptr;
static lv_indev_t   *touch_indev = nullptr;

// Double draw buffer, allocated from PSRAM at runtime (see setup()).
static uint8_t *draw_buf_1 = nullptr;
static uint8_t *draw_buf_2 = nullptr;

// Demo state
static lv_obj_t *counter_label = nullptr;
static uint32_t  tap_count     = 0;

// ---------------------------------------------------------------------------
// LVGL flush callback: push the rendered area to the panel.
// ---------------------------------------------------------------------------
static void disp_flush_cb(lv_display_t *d, const lv_area_t *area, uint8_t *px_map)
{
    // JD9165 wrapper expects inclusive/exclusive bounds as (x1, y1, x2+1, y2+1).
    lcd.lcd_draw_bitmap(area->x1, area->y1, area->x2 + 1, area->y2 + 1, px_map);
    lv_display_flush_ready(d);
}

// ---------------------------------------------------------------------------
// LVGL input device callback: read the GT911 touch controller.
// ---------------------------------------------------------------------------
static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    uint16_t x, y;
    if (touch.getTouch(&x, &y)) {
        if (x >= LCD_H_RES) x = LCD_H_RES - 1;
        if (y >= LCD_V_RES) y = LCD_V_RES - 1;
        data->point.x = x;
        data->point.y = y;
        data->state   = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ---------------------------------------------------------------------------
// Demo UI
// ---------------------------------------------------------------------------
static void button_event_cb(lv_event_t *e)
{
    (void)e;
    tap_count++;
    if (counter_label) {
        lv_label_set_text_fmt(counter_label, "Taps: %lu", (unsigned long)tap_count);
    }
}

static void build_demo_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101418), LV_PART_MAIN);

    // Title
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Guition ESP32-P4 - LVGL 9");
    lv_obj_set_style_text_color(title, lv_color_hex(0xF0F0F0), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 40);

    // Subtitle
    lv_obj_t *sub = lv_label_create(scr);
    lv_label_set_text(sub, "1024x600 - JD9165 MIPI-DSI - GT911 touch");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x8AA0B0), 0);
    lv_obj_align(sub, LV_ALIGN_TOP_MID, 0, 90);

    // Button
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 320, 120);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(btn, button_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, "TAP ME");
    lv_obj_set_style_text_font(btn_lbl, &lv_font_montserrat_28, 0);
    lv_obj_center(btn_lbl);

    // Counter
    counter_label = lv_label_create(scr);
    lv_label_set_text(counter_label, "Taps: 0");
    lv_obj_set_style_text_color(counter_label, lv_color_hex(0x40C060), 0);
    lv_obj_set_style_text_font(counter_label, &lv_font_montserrat_28, 0);
    lv_obj_align(counter_label, LV_ALIGN_CENTER, 0, 140);
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup(void)
{
    Serial.begin(115200);
    delay(200);
    Serial.println("Guition ESP32-P4 LVGL9 hello");

    // 1) Bring up the panel and touch controller (vendor drivers).
    lcd.begin();
    touch.begin();

    // 2) Init LVGL and wire its tick to millis().
    lv_init();
    lv_tick_set_cb((lv_tick_get_cb_t)millis);

    // 3) Allocate the double draw buffer from PSRAM.
    //    RGB565 (LV_COLOR_DEPTH 16) -> 2 bytes per pixel.
    //    A full-screen buffer is used here for simplicity; a partial buffer
    //    (e.g. 1/10 of the screen) also works and uses far less RAM.
    const size_t px_count  = (size_t)LCD_H_RES * LCD_V_RES;
    const size_t buf_bytes = px_count * sizeof(uint16_t);   // 2 bytes/px

    draw_buf_1 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    draw_buf_2 = (uint8_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM);
    if (!draw_buf_1 || !draw_buf_2) {
        // Almost always means PSRAM is disabled in the FQBN (black screen).
        Serial.println("PSRAM draw-buffer alloc FAILED - enable PSRAM=enabled");
        while (true) delay(1000);
    }

    // 4) Create the display, attach the flush cb and the PSRAM buffers.
    disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, draw_buf_1, draw_buf_2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    // 5) Create the touch input device.
    touch_indev = lv_indev_create();
    lv_indev_set_type(touch_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(touch_indev, touch_read_cb);
    lv_indev_set_display(touch_indev, disp);

    // 6) Build the demo screen.
    build_demo_ui();

    Serial.println("READY");
}

void loop(void)
{
    lv_timer_handler();   // let LVGL render and process input
    delay(5);
}
