#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_log.h>

NimBLECharacteristic* pHrChar;

void setup() {
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_deinit();

  setCpuFrequencyMhz(80);
  esp_log_level_set("*", ESP_LOG_NONE);

  NimBLEDevice::init("HeartRateSensor");

  NimBLEServer* pServer = NimBLEDevice::createServer();
  NimBLEService* pService = pServer->createService("180D");

  pHrChar = pService->createCharacteristic("2A37", NIMBLE_PROPERTY::NOTIFY);
  NimBLEDescriptor* pCCCD = new NimBLEDescriptor("2902",
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE, 2, pHrChar);
  pHrChar->addDescriptor(pCCCD);

  pService->start();

  NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->setAppearance(0x0341);
  pAdvertising->addServiceUUID("180D");
  pAdvertising->setName("HeartRateSensor");

  // правильный метод
  pAdvertising->setAdvertisingInterval(1600); // интервал в единицах 0.625 мс (1600 = 1 секунда)

  pAdvertising->start();
}

void loop() {
  static uint8_t bpm = 70;
  uint8_t hrData[2] = {0x00, bpm++};
  if (bpm > 100) bpm = 70;

  pHrChar->setValue(hrData, sizeof(hrData));
  pHrChar->notify();

  delay(1000); // реже -> меньше нагрузка
}
