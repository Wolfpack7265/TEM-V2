#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <TRGBSuppport.h>
#include <lvgl.h>
#include "draw/lv_draw_mask.h"
#include "ui/ui.h"
#include "FS.h"
#include "SD.h"

float baro_pressure, manifold_abs_pressure, fuelLevel, boost;

int max_main_value = 12;
int min_main_value = -10;
int segments = 15;
int increment_per_segment = max_main_value/segments;


bool map_ready = false;
bool baro_ready = false;

static lv_style_t style_orange;
static lv_style_t style_red;
static lv_style_t style_grey;

lv_color_t target_color;
int main_arc;
int secondary_arc;
char center_text[12] = "";

static int last_main_arc = 0;
static int last_secondary_arc = 0;
static char last_fuel_text[8] = "0%";
static lv_color_t last_main_color = lv_color_hex(0xD9D9D9);

TRGBSuppport trgb;


// ===== Protocol Configuration =====
#define OBD_PROTOCOL "ATSP6"
#define PROTOCOL_NAME "ISO 15765-4"

// ===== BLE UUIDs =====
#define SERVICE_UUID           "0000fff0-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_TX "0000fff1-0000-1000-8000-00805f9b34fb"
#define CHARACTERISTIC_UUID_RX "0000fff2-0000-1000-8000-00805f9b34fb"

// ===== Global Variables =====
static BLEAdvertisedDevice* elmDevice = nullptr;
static BLEClient* pClient = nullptr;
static BLERemoteCharacteristic* pTxCharacteristic = nullptr;
static BLERemoteCharacteristic* pRxCharacteristic = nullptr;
static bool deviceConnected = false;
static bool doConnect = false;
static String receivedData = "";

// ===== Callbacks =====
class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        String deviceName = String(advertisedDevice.getName().c_str());
        if (deviceName.indexOf("OBD") != -1 || deviceName.indexOf("ELM") != -1) {
            BLEDevice::getScan()->stop();
            elmDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
};

static void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    for (int i = 0; i < length; i++) receivedData += (char)pData[i];
}

class MyClientCallback : public BLEClientCallbacks {
    void onConnect(BLEClient* pclient) { deviceConnected = true; }
    void onDisconnect(BLEClient* pclient) { deviceConnected = false; }
};

// ===== Core OBD2 Functions =====
bool connectToELM327() {
    pClient = BLEDevice::createClient();
    pClient->setClientCallbacks(new MyClientCallback());
    if (!pClient->connect(elmDevice)) return false;
    
    BLERemoteService* pRemoteService = pClient->getService(SERVICE_UUID);
    if (!pRemoteService) return false;
    
    pTxCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_TX);
    pRxCharacteristic = pRemoteService->getCharacteristic(CHARACTERISTIC_UUID_RX);
    
    if (pTxCharacteristic && pTxCharacteristic->canNotify()) {
        pTxCharacteristic->registerForNotify(notifyCallback);
    }
    return true;
}

void sendOBDCommand(String command) {
    if (!deviceConnected) return;
    receivedData = "";
    command += "\r";
    pRxCharacteristic->writeValue(command.c_str(), command.length());
}

String waitForResponse(int timeoutMs = 2000) {
    unsigned long startTime = millis();
    while (millis() - startTime < timeoutMs) {
        if (receivedData.indexOf('>') != -1) {
            String response = receivedData;
            receivedData = "";
            return response;
        }
        delay(10);
    }
    return "";
}

bool initializeELM327() {
    sendOBDCommand("ATZ"); delay(2500);
    sendOBDCommand("ATE0"); delay(500);
    sendOBDCommand("ATS0"); delay(500);
    sendOBDCommand("ATL0"); delay(500);
    sendOBDCommand(OBD_PROTOCOL); delay(2000);
    sendOBDCommand("ATH0"); delay(500);
    sendOBDCommand("ATAT2"); delay(500);
    sendOBDCommand("ATSTFF"); delay(500);
    sendOBDCommand("ATKW"); delay(500);
    Serial.println("ELM327 Initialized");
    return true;
}


void parseResponse(const char* pid) {
    // Ensure we have data
    if (receivedData.length() < 6) return;

    // Remove noise from the raw response string
    receivedData.replace(" ", "");
    receivedData.replace("\r", "");
    receivedData.replace("\n", "");
    receivedData.replace(">", "");

    // Find the position of the PID echo
    int pidPos = receivedData.indexOf("41"); // 41 is the standard Mode 01 response
    if (pidPos == -1) return;

    // Extract the hex data bytes (assuming standard 2-byte data)
    // The PID is 2 chars, and the data follows immediately
    String hexData = receivedData.substring(pidPos + 4, pidPos + 6);
    int rawValue = (int)strtol(hexData.c_str(), NULL, 16);

    // Apply specific formulas based on which PID we requested
    if (strcmp(pid, "010B") == 0) {
        // MAP: A (kPa)
        manifold_abs_pressure = (float)rawValue; 
    } 
    else if (strcmp(pid, "012F") == 0) {
        // Fuel Level: (100 / 255) * A (%)
        fuelLevel = (float)rawValue * (100.0 / 255.0);
    }
    else if (strcmp(pid, "0133") == 0) {
        // Barometric Pressure: A (kPa)
        baro_pressure = (float)rawValue;
    }

    // Clear buffer for next request
    receivedData = "";
}

float calculateBoost(float mapKpa, float baroKpa) {
    // 1. Calculate the difference (Gauge Pressure in kPa)
    float gaugePressureKpa = mapKpa - baroKpa;
    
    // 2. Convert kPa to PSI (1 kPa ≈ 0.1450377 PSI)
    float boostPsi = gaugePressureKpa * 0.1450377;
    
    // 3. Round to 2 decimal places
    // Multiply by 100, round to nearest integer, divide by 100.0
    return roundf(boostPsi * 100.0f) / 100.0f;
}

enum State { SENDING, WAITING, PARSING };
State currentState = SENDING;

// Track which PID we are currently working on
int currentPIDIndex = 0;
const char* pids[] = {"0133", "010B", "012F"}; // Baro, MAP, Fuel
unsigned long lastActionTime = 0;
const unsigned long timeout = 200; // 500ms max wait per PID

struct UIUpdateData {
    int neg_arc;      // For ui_Main_Gauge_Negative (-10 to 0)
    int pos_arc;      // For ui_Main_Gauge (0 to 12)
    lv_color_t color; // Color for the boost gauge
    int secondary_arc;
    char fuel_text[8];

    UIUpdateData(int n, int p, lv_color_t c, int s, const char* f, const char* g) 
        : neg_arc(n), pos_arc(p), color(c), secondary_arc(s) { 
        strncpy(fuel_text, f, sizeof(fuel_text)); 
        strncpy(center_text, g, sizeof(fuel_text)); 
    }
};

void handleOBDStateMachine() {
    switch (currentState) {
        case SENDING:
            sendOBDCommand(pids[currentPIDIndex]);
            lastActionTime = millis();
            currentState = WAITING;
            break;

        case WAITING:
            // Check if we got a response or if we timed out
            if (receivedData.indexOf('>') != -1 || (millis() - lastActionTime > timeout)) {
                currentState = PARSING;
            }
            break;

        case PARSING:
    parseResponse(pids[currentPIDIndex]);

    // Update the "Ready" flags
    if (strcmp(pids[currentPIDIndex], "010B") == 0) map_ready = true;
    if (strcmp(pids[currentPIDIndex], "0133") == 0) baro_ready = true;

    int neg_arc = 0;
    int pos_arc = 0;

    // Use the static variables we defined earlier to keep values persistent
    if (map_ready && baro_ready) {
        float boost = calculateBoost(manifold_abs_pressure, baro_pressure);
        
        if (boost < 0) {
            // --- Negative Gauge: -10 to 0 (Target: 3 segments) ---
            // 1. Convert to a positive vacuum value (e.g., -5 becomes 5)
            //float segment_index = (constrain(boost, -10.0, 0.0) / 10.0) * 3.0;
            float vacuum_val = abs(constrain(boost, -10.0, 0.0));
            // 2. Map vacuum to 0.0 - 3.0 range
            float segment_index = ((10.0 - vacuum_val) / 10.0) * 3.0;
            // 2. Snap to nearest segment (0, 1, 2, or 3)
            int snapped_segment = round(segment_index);
            // 3. Scale to 0-100% (33.33% per segment)
            neg_arc = (int)(snapped_segment * 33.33);
            pos_arc = 0;
        } else {
            // --- Positive Gauge: 0 to 12 (Target: 15 segments) ---
            neg_arc = 100;
            // 1. Map boost to 0.0 - 15.0 range
            float segment_index = (constrain(boost, 0.0, 12.0) / 12.0) * 15.0;
            // 2. Snap to nearest segment (0 through 15)
            int snapped_segment = round(segment_index);
            // 3. Scale to 0-100% (6.67% per segment)
            pos_arc = (int)(snapped_segment * 6.67);
        }
    }

    if (strcmp(pids[currentPIDIndex], "012F") == 0) {
        last_secondary_arc = (int)fuelLevel;
        snprintf(last_fuel_text, sizeof(last_fuel_text), "%d%%", (int)fuelLevel);
    }

    // Now instantiate the struct using the global definition
    UIUpdateData* data = new UIUpdateData(neg_arc, pos_arc, last_main_color, last_secondary_arc, last_fuel_text, center_text);

    lv_async_call([](void* arg) {
        UIUpdateData* d = (UIUpdateData*)arg;
        if (ui_Main_Gauge_Negative) 
            lv_arc_set_value(ui_Main_Gauge_Negative, d->neg_arc);
        
            if (ui_Main_Gauge) {
            lv_obj_set_style_arc_color(ui_Main_Gauge, d->color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
            lv_arc_set_value(ui_Main_Gauge, d->pos_arc);
        }
        if (ui_Secondary_Gauge) lv_arc_set_value(ui_Secondary_Gauge, d->secondary_arc);
        if (ui_Secondary_Label) lv_label_set_text(ui_Secondary_Label, d->fuel_text);
        
        delete d; // Clean up the heap memory
    }, data);

    currentPIDIndex = (currentPIDIndex + 1) % 3;
    currentState = SENDING;
    break;
    }
}

/////////////////////////////////////////////////////////////////////////////////////////
//                  LVGL functions
///////////////////////////////////////////////////////////////////////////////////////////////
void updateLabelAsync(const char* newText) {
    // THIS WILL TELL US EXACTLY WHAT IS HAPPENING
    Serial.printf("DEBUG: UI Update Request Received. Text: '%s'\n", newText);
    
    char* textCopy = strdup(newText);
    lv_async_call([](void* data) {
        char* text = (char*)data;
        if (ui_Center_Label != NULL) {
            lv_label_set_text(ui_Center_Label, text);
        }
        free(text);
    }, textCopy);
}

void setup_gauge_styles() {
    // Style for normal range
    lv_style_init(&style_grey);
    lv_style_set_arc_color(&style_grey, lv_color_hex(0xD9D9D9));

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

/////////////////////////////////////////////////////////////////////////////////////////
//                  Setup and Loop
///////////////////////////////////////////////////////////////////////////////////////////////

void setup() {
    Serial.begin(460800);
    trgb.init();
    ui_init();
    setup_gauge_styles();
    delay(500); // Give some time for the UI to initialize before starting BLE
    Serial.println("UI Initialized, starting BLE...");

    BLEDevice::init("ESP32_OBD_Reader");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(10, false);
}

void loop() {
    // 1. Handle BLE connection state
    if (doConnect) {
        if (connectToELM327() && initializeELM327()) {
            doConnect = false;
        }
    }else if(elmDevice == nullptr) {
            //Serial.println("Not Connected.........");
            lv_label_set_text(ui_Center_Label, "N/A");
            lv_label_set_text(ui_Secondary_Label, "0%");
            lv_arc_set_value(ui_Main_Gauge, 0);
            lv_arc_set_value(ui_Main_Gauge_Negative, 0);
            lv_arc_set_value(ui_Secondary_Gauge, 0);
            //disconnected_message = true;
        
    }

    // 2. Handle OBD data cycle (This is your State Machine)
    if (deviceConnected) {
        lv_label_set_text(ui_Center_Label, "");
        handleOBDStateMachine();
    }
    
    // 3. Handle LVGL UI tasks
    lv_timer_handler();
    
    lv_refr_now(NULL); 
    delay(1);
}
    //lv_refr_now(NULL); 
   // delay(16.6);

