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
#define SERVICE_UUID          "0000fff0-0000-1000-8000-00805f9b34fb"
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

unsigned long lastValidDataTime = 0;

void parseResponse(const char* pid) {
    if (receivedData.length() < 6) {
        receivedData = "";
        return;
    }

    receivedData.replace(" ", "");
    receivedData.replace("\r", "");
    receivedData.replace("\n", "");
    receivedData.replace(">", "");

    int pidPos = receivedData.indexOf("41"); 
    if (pidPos == -1) {
        receivedData = ""; 
        return;
    }

    String hexData = receivedData.substring(pidPos + 4, pidPos + 6);
    int rawValue = (int)strtol(hexData.c_str(), NULL, 16);

    if (strcmp(pid, "010B") == 0) {
        manifold_abs_pressure = (float)rawValue; 
    } 
    else if (strcmp(pid, "012F") == 0) {
        fuelLevel = (float)rawValue * (100.0 / 255.0);
    }
    else if (strcmp(pid, "0133") == 0) {
        baro_pressure = (float)rawValue;
    }

    lastValidDataTime = millis(); 
    receivedData = "";
}

float calculateBoost(float mapKpa, float baroKpa) {
    float gaugePressureKpa = mapKpa - baroKpa;
    float boostPsi = gaugePressureKpa * 0.1450377;
    return roundf(boostPsi * 100.0f) / 100.0f;
}

enum State { SENDING, WAITING, PARSING };
State currentState = SENDING;

int currentPIDIndex = 0;
const char* pids[] = {"0133", "010B", "012F"}; 
unsigned long lastActionTime = 0;
const unsigned long timeout = 400; 

struct UIUpdateData {
    int neg_arc;      
    int pos_arc;      
    lv_color_t color; 
    int secondary_arc;
    char fuel_text[8];

    UIUpdateData(int n, int p, lv_color_t c, int s, const char* f, const char* g) 
        : neg_arc(n), pos_arc(p), color(c), secondary_arc(s) { 
        strncpy(fuel_text, f, sizeof(fuel_text)); 
        strncpy(center_text, g, sizeof(fuel_text)); 
    }
};

void handleOBDStateMachine() {
    if (millis() - lastValidDataTime > 5000) {
        currentState = SENDING;
        currentPIDIndex = 0;
        receivedData = "";
        lastValidDataTime = millis(); 
        return;
    }
    switch (currentState) {
        case SENDING:
            sendOBDCommand(pids[currentPIDIndex]);
            lastActionTime = millis();
            currentState = WAITING;
            break;

        case WAITING:
            if (receivedData.indexOf('>') != -1 || (millis() - lastActionTime > timeout)) {
                currentState = PARSING;
            }
            break;

        case PARSING:
            parseResponse(pids[currentPIDIndex]);

            if (strcmp(pids[currentPIDIndex], "010B") == 0) map_ready = true;
            if (strcmp(pids[currentPIDIndex], "0133") == 0) baro_ready = true;

            int neg_arc = 0;
            int pos_arc = 0;

            if (map_ready && baro_ready) {
                float boost = calculateBoost(manifold_abs_pressure, baro_pressure);
                
                if (boost < 0) {
                    float vacuum_val = abs(constrain(boost, -10.0, 0.0));
                    float segment_index = ((10.0 - vacuum_val) / 10.0) * 3.0;
                    int snapped_segment = round(segment_index);
                    neg_arc = (int)(snapped_segment * 33.33);
                    pos_arc = 0;
                } else {
                    neg_arc = 100;
                    float segment_index = (constrain(boost, 0.0, 12.0) / 12.0) * 15.0;
                    int snapped_segment = round(segment_index);
                    pos_arc = (int)(snapped_segment * 6.67);
                }
            }

            if (strcmp(pids[currentPIDIndex], "012F") == 0) {
                last_secondary_arc = (int)fuelLevel;
                snprintf(last_fuel_text, sizeof(last_fuel_text), "%d%%", (int)fuelLevel);
            }

            UIUpdateData* data = new UIUpdateData(neg_arc, pos_arc, last_main_color, last_secondary_arc, last_fuel_text, center_text);

            lv_async_call([](void* arg) {
                UIUpdateData* d = (UIUpdateData*)arg;
                if (ui_Main_Gauge_Negative) {
                    lv_arc_set_value(ui_Main_Gauge_Negative, d->neg_arc);
                    lv_obj_invalidate(ui_Main_Gauge_Negative);
                }
                
                if (ui_Main_Gauge) {
                    lv_obj_set_style_arc_color(ui_Main_Gauge, d->color, LV_PART_INDICATOR | LV_STATE_DEFAULT);
                    lv_arc_set_value(ui_Main_Gauge, d->pos_arc);
                    lv_obj_invalidate(ui_Main_Gauge);
                }
                if (ui_Secondary_Gauge) {
                    lv_arc_set_value(ui_Secondary_Gauge, d->secondary_arc);
                    lv_obj_invalidate(ui_Secondary_Gauge);
                }
                if (ui_Secondary_Label) {
                    lv_label_set_text(ui_Secondary_Label, d->fuel_text);
                    lv_obj_invalidate(ui_Secondary_Label);
                }
                
                delete d; 
            }, data);

            currentPIDIndex = (currentPIDIndex + 1) % 3;
            currentState = SENDING;
            break;
    }
}

void setup_gauge_styles() {
    lv_style_init(&style_grey);
    lv_style_set_arc_color(&style_grey, lv_color_hex(0xD9D9D9));

    lv_style_init(&style_orange);
    lv_style_set_arc_color(&style_orange, lv_color_hex(0xFF8800));

    lv_style_init(&style_red);
    lv_style_set_arc_color(&style_red, lv_color_hex(0xFF0000));
}

void setup() {
    Serial.begin(460800);
    trgb.init();
    ui_init();
    setup_gauge_styles();
    delay(500); 
    Serial.println("UI Initialized, starting BLE...");

    BLEDevice::init("ESP32_OBD_Reader");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setActiveScan(true);
    pBLEScan->start(10, false);
}

void loop() {
    if (doConnect) {
        if (connectToELM327() && initializeELM327()) {
            doConnect = false;
        }
    } else if (elmDevice == nullptr) {
        lv_label_set_text(ui_Center_Label, "N/A");
        lv_label_set_text(ui_Secondary_Label, "0%");
        lv_arc_set_value(ui_Main_Gauge, 0);
        lv_arc_set_value(ui_Main_Gauge_Negative, 0);
        lv_arc_set_value(ui_Secondary_Gauge, 0);
    }

    if (deviceConnected) {
        lv_label_set_text(ui_Center_Label, "");
        handleOBDStateMachine();
    }
    
    // Natural LVGL rendering cycle
    lv_timer_handler();
    
    delay(5); // Small delay to let FreeRTOS context-switch cleanly
}