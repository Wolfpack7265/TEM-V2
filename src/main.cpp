#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h" // You must include this specifically
#include "ui/ui.h" // Import the generated UI header
#include "FS.h"
#include "SD.h"

TRGBSuppport trgb;
void setup() {
  Serial.begin(460800);
  delay(2000);
  Serial.println("Starting...");

  trgb.init();
  Serial.println("TRGB Init Done.");

  ui_init();

  

  Serial.println("UI Init Done.");

}

void loop() {
    
  lv_timer_handler();

    static int var = 0;
    static int direction = 1;
    static uint32_t last_move = 0;

    if (millis() - last_move > 50) {
        last_move = millis();
        var += direction;
        if (var >= 100 || var <= 0) direction *= -1;

        lv_arc_set_value(ui_Secondary_Gauge, var);

        lv_disp_t * dispp = lv_disp_get_default();
        if(dispp->driver->flush_cb) {
        // This is the direct call to your screen's hardware driver
        // It tells the screen: "The buffer is ready, push it now!"

       
    }
}
 // FORCED REFRESH
  lv_refr_now(NULL); 
  delay(16.6);
}