// Pick the IMU library for your Nano 33 BLE Sense revision.
// Rev1: LSM9DS1  -> library "Arduino_LSM9DS1"
// Rev2: BMI270   -> library "Arduino_BMI270_BMM150"
// Both expose the same IMU.readAcceleration / IMU.readGyroscope API.
#pragma once

#define NANO_REV 2  // TODO: set to 1 or 2 after checking the board silkscreen

#if NANO_REV == 1
  #include <Arduino_LSM9DS1.h>
#else
  #include <Arduino_BMI270_BMM150.h>
#endif

#define SAMPLE_HZ 100
