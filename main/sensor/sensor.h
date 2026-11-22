#pragma once

// Single entry point for reading the sensor. Only the simulated
// implementation exists today; the physical sensor driver goes here
// once one is chosen.

void sensor_init(void);

// Temperature in °C, or NAN if the read fails.
float sensor_read(void);
