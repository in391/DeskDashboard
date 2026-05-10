#include "ble_client_manager.h"

#include <NimBLEDevice.h>
#include <strings.h>

namespace BleClientManager {

// UUIDs mapping exactly to the Raspberry Pi BLE Server
static NimBLEUUID serviceUUID("a25659a2-0de7-4f74-a149-94f47b218ba3");
static NimBLEUUID charUUID("a25659a2-0de7-4f74-a149-94f47b218ba4");      // notify
static NimBLEUUID requestCharUUID("a25659a2-0de7-4f74-a149-94f47b218ba5"); // write (client -> server request)

static boolean doConnect = false;
static boolean connected = false;
static boolean doScan = true;

static const char *targetServerName = "RasPi-HomeServer";
// Optional: set this to your Raspberry Pi BLE MAC address to make matching deterministic.
// Keep empty to disable address matching.
static const char *targetServerAddress = "";

static const size_t BLE_JSON_PAYLOAD_MAX = 1024;
static const size_t BLE_RX_QUEUE_DEPTH = 8;
struct BleQueuedPayload {
  char data[BLE_JSON_PAYLOAD_MAX];
  size_t len;
  bool truncated;
};

static BleQueuedPayload bleRxQueue[BLE_RX_QUEUE_DEPTH];
static size_t bleRxHead = 0;
static size_t bleRxTail = 0;
static size_t bleRxCount = 0;
static volatile bool dataReady = false;
static volatile uint32_t bleRxDroppedCount = 0;
static portMUX_TYPE payloadMux = portMUX_INITIALIZER_UNLOCKED;

static NimBLEAddress *pServerAddress = nullptr;
static NimBLERemoteCharacteristic *pRemoteCharacteristic = nullptr;
static NimBLERemoteCharacteristic *pRequestCharacteristic = nullptr;
static NimBLEClient *pActiveClient = nullptr;
static const uint8_t BLE_CONNECT_TIMEOUT_S = 5; // 5 seconds


static void notifyCallback(NimBLERemoteCharacteristic *pBLERemoteCharacteristic,
                           uint8_t *pData, size_t length, bool isNotify) {
  (void)pBLERemoteCharacteristic;
  (void)isNotify;

  const size_t maxCopy = BLE_JSON_PAYLOAD_MAX - 1;
  const size_t copyLen = (length > maxCopy) ? maxCopy : length;

  portENTER_CRITICAL(&payloadMux);
  if (bleRxCount >= BLE_RX_QUEUE_DEPTH) {
    // Drop the oldest payload and keep the newest state update.
    bleRxTail = (bleRxTail + 1) % BLE_RX_QUEUE_DEPTH;
    bleRxCount--;
    bleRxDroppedCount++;
  }

  BleQueuedPayload &slot = bleRxQueue[bleRxHead];
  memcpy(slot.data, pData, copyLen);
  slot.data[copyLen] = '\0';
  slot.len = copyLen;
  slot.truncated = (length > maxCopy);

  bleRxHead = (bleRxHead + 1) % BLE_RX_QUEUE_DEPTH;
  bleRxCount++;
  dataReady = (bleRxCount > 0);
  portEXIT_CRITICAL(&payloadMux);
}

static bool connectToServer() {
  if (pServerAddress == nullptr) {
    Serial.println("No target BLE device selected yet; continue scanning.");
    doScan = true;
    return false;
  }

  Serial.print("Forming a connection to ");
  Serial.println(pServerAddress->toString().c_str());

  if (pActiveClient != nullptr) {
    if (pActiveClient->isConnected()) {
      pActiveClient->disconnect();
    }
    NimBLEDevice::deleteClient(pActiveClient);
    pActiveClient = nullptr;
  }

  NimBLEClient *pClient = NimBLEDevice::createClient();
  pActiveClient = pClient;
  Serial.println(" - Created client");

  pClient->setConnectTimeout(BLE_CONNECT_TIMEOUT_S);
  if (!pClient->connect(*pServerAddress, true)) {
    connected = false;
    doScan = false;
    return false;
  }

  connected = true;
  Serial.println(" - Connected to server");

  NimBLERemoteService *pRemoteService = pClient->getService(serviceUUID);
  if (pRemoteService == nullptr) {
    Serial.print("Failed to find our service UUID: ");
    Serial.println(serviceUUID.toString().c_str());
    pClient->disconnect();
    connected = false;
    doScan = true;
    return false;
  }
  Serial.println(" - Found our service");

  pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
  if (pRemoteCharacteristic == nullptr) {
    Serial.print("Failed to find our characteristic UUID: ");
    Serial.println(charUUID.toString().c_str());
    pClient->disconnect();
    connected = false;
    doScan = true;
    return false;
  }
  Serial.println(" - Found our characteristic");

  // Optional request characteristic (write) — allows client to demand a full data push.
  pRequestCharacteristic = pRemoteService->getCharacteristic(requestCharUUID);
  if (pRequestCharacteristic != nullptr && pRequestCharacteristic->canWrite()) {
    Serial.println(" - Found request characteristic (write)");
  } else {
    pRequestCharacteristic = nullptr;
    Serial.println(" - Request characteristic not found or not writable; server-push only.");
  }

  if (pRemoteCharacteristic->canNotify()) {
    pRemoteCharacteristic->subscribe(true, notifyCallback);
    Serial.println(" - Registered for Push Notifications!");

    NimBLERemoteDescriptor *p2902 =
        pRemoteCharacteristic->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (p2902 != nullptr) {
      uint8_t enableNotify[] = {0x01, 0x00};
      p2902->writeValue(enableNotify, 2, true);
      Serial.println(" - CCCD (0x2902) explicitly enabled!");
    } else {
      Serial.println(" - WARNING: CCCD Descriptor 0x2902 not found! Push drops may occur.");
    }
    
    // After subscription is established, request initial data sync from server.
    if (pRequestCharacteristic != nullptr && !tracker_time && !tracker_schedule && !tracker_weather && !tracker_sensor && !tracker_info) {
      Serial.println(" - Requesting initial data sync from server...");
      requestData();
    }
  } else {
    Serial.println(" - Warning: Characteristic doesn't support notifications!");
  }

  return true;
}

class MyAdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    const bool serviceMatch = advertisedDevice->isAdvertisingService(serviceUUID);

    bool nameMatch = false;
    if (advertisedDevice->haveName()) {
      const std::string advertisedName = advertisedDevice->getName();
      nameMatch = (strcasecmp(advertisedName.c_str(), targetServerName) == 0);
    }

    bool addressMatch = false;
    if (targetServerAddress[0] != '\0') {
      const std::string advertisedAddress =
          advertisedDevice->getAddress().toString();
      addressMatch =
          (strcasecmp(advertisedAddress.c_str(), targetServerAddress) == 0);
    }

    if (serviceMatch || nameMatch || addressMatch) {
      Serial.print("Target BLE server matched: ");
      Serial.println(advertisedDevice->getAddress().toString().c_str());

      NimBLEDevice::getScan()->stop();
      if (pServerAddress != nullptr) {
        delete pServerAddress;
        pServerAddress = nullptr;
      }
      pServerAddress = new NimBLEAddress(advertisedDevice->getAddress());
      doConnect = true;
      doScan = true;
    }
  }
};

void begin() {
  NimBLEDevice::init("");

  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
  pBLEScan->setInterval(160);
  pBLEScan->setWindow(160);  // 100% duty cycle during scan for fastest discovery
  pBLEScan->setActiveScan(true);

  pBLEScan->start(5, nullptr); // non-blocking: scan runs in background, loop() drives connection
}

void loop() {
  if (doConnect) {
    if (connectToServer()) {
      Serial.println("We are now connected to the BLE Server.");
      Serial.println("Connected to RasPi-HomeServer!!");
    } else {
      Serial.println("Failed to connect to the server.");
    }
    doConnect = false;
  }

  if (connected) {
    if (pActiveClient != nullptr && !pActiveClient->isConnected()) {
      Serial.println("BLE link lost. Restarting scan...");
      connected = false;
      doConnect = false;
      doScan = true;
      pRemoteCharacteristic = nullptr;
      pRequestCharacteristic = nullptr;
      return;
    }
  } else if (doScan) {
    NimBLEScan *pBLEScan = NimBLEDevice::getScan();
    if (pBLEScan != nullptr && !pBLEScan->isScanning()) {
      pBLEScan->start(5, nullptr);  // non-blocking: callback controls matching/connect
    }
  }
}

void resumeScan() {
  connected = false;
  doConnect = false;
  doScan = true;

  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
  if (pBLEScan != nullptr && !pBLEScan->isScanning()) {
    pBLEScan->start(5, nullptr);  // kick scan immediately on wake
  }
}

bool isConnected() {
  return connected;
}

bool isScanning() {
  return doScan;
}

bool consumePayload(String &payload) {
  if (!dataReady) {
    return false;
  }

  char localPayload[BLE_JSON_PAYLOAD_MAX] = {0};
  bool wasTruncated = false;
  bool hasPayload = false;
  uint32_t droppedSinceLast = 0;

  portENTER_CRITICAL(&payloadMux);
  if (bleRxCount > 0) {
    BleQueuedPayload &slot = bleRxQueue[bleRxTail];
    memcpy(localPayload, slot.data, slot.len + 1);
    wasTruncated = slot.truncated;
    bleRxTail = (bleRxTail + 1) % BLE_RX_QUEUE_DEPTH;
    bleRxCount--;
    hasPayload = true;
  }
  dataReady = (bleRxCount > 0);
  if (bleRxDroppedCount > 0) {
    droppedSinceLast = bleRxDroppedCount;
    bleRxDroppedCount = 0;
  }
  portEXIT_CRITICAL(&payloadMux);

  if (!hasPayload) {
    return false;
  }

  payload = String(localPayload);
  return true;
}

void disconnect() {
  if (pActiveClient != nullptr && pActiveClient->isConnected()) {
    pActiveClient->disconnect();
  }
  connected = false;
  doConnect = false;
  doScan = false;
  pRemoteCharacteristic = nullptr;
  pRequestCharacteristic = nullptr;
}

void deinit() {
  // Stop any in-progress scan so the BT scanner task is idle before sleep.
  NimBLEScan *pBLEScan = NimBLEDevice::getScan();
  if (pBLEScan != nullptr) {
    pBLEScan->stop();
  }
  // Disconnect from server if connected.
  if (pActiveClient != nullptr && pActiveClient->isConnected()) {
    pActiveClient->disconnect();
  }
  connected = false;
  doConnect = false;
  pRemoteCharacteristic = nullptr;
  pRequestCharacteristic = nullptr;
  // Leave doScan = true so the loop restarts scanning immediately after wake.
  doScan = true;
  // Give the NimBLE host task a moment to process the stop/disconnect
  // before the CPU enters sleep. Do NOT call NimBLEDevice::deinit() —
  // tearing down the NimBLE host FreeRTOS task is crash-prone on ESP32-S3;
  // leaving it idle and paused is safe and sufficient.
  delay(50);
}

bool requestData() {
  if (!connected || pRequestCharacteristic == nullptr) {
    Serial.println("requestData() blocked: not connected or no request characteristic");
    return false;
  }

  const char *req = "{\"cmd\":\"get\"}";
  Serial.print("requestData() sending: ");
  Serial.println(req);
  const bool ok = pRequestCharacteristic->writeValue(
      reinterpret_cast<const uint8_t *>(req), strlen(req), true);

  if (ok) {
    Serial.println("requestData() write succeeded");
  } else {
    Serial.println("requestData() write FAILED");
  }
  return ok;
}

}  // namespace BleClientManager
