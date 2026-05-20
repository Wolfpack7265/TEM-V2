#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "ui/ui.h" // Import the generated UI header

TRGBSuppport trgb;
void setup() {
  Serial.begin(115200);

  delay(100);   // Rumors say it helps avoid sporadical crashes after wakeup from deep-sleep
  trgb.init();

  // Print some info to Serial
  TRGBSuppport::print_chip_info();
  TRGBSuppport::scan_iic();

  // Initialize SD Card. It can be accessed by SD_MMC object.
  trgb.SD_init();

  // load UI
  ui_init();

    
}

void loop() {
    lv_timer_handler(); // Handles animations and drawing
    delay(5);
}