#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h" // You must include this specifically
#include "ui/ui.h" // Import the generated UI header
#include "FS.h"
#include "SD.h"
#include "ELMduino.h"
#include "NimBLEDevice.h"
#include <deque>

ELM327 myELM327;

TRGBSuppport trgb;

static NimBLEAddress obdAddress("AA:BB:CC:11:22:33", 1);
static NimBLEClient* pClient = nullptr;
static lv_style_t style_orange;
static lv_style_t style_red;

static int current_rpm = -1;

static BLEAddress *pServerAddress;
static BLERemoteCharacteristic *pRemoteCharacteristic;
static bool connected = false;

// ELMduino needs a Stream interface, so we map it here
class OBDStream : public Stream {
public:
    int available() override { return rxQueue.size(); }
    
    int read() override { 
        if (rxQueue.empty()) return -1;
        uint8_t c = rxQueue.front(); 
        rxQueue.pop_front(); 
        return c; 
    }
    
    int peek() override { 
        if (rxQueue.empty()) return -1;
        return rxQueue.front(); 
    }
    
    size_t write(uint8_t c) override { 
        if(pRemoteCharacteristic) pRemoteCharacteristic->writeValue(&c, 1, false);
        return 1; 
    }
    
    void flush() override {}
    
    void push(uint8_t* data, size_t len) { 
        for(size_t i=0; i<len; i++) rxQueue.push_back(data[i]); 
    }
private:
    std::deque<uint8_t> rxQueue; 
} obdStream;

// Helper to update the label safely from any task
void updateLabelAsync(const char* newText) {
    // THIS WILL TELL US EXACTLY WHAT IS HAPPENING
    Serial.printf("DEBUG: UI Update Request Received. Text: '%s'\n", newText);
    
    char* textCopy = strdup(newText);
    lv_async_call([](void* data) {
        char* text = (char*)data;
        if (ui_CenterLabel != NULL) {
            lv_label_set_text(ui_CenterLabel, text);
        }
        free(text);
    }, textCopy);
}

void obdTask(void* parameter) {
    bool initialized = false;
    updateLabelAsync("connecting...");

    while (true) {
        if (connected && pRemoteCharacteristic != nullptr) {
            if (!initialized) {
                if (myELM327.begin(obdStream, false, 2000)) {
                    myELM327.sendCommand("ATE0");
                    myELM327.sendCommand("ATL0");
                    initialized = true;
                    while(obdStream.available()) obdStream.read();
                }
            } else {
                // Only poll if initialized
                float rpm = myELM327.rpm();
                if (myELM327.get_response() == ELM_SUCCESS) {
                    if (rpm > 0) {
                        current_rpm = (int)rpm; // Update global variable
                        char buffer[16];
                        snprintf(buffer, sizeof(buffer), "%d", current_rpm);
                        updateLabelAsync(buffer);
                    }
                } else if (myELM327.get_response() == ELM_TIMEOUT) {
                    initialized = false;
                    current_rpm = -1; // Reset to "Disconnected" state
                    updateLabelAsync("connecting...");
                }   else {
                    initialized = false;
                    current_rpm = -1; // Reset to "Disconnected" state
                    updateLabelAsync("connecting...");
}
            }
        }
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}


static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    obdStream.push(pData, length);
}

void onConnect(NimBLEClient* pClient) {
    // 1. You MUST find the characteristic first
    BLERemoteService* pService = pClient->getService("000018f0-0000-1000-8000-00805f9b34fb");
    if (pService) {
        pRemoteCharacteristic = pService->getCharacteristic("00002af1-0000-1000-8000-00805f9b34fb");
        
        if (pRemoteCharacteristic) {
            // 2. ONLY subscribe if you found the characteristic!
            pRemoteCharacteristic->subscribe(true, notifyCallback);
            
            // 3. ONLY start the task once subscription is successful
            connected = true;
            xTaskCreate(obdTask, "OBDTask", 16384, NULL, 1, NULL);
            Serial.println("OBD Task started successfully.");
        }
    }
}


class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println("Connected!");
        BLERemoteService* pService = pClient->getService("000018f0-0000-1000-8000-00805f9b34fb");
        if (pService) {
            pRemoteCharacteristic = pService->getCharacteristic("00002af1-0000-1000-8000-00805f9b34fb");
            if (pRemoteCharacteristic) {
                pRemoteCharacteristic->subscribe(true, notifyCallback);
                connected = true;
                xTaskCreate(obdTask, "OBDTask", 16384, NULL, 1, NULL);
            }
        }
    }

    // UPDATE THIS: Add the second parameter 'ble_gap_conn_desc*'
    void onDisconnect(NimBLEClient* pClient, ble_gap_conn_desc* desc){
        connected = false;
        Serial.println("Disconnected!");
        // Reset pointers/tasks here to prevent the obdTask from trying to access them
        pRemoteCharacteristic = nullptr;
    }
};

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
    trgb.init();
    ui_init();
    lv_label_set_text(ui_CenterLabel, "connecting...");
    setup_gauge_styles();

    NimBLEDevice::init("ESP32_Client");
    pClient = NimBLEDevice::createClient();

    // Define a class for callbacks or just use a helper
    static MyClientCallback clientCB; // You need to define this class
    pClient->setClientCallbacks(&clientCB);

    // Initiate connection (Non-blocking)
    if (!pClient->connect(obdAddress)) {
        Serial.println("Failed to initiate connection");
    }
}

void loop() {
    lv_timer_handler();

    lv_color_t target_color;
    int arc_value;

    if (current_rpm == -1) {
        arc_value = 0;
    } else {
        arc_value = map(constrain(current_rpm, 0, 6500), 0, 6500, 0, 100);
        
        // Logic for Colors
        if (current_rpm >= 6000) {
            target_color = lv_color_hex(0xFF0000); // Red
        } else if (current_rpm >= 5000) {
            target_color = lv_color_hex(0xFF8800); // Orange
        } else {
            target_color = lv_color_hex(0xD9D9D9); // Normal
        }
    }

    // Apply color and value
    lv_obj_set_style_arc_color(ui_Main_Gauge, target_color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_arc_set_value(ui_Main_Gauge, arc_value);
    lv_obj_invalidate(ui_Main_Gauge);

    lv_refr_now(NULL); 
    delay(16.6);
}

//lv_refr_now(NULL); 
//delay(16.6);