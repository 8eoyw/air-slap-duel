// Attacker node: IMU -> threshold trigger -> TFLite Micro inference -> BLE notify.
// The defender sketch is the same file with ROLE = ROLE_DEFENDER and its own model.h.
// Copy firmware/common/*.h and the exported ml/out/model.h into this folder.
#include <ArduinoBLE.h>
#include "board.h"
#include "slap_protocol.h"
// #include "model.h"   // TODO: exported by ml/export.py

const SlapRole ROLE = ROLE_ATTACKER;
const char*    NAME = SLAP_NAME_ATTACKER;

const float    ACC_TRIGGER_G = 2.5f;   // |a| threshold that opens a window
const uint8_t  CONF_MIN      = 180;    // below this -> treat as background
const uint32_t COOLDOWN_MS   = 400;

BLEService slapService(SLAP_SERVICE_UUID);
BLECharacteristic eventChar(SLAP_EVENT_UUID, BLENotify, sizeof(SlapEvent));
BLECharacteristic syncChar(SLAP_SYNC_UUID, BLEWriteWithoutResponse, sizeof(SlapSync));
BLEByteCharacteristic roleChar(SLAP_ROLE_UUID, BLERead);

int32_t clockOffset = 0;  // referee_ms - millis(), median of recent beacons

void onSync(BLEDevice, BLECharacteristic c) {
  SlapSync s;
  c.readValue(&s, sizeof(s));
  // TODO: keep last 8 offsets and take the median to filter BLE jitter
  clockOffset = (int32_t)(s.referee_ms - millis());
}

void setup() {
  Serial.begin(115200);
  if (!IMU.begin() || !BLE.begin()) while (true) {}

  BLE.setLocalName(NAME);
  BLE.setAdvertisedService(slapService);
  slapService.addCharacteristic(eventChar);
  slapService.addCharacteristic(syncChar);
  slapService.addCharacteristic(roleChar);
  BLE.addService(slapService);
  roleChar.writeValue((uint8_t)ROLE);
  syncChar.setEventHandler(BLEWritten, onSync);
  BLE.advertise();

  // TODO: set up TFLite Micro interpreter and tensor arena
}

void sendEvent(uint32_t localOnset, uint8_t cls, uint8_t conf, uint16_t peak) {
  SlapEvent e{localOnset + clockOffset, cls, conf, peak};
  eventChar.writeValue(&e, sizeof(e));
  Serial.print("evt cls="); Serial.print(cls);
  Serial.print(" conf="); Serial.print(conf);
  Serial.print(" peak="); Serial.println(peak);
}

void loop() {
  BLE.poll();
  // TODO:
  // 1. sample IMU at SAMPLE_HZ into a ring buffer
  // 2. if |a| > ACC_TRIGGER_G and not in cooldown: mark onset, collect 0.5 s more
  // 3. normalize window, run inference, time it with micros() for experiment 1
  // 4. if top class != background and conf >= CONF_MIN: sendEvent(...)
}
