#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_log.h>

// —————————————————————————————————————————————————————————————————
//  Глобальные объекты и флаги
// —————————————————————————————————————————————————————————————————
static NimBLECharacteristic* pHrChar;
static NimBLEServer*        pServer    = nullptr;
static NimBLEAdvertising*   pAdv       = nullptr;
static bool                 isPromotion = false;  // если true — реклама всегда включена

// —————————————————————————————————————————————————————————————————
//  Функция обновления параметров (вызывайте её при смене флага)
// —————————————————————————————————————————————————————————————————
void updateParams(bool promotion) {
  isPromotion = promotion;
  
  if (isPromotion) {
    pAdv->start();
  } else {
    if (pServer->getConnectedCount() > 0) {
      pAdv->stop();
    } else {
      pAdv->start();
    }
  }
}

// —————————————————————————————————————————————————————————————————
//  Колбэки сервера для безопасности и рекламы
// —————————————————————————————————————————————————————————————————
class SecurityCallbacks : public NimBLEServerCallbacks {
public:
  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    return passkey;
  }

  void onConfirmPassKey(NimBLEConnInfo &connInfo, uint32_t passkey) override {
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }

  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
  }

  void onConnect(NimBLEServer* server, NimBLEConnInfo &connInfo) override {
    // Если promotion == false — останавливаем рекламу, иначе оставляем включённой
    if (!isPromotion) {
      server->stopAdvertising();
    } else {
    }
  }

  void onDisconnect(NimBLEServer* server, NimBLEConnInfo &connInfo, int reason) override {
    // Если promotion == false — запускаем рекламу, иначе оставляем включённой
    if (!isPromotion) {
      server->startAdvertising();
    }
  }
};

// —————————————————————————————————————————————————————————————————
//  Колбэки характеристики для контроля подписки
// —————————————————————————————————————————————————————————————————
class HrCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
public:
  void onSubscribe(NimBLECharacteristic* pCharacteristic,
                   NimBLEConnInfo &connInfo,
                   uint16_t subValue) override {
    if (subValue != 0 && !connInfo.isEncrypted()) {
      NimBLEDevice::getServer()->disconnect(connInfo);
    }
  }
};

// —————————————————————————————————————————————————————————————————
//  setup() и loop()
// —————————————————————————————————————————————————————————————————
void setup() {

  // Отключаем Wi-Fi/Netif
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_deinit();

  // Энергосбережение
  setCpuFrequencyMhz(80);
  esp_log_level_set("*", ESP_LOG_NONE);

  // Инициализация BLE
  NimBLEDevice::init("HeartRateSensor");

  // Настройка безопасности: bonding + MITM (PIN) + Secure Connections
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(123456);

  // Создаём сервер и колбэки
  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new SecurityCallbacks());
  pServer->advertiseOnDisconnect(false);  // рекл. контроль вручную

  // Создаём Heart Rate Service
  NimBLEService* pService = pServer->createService("180D");

  // Создаём характеристику HR Measurement
  pHrChar = pService->createCharacteristic(
    "2A37",
    NIMBLE_PROPERTY::NOTIFY    |
    NIMBLE_PROPERTY::READ_ENC  |
    NIMBLE_PROPERTY::WRITE_ENC
  );
  pHrChar->setCallbacks(new HrCharacteristicCallbacks());

  pService->start();

  // Настраиваем рекламу
  pAdv = pServer->getAdvertising();
  pAdv->setAppearance(0x0341);
  pAdv->addServiceUUID("180D");
  pAdv->setName("HeartRateSensor");
  pAdv->setAdvertisingInterval(1600);  // ≈1 секунда
  pAdv->start();

  // Изначально promotion выключён
  isPromotion = false;
}

void loop() {
  static uint8_t bpm = 70;
  uint8_t hrData[2] = { 0x00, bpm++ };
  if (bpm > 100) bpm = 70;

  // Отправка уведомлений только при наличии подключённых клиентов
  if (pServer->getConnectedCount() > 0) {
    pHrChar->setValue(hrData, sizeof(hrData));
    pHrChar->notify();
  }

  // Здесь можно проверять внешние события и вызывать updateParams(),
  // например, по Serial-команде:
  // if (Serial.available()) {
  //   char c = Serial.read();
  //   if (c == '1') updateParams(true);
  //   if (c == '0') updateParams(false);
  // }

  delay(1000);
}
