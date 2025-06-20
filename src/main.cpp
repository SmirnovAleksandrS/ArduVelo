#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_log.h>

// Глобальный указатель на характеристику Heart Rate Measurement
static NimBLECharacteristic* pHrChar;

// Колбэки сервера для управления безопасностью и рекламой
class SecurityCallbacks : public NimBLEServerCallbacks {
public:
  // Показываем PIN на Serial для ввода на клиенте
  uint32_t onPassKeyDisplay() override {
    uint32_t passkey = NimBLEDevice::getSecurityPasskey();
    Serial.printf(">>> Passkey: %06u\n", passkey);
    return passkey;
  }
  // Клиент ввёл PIN — подтверждаем
  void onConfirmPassKey(NimBLEConnInfo &connInfo, uint32_t passkey) override {
    Serial.printf(">>> Client entered PIN %06u – accepting\n", passkey);
    NimBLEDevice::injectConfirmPasskey(connInfo, true);
  }
  // После окончания процедуры аутентификации
  void onAuthenticationComplete(NimBLEConnInfo &connInfo) override {
    Serial.println(">>> Pairing & encryption complete");
  }
  // Остановка рекламы при подключении клиента
  void onConnect(NimBLEServer* pServer, NimBLEConnInfo &connInfo) override {
    Serial.println("Client connected → stopping advertising");
    pServer->stopAdvertising();
  }
  // Возобновление рекламы при отключении клиента
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo &connInfo, int reason) override {
    Serial.println("Client disconnected → resuming advertising");
    pServer->startAdvertising();
  }
};

// Колбэки характеристики для контроля подписки
class HrCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
public:
  // Вызывается при попытке (отписке/подписке) клиента
  void onSubscribe(NimBLECharacteristic* pCharacteristic,
                   NimBLEConnInfo &connInfo,
                   uint16_t subValue) override {
    Serial.printf("Subscription change: 0x%04X\n", subValue);
    // Если клиент пытается подписаться и связь не зашифрована — разрываем
    if (subValue != 0 && !connInfo.isEncrypted()) {
      Serial.println("Subscription on unencrypted link → disconnecting client");
      NimBLEDevice::getServer()->disconnect(connInfo);
    }
  }
};

void setup() {
  Serial.begin(115200);

  // Отключаем Wi-Fi для освобождения радиомодуля
  esp_wifi_stop();
  esp_wifi_deinit();
  esp_netif_deinit();

  // Энергосбережение
  setCpuFrequencyMhz(80);
  esp_log_level_set("*", ESP_LOG_NONE);

  // Инициализация BLE
  NimBLEDevice::init("HeartRateSensor");

  // Включаем bonding + MITM(PIN) + Secure Connections
  NimBLEDevice::setSecurityAuth(/*bonding*/true, /*MITM*/true, /*SC*/true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(123456);

  // Создаём сервер и назначаем ему колбэки безопасности
  NimBLEServer* pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new SecurityCallbacks());
  // Отключаем автоматическую рекламу при дисконнекте,
  // т.к. мы вручную перезапускаем рекламу в onDisconnect
  pServer->advertiseOnDisconnect(false);

  // Создаём сервис Heart Rate (UUID 0x180D)
  NimBLEService* pService = pServer->createService("180D");

  // Создаём характеристику Heart Rate Measurement (0x2A37),
  // требующую шифрования для чтения/записи и поддерживающую уведомления
  pHrChar = pService->createCharacteristic(
    "2A37",
    NIMBLE_PROPERTY::NOTIFY    |
    NIMBLE_PROPERTY::READ_ENC  |
    NIMBLE_PROPERTY::WRITE_ENC
  );
  // Назначаем колбэки характеристики для защиты подписки
  pHrChar->setCallbacks(new HrCharacteristicCallbacks());

  // Запускаем сервис
  pService->start();

  // Настраиваем и запускаем рекламу
  NimBLEAdvertising* pAdv = pServer->getAdvertising();
  pAdv->setAppearance(0x0341);         // Heart Rate Sensor
  pAdv->addServiceUUID("180D");
  pAdv->setName("HeartRateSensor");
  pAdv->setAdvertisingInterval(1600);  // 1600 * 0.625 мс = 1 секунда
  pAdv->start();
}

void loop() {
  static uint8_t bpm = 70;
  uint8_t hrData[2] = { 0x00, bpm++ };  // флаг 0x00 + значение bpm
  if (bpm > 100) bpm = 70;

  // Отправляем уведомление только при наличии подключённого клиента
  if (NimBLEDevice::getServer()->getConnectedCount() > 0) {
    pHrChar->setValue(hrData, sizeof(hrData));
    pHrChar->notify();
  }
  delay(1000);
}
