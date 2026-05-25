#include <Arduino.h>
#include "NimBLEDevice.h"

void setup() {
    Serial.begin(460800);
    delay(2000); // Important: Wait for Serial to stabilize

    Serial.println(">>> [DEBUG] Starting BLE Initialization...");
    
    // 1. Ensure any previous state is wiped
    NimBLEDevice::deinit(true);
    
    // 2. Explicitly initialize
    if (!NimBLEDevice::init("ESP32_Scanner")) {
        Serial.println(">>> [ERROR] BLE Initialization failed!");
        return;
    }

    // 3. Set the radio power explicitly
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);

    Serial.println(">>> [DEBUG] Starting 10s scan...");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    pScan->setInterval(100);
    pScan->setWindow(99);
    
    // 4. Start the scan and capture the boolean success/fail
    bool scanStarted = pScan->start(10, false);
    
    if (scanStarted) {
        NimBLEScanResults results = pScan->getResults();
        Serial.printf(">>> [DEBUG] Scan finished. Found %d devices.\n", results.getCount());
        for(int i = 0; i < results.getCount(); i++) {
            Serial.printf("Device %d: %s\n", i, results.getDevice(i)->getAddress().toString().c_str());
        }
    } else {
        Serial.println(">>> [ERROR] Scan failed to start.");
    }
}

void loop() {}
/*
#include <Arduino.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h"
#include "ui/ui.h"
#include "FS.h"
#include "SD.h"
#include "ELMduino.h"
#include "NimBLEDevice.h"
#include <deque>

ELM327 myELM327;
TRGBSuppport trgb;

// Global variables needed by the callback
static bool connected = false;
static BLERemoteCharacteristic* pRemoteCharacteristic = nullptr;

// Forward declaration of functions used in the callback
static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);
void obdTask(void* parameter);

// NOW define the class
// 1. Define the class only ONCE
class MyClientCallback : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
        Serial.println(">>> [CALLBACK] Connected!");
        if (!pClient->discoverAttributes()) {
            pClient->disconnect();
            return;
        }
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

    // 2. Updated signature: Removed 'ble_gap_conn_desc*' if the compiler is strict,
    // or keep it and remove 'override' if it still fails.
    void onDisconnect(NimBLEClient* pClient) {
        connected = false;
        Serial.println(">>> [CALLBACK] Disconnected!");
        pRemoteCharacteristic = nullptr;
    }
};
// Now declare the static instance
static MyClientCallback clientCB;

// 3. Continue with other globals
static NimBLEAddress obdAddress("00:10:cc:4f:36:03", 1); 
static NimBLEClient* pClient = nullptr;

static lv_style_t style_orange;
static lv_style_t style_red;


static int current_rpm = -1;

static BLEAddress *pServerAddress;

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
    updateLabelAsync("no connect");

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
                    current_rpm = -1; // Reset to "no connect" state
                    updateLabelAsync("no connect");
                }   else {
                    initialized = false;
                    current_rpm = -1; // Reset to "no connect" state
                    updateLabelAsync("no connect");
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
    lv_label_set_text(ui_CenterLabel, "no connect");
    setup_gauge_styles();
    delay(500); // Give some time for the UI to initialize before starting BLE
    Serial.println("UI Initialized, starting BLE...");

    NimBLEDevice::deinit(true); 
    delay(500);

    NimBLEDevice::init("ESP32_Client");
    
    Serial.println(">>> [SETUP] Scanning for devices...");
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setActiveScan(true);
    
    // START the scan and wait for it to finish
    pScan->start(10, false); 
    
    // Access the results directly from the scanner object
    NimBLEScanResults results = pScan->getResults();
    
    Serial.printf(">>> [SETUP] Scan finished. Found %d devices.\n", results.getCount());
    for(int i = 0; i < results.getCount(); i++) {
        // Use '->' because getDevice returns a pointer
        Serial.printf("Device %d: %s\n", i, results.getDevice(i)->getAddress().toString().c_str());
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

*/