#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h" // You must include this specifically
#include "ui/ui.h" // Import the generated UI header
#include "FS.h"
#include "SD.h"

TRGBSuppport trgb;

static lv_style_t style_orange;
static lv_style_t style_red;

void setup_gauge_styles() {
    // Style for normal range
    lv_style_init(&style_orange);
    lv_style_set_arc_color(&style_orange, lv_color_hex(0xFF8800));

    // Style for warning range (using your hex code)
    // Hex to LVGL color conversion: lv_color_hex(0xYourHexCode)
    lv_style_init(&style_red);
    lv_style_set_arc_color(&style_red, lv_color_hex(0xFF0000)); // Example: Red-Orange
}

void update_gauge_color(int value) {
    if (value > 8) { // Change color when value > 80
        lv_obj_add_style(ui_Main_Gauge, &style_orange, LV_PART_INDICATOR);
    } else if(value> 10) {
        lv_obj_add_style(ui_Main_Gauge, &style_red, LV_PART_INDICATOR);
    }
}
void setup() {
  Serial.begin(460800);
  delay(2000);
  Serial.println("Starting...");

  trgb.init();
  Serial.println("TRGB Init Done.");

  ui_init();

  setup_gauge_styles();

  Serial.println("UI Init Done.");

}

void loop() {
    lv_timer_handler();

    static int section = 0; 
    static int direction = 1;
    static uint32_t last_move = 0;

    if (millis() - last_move > 1000) {
        last_move = millis();
        section += direction;
        if (section >= 13 || section <= 0) direction *= -1;

        int value = 1 + (int)(section * 7.46);

        // Logic for Colors:
        // 9th segment (index 8) -> Orange
        // 11th segment (index 10) -> Red
        
        // Remove previous styles to prevent stacking
        lv_obj_remove_style(ui_Main_Gauge, &style_orange, LV_PART_INDICATOR);
        lv_obj_remove_style(ui_Main_Gauge, &style_red, LV_PART_INDICATOR);

        // 1. Logic to pick the color
        lv_color_t target_color;
        
        if (section >= 12) {
            target_color = lv_color_hex(0xFF0000); // Red
        } else if (section >= 10) {
            target_color = lv_color_hex(0xFF8800); // Orange
        } else {
            target_color = lv_color_hex(0xD9D9D9); // Default/Green (Set this to your normal color)
        }

        // 2. Apply the color directly to the indicator part
        lv_obj_set_style_arc_color(ui_Main_Gauge, target_color, LV_PART_INDICATOR | LV_STATE_DEFAULT);

        // 3. Update the value
        lv_arc_set_value(ui_Main_Gauge, value);
        
        // 4. Force a refresh of the object
        lv_obj_invalidate(ui_Main_Gauge);

    }
    lv_refr_now(NULL); 
    delay(16.6);
}

//lv_refr_now(NULL); 
//delay(16.6);