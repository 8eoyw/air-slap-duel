// Fallback referee hub (docs/risks.md, row 1): a third Nano 33 BLE acts as the
// BLE central for both wearables, runs the referee core, and prints game events
// as JSON lines on USB Serial and on Serial1 (TX pin D1) for the XiaoZhi board
// to render (face, TTS, servo). XiaoZhi then only needs a UART reader.
//
// Commands accepted on either serial port (one per line):
//   start | announce_done | swap_done | reset
//
// arduino-cli compile -b arduino:mbed_nano:nano33ble --library ../libraries/SlapCommon .
#include <ArduinoBLE.h>
#include <referee_core.h>
#include <slap_protocol.h>

struct Peer {
  const char* name;
  BLEDevice dev;
  BLECharacteristic event, sync;
  bool ready = false;
};
Peer peers[2] = {{SLAP_NAME_ATTACKER}, {SLAP_NAME_DEFENDER}};

// ---- JSON output --------------------------------------------------------
void emit(const String& line) {
  Serial.println(line);
  Serial1.println(line);
}

struct JsonOut : RefereeOutput {
  void onState(RefState s) override {
    emit(String("{\"type\":\"state\",\"state\":\"") + refStateName(s) + "\"}");
  }
  void onAnnounce(uint8_t attacker, uint8_t round) override {
    emit(String("{\"type\":\"announce\",\"attacker\":") + attacker + ",\"round\":" + round + "}");
  }
  void onResult(const RoundResult& r) override {
    static const char* str[] = {"light", "medium", "heavy"};
    emit(String("{\"type\":\"result\",\"outcome\":\"") + refOutcomeName(r.outcome) +
         "\",\"strength\":\"" + str[r.strength] + "\",\"attack\":\"" + slapClassName(r.attack_cls) +
         "\",\"defense\":\"" + slapClassName(r.defense_cls) + "\",\"delta_ms\":" + r.delta_ms +
         ",\"damage\":" + r.damage + ",\"attacker\":" + r.attacker + ",\"hp\":[" + r.hp[0] + "," +
         r.hp[1] + "]}");
  }
  void onSwap(uint8_t next) override {
    emit(String("{\"type\":\"swap\",\"attacker\":") + next + "}");
  }
  void onGameOver(uint8_t winner, const int16_t hp[2]) override {
    emit(String("{\"type\":\"game_over\",\"winner\":") + winner + ",\"hp\":[" + hp[0] + "," + hp[1] + "]}");
  }
} out;
RefereeCore ref(&out);

// ---- BLE central --------------------------------------------------------
bool setupPeer(Peer& p, BLEDevice d) {
  if (!d.connect()) return false;
  if (!d.discoverService(SLAP_SERVICE_UUID)) { d.disconnect(); return false; }
  p.event = d.characteristic(SLAP_EVENT_UUID);
  p.sync = d.characteristic(SLAP_SYNC_UUID);
  if (!p.event || !p.sync || !p.event.subscribe()) { d.disconnect(); return false; }
  p.dev = d;
  p.ready = true;
  emit(String("{\"type\":\"peer\",\"name\":\"") + p.name + "\",\"connected\":true}");
  return true;
}

void scanForMissing() {
  static uint32_t lastScan = 0;
  bool missing = !peers[0].ready || !peers[1].ready;
  if (!missing || millis() - lastScan < 500) return;
  lastScan = millis();
  BLE.scanForUuid(SLAP_SERVICE_UUID);
  uint32_t t0 = millis();
  while (millis() - t0 < 300) {
    BLEDevice d = BLE.available();
    if (!d) continue;
    for (Peer& p : peers) {
      if (!p.ready && d.localName() == p.name) {
        BLE.stopScan();
        setupPeer(p, d);
        return;
      }
    }
  }
  BLE.stopScan();
}

void pollPeers(uint32_t now) {
  for (int i = 0; i < 2; i++) {
    Peer& p = peers[i];
    if (!p.ready) continue;
    if (!p.dev.connected()) {
      p.ready = false;
      emit(String("{\"type\":\"peer\",\"name\":\"") + p.name + "\",\"connected\":false}");
      continue;
    }
    if (p.event.valueUpdated()) {
      SlapEvent e;
      p.event.readValue(&e, sizeof(e));
      emit(String("{\"type\":\"event\",\"from\":\"") + p.name + "\",\"cls\":\"" + slapClassName(e.cls) +
           "\",\"t\":" + e.t_onset + ",\"rx\":" + now + ",\"conf\":" + e.conf + ",\"peak\":" + e.peak + "}");
      if (i == 0) ref.onAttackerEvent(e, now);
      else        ref.onDefenderEvent(e, now);
    }
  }
}

void sendSync(uint32_t now) {
  static uint32_t last = 0;
  if (now - last < SYNC_PERIOD_MS) return;
  last = now;
  SlapSync s{now};
  for (Peer& p : peers)
    if (p.ready) p.sync.writeValue(&s, sizeof(s), false);
}

// ---- commands -----------------------------------------------------------
void handleCommand(String cmd, uint32_t now) {
  cmd.trim();
  if (cmd == "start") ref.start(now);
  else if (cmd == "announce_done") ref.announceDone(now);
  else if (cmd == "swap_done") ref.swapDone(now);
  else if (cmd == "reset") ref.reset();
}

void readCommands(Stream& s, String& buf, uint32_t now) {
  while (s.available()) {
    char c = s.read();
    if (c == '\n') { handleCommand(buf, now); buf = ""; }
    else if (buf.length() < 32) buf += c;
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  uint32_t t = millis();
  while (!Serial && millis() - t < 2000) {}
  if (!BLE.begin()) { emit("{\"type\":\"error\",\"what\":\"ble_init\"}"); while (true) {} }
  emit("{\"type\":\"hello\",\"role\":\"hub\"}");
}

void loop() {
  static String b0, b1;
  uint32_t now = millis();
  BLE.poll();
  scanForMissing();
  pollPeers(now);
  sendSync(now);
  readCommands(Serial, b0, now);
  readCommands(Serial1, b1, now);
  ref.tick(now);
}
