#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "ui/ui.h" // Import the generated UI header

TRGBSuppport trgb;
void setup() {
  
 // 1. MUST start serial first
    Serial.begin(115200);
    delay(100); // Give the bridge chip time to handshake
    
    Serial.println("----- Setup Starting -----");

    // 2. Initialize board
    trgb.init();
    
    // 3. Load UI
    ui_init();

    Serial.println("----- Setup Complete -----");
  // Print some info to Serial
  TRGBSuppport::print_chip_info();
  TRGBSuppport::scan_iic();

  // Initialize SD Card. It can be accessed by SD_MMC object.
  //trgb.SD_init();

}

void loop() {
    lv_timer_handler(); // Crucial: must be in every loop!

    // Simple test: move the gauge every 0.5 seconds
    static uint32_t last_move = 0;
    if (millis() - last_move > 500) {
        last_move = millis();
        
        // This targets your SquareLine needle (replace 'ui_Needle' with your actual object name)
        // This will swing the needle back and forth to prove the code is connected
        static int val = 0;
        val = (val == 0) ? 100 : 0; 
        Serial.printf("Gauge Value: %d\n", val);
        lv_arc_set_value(ui_Secondary_Gauge, val); 
    }
}