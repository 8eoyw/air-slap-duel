// IMU data collection: streams "t_ms,ax,ay,az,gx,gy,gz" over serial at SAMPLE_HZ.
// Driven by tools/collect_session.py (or send 'r' to start a take, 's' to stop).
//
// arduino-cli compile -b arduino:mbed_nano:nano33ble --library ../libraries/SlapCommon .
#include <slap_board.h>

const unsigned long PERIOD_US = 1000000UL / SAMPLE_HZ;
bool recording = false;
unsigned long nextUs = 0;
float last[6] = {0, 0, 1, 0, 0, 0};

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  pinMode(LED_BUILTIN, OUTPUT);
  if (!IMU.begin()) {
    Serial.println("# IMU init failed");
    while (true) {}
  }
  Serial.print("# ready rev="); Serial.print(NANO_REV);
  Serial.print(" hz="); Serial.println(SAMPLE_HZ);
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') { recording = true;  nextUs = micros(); digitalWrite(LED_BUILTIN, HIGH); Serial.println("# start"); }
    if (c == 's') { recording = false; digitalWrite(LED_BUILTIN, LOW); Serial.println("# stop"); }
  }
  if (!recording) return;

  unsigned long now = micros();
  if ((long)(now - nextUs) < 0) return;
  nextUs += PERIOD_US;

  // Hold the previous value if the IMU has no new sample, so rows stay at a fixed rate.
  if (IMU.accelerationAvailable()) IMU.readAcceleration(last[0], last[1], last[2]);  // g
  if (IMU.gyroscopeAvailable()) IMU.readGyroscope(last[3], last[4], last[5]);        // dps
  Serial.print(millis());
  for (int i = 0; i < 6; i++) {
    Serial.print(',');
    Serial.print(last[i], i < 3 ? 3 : 1);
  }
  Serial.println();
}
