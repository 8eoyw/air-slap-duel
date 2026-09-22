// IMU data collection: streams "t_ms,ax,ay,az,gx,gy,gz" over serial at SAMPLE_HZ.
// Record with tools/serial_to_csv.py. Send 'r' to start a take, 's' to stop.
// Copy firmware/common/*.h into this sketch folder before compiling (Arduino IDE
// only sees files inside the sketch directory).
#include "board.h"

const unsigned long PERIOD_US = 1000000UL / SAMPLE_HZ;
bool recording = false;
unsigned long nextUs = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  if (!IMU.begin()) {
    Serial.println("# IMU init failed");
    while (true) {}
  }
  Serial.println("# ready: send r to record, s to stop");
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'r') { recording = true;  Serial.println("# start"); }
    if (c == 's') { recording = false; Serial.println("# stop"); }
  }
  if (!recording) return;

  unsigned long now = micros();
  if ((long)(now - nextUs) < 0) return;
  nextUs = now + PERIOD_US;

  float ax, ay, az, gx, gy, gz;
  if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable()) {
    IMU.readAcceleration(ax, ay, az);  // g
    IMU.readGyroscope(gx, gy, gz);     // dps
    Serial.print(millis()); Serial.print(',');
    Serial.print(ax, 3); Serial.print(','); Serial.print(ay, 3); Serial.print(',');
    Serial.print(az, 3); Serial.print(','); Serial.print(gx, 1); Serial.print(',');
    Serial.print(gy, 1); Serial.print(','); Serial.println(gz, 1);
  }
}
