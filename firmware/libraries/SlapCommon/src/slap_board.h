// Pick the IMU library for your Nano 33 BLE Sense revision.
// Rev1: LSM9DS1  -> library "Arduino_LSM9DS1"
// Rev2: BMI270   -> library "Arduino_BMI270_BMM150"
// Both expose the same IMU.readAcceleration / IMU.readGyroscope API.
#pragma once

// Our boards are Rev1 (I2C scan: LSM9DS1 at 0x6B/0x1E, HTS221 0x5F, LPS22HB 0x5C).
// A Rev2 board can override at build time: --build-property "compiler.cpp.extra_flags=-DNANO_REV=2"
#ifndef NANO_REV
#define NANO_REV 1
#endif

#if NANO_REV == 1
  #include <Arduino_LSM9DS1.h>
#else
  #include <Arduino_BMI270_BMM150.h>
#endif

#ifndef SAMPLE_HZ
#define SAMPLE_HZ 100
#endif
