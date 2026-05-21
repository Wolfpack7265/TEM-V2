#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h" // You must include this specifically
#include "ui/ui.h" // Import the generated UI header
#include "FS.h"
#include "SD.h"
#include "LittleFS.h"

TRGBSuppport trgb;

lv_obj_t * gauge_segments[15];

// The custom driver to find files in LittleFS instead of an SD card
static void* my_fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode) {
    String newPath = "/storage/" + String(path);
    Serial.printf("DEBUG: Looking for file at: %s\n", newPath.c_str()); // Add this!
    return fopen(newPath.c_str(), mode == LV_FS_MODE_WR ? "wb" : "rb");
}

void setup() {
  Serial.begin(460800);
  delay(2000);
  Serial.println("Starting...");

  trgb.init();
  Serial.println("TRGB Init Done.");

  // 1. Mount the internal partition
    if(!LittleFS.begin(true, "/storage")){
        Serial.println("LittleFS Mount Failed!");
    }

    // 2. Register the driver that intercepts 'S:'
    static lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'S'; 
    drv.open_cb = my_fs_open;
    // You only need to define the 'open' callback for the path redirection
    // LVGL will use standard C file functions for the rest if you point them correctly
    lv_fs_drv_register(&drv);

  ui_init();

  // Move the image to the top layer of the screen temporarily
  lv_obj_t * act_scr = lv_scr_act();
  lv_obj_set_parent(ui_MainGaugeEmpty, act_scr); 

  // Clear any flags that might hide it
  lv_obj_clear_flag(ui_MainGaugeEmpty, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(ui_MainGaugeEmpty);

  // Diagnostic check in setup()
  // 2. Map your SquareLine objects to the array
    // gauge_segments[0] = ui_Section1;
    // gauge_segments[1] = ui_Section2;
    // gauge_segments[2] = ui_Section3;
    // gauge_segments[3] = ui_Section4;
    // gauge_segments[4] = ui_Section5;
    // gauge_segments[5] = ui_Section6;
    // gauge_segments[6] = ui_Section7;
    // gauge_segments[7] = ui_Section8;
    // gauge_segments[8] = ui_Section9;
    // gauge_segments[9] = ui_Section10;
    // gauge_segments[10] = ui_Section11;
    // gauge_segments[11] = ui_Section12;
    // gauge_segments[12] = ui_Section13;
    // gauge_segments[13] = ui_Section14;
    // gauge_segments[14] = ui_Section15;

  Serial.println("UI Init Done.");

  // Initialize SD Card. It can be accessed by SD_MMC object.
  //trgb.SD_init();

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