#include <Arduino.h>
#include "NimBLEDevice.h"
#include <Wire.h>
#include <ELMduino.h>
#include <TRGBSuppport.h>

TRGBSuppport trgb;


#define LED 2
ELM327 myELM327;

// put function declarations here:
int myFunction(int, int);

void setup()
{
Serial.begin(115200);

NimBLEDevice::init("");

NimBLEScan *pScan = NimBLEDevice::getScan();
NimBLEScanResults results = pScan->start(10);

NimBLEUUID serviceUuid("VEEPEAK");

for(int i =0; i< results.getCount(); i++){
  NimBLEAdvertisedDevice device = results.getDevice(i);
  Serial.println(i);
  if (device.isAdvertisingService(serviceUuid)){
    NimBLEClient *pClient = NimBLEDevice::createClient();

    if(pClient->connect(&device)) {
    //success
    } else {
    // failed to connect
    }
    }
  }
}

void loop() {
  // put your main code here, to run repeatedly:
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}