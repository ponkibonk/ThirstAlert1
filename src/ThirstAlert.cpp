
#include "Particle.h"
#include <stdlib.h>
#include <stdio.h>

// Standard library headers provide strtol() and snprintf().

// Connect to Particle Cloud automatically and run cloud/network tasks
// alongside this application.
SYSTEM_MODE(AUTOMATIC);
SYSTEM_THREAD(ENABLED);

// Send informational messages to the device log, viewable over USB.
SerialLogHandler logHandler(LOG_LEVEL_INFO);

// Sensor input and calibration points for this particular moisture sensor.
// THESE VALUES WILL BE CHANGED DEPENDING ON KASHVI'S SENSOR CALIBRATION FINDINGS.
const int moisturePin = A0;
const int dryReading = 500;
const int wetReading = 2600;
const int superwetReading = 3500;

// The moisture target can be changed through the Particle Cloud function.
int targetMoisturePercent = 60;

// Read the sensor once per second and publish a cloud event every 10 seconds.
const unsigned long readIntervalMs = 1000;
const unsigned long cloudPublishIntervalMs = 10000;
unsigned long lastReadTime = 0;
unsigned long lastCloudPublishTime = 0;

// These values are registered as Particle Cloud variables for remote lookup.
int currentMoisturePercent = 0;
int currentRawReading = 0;

// Turn a raw analog reading into a human-readable moisture category.
const char* moistureLevel(int reading) {
  if (reading <= dryReading) return "DRY";
  if (reading <= wetReading) return "WET";
  return "SUPER WET";
}

// Scale the sensor's calibrated dry-to-super-wet range to 0-100%.
// Clamp values outside the calibration range so the result stays in range.
int moisturePercent(int reading) {
  long percent = static_cast<long>(reading - dryReading) * 100L /
      (superwetReading - dryReading);
  if (percent < 0) return 0;
  if (percent > 100) return 100;
  return static_cast<int>(percent);
}

// Accept only valid percentages and update the target used by the main loop.
bool setTargetMoisturePercent(int percent) {
  if (percent < 0 || percent > 100) {
    return false;
  }

  targetMoisturePercent = percent;
  Log.info("Target moisture set to %d%%", targetMoisturePercent);
  return true;
}

// Particle calls this function when a cloud request changes the moisture target.
// Parse the request as an integer, reject invalid input, and return the new value.
int setMoistureTargetFunction(String command) {
  const char* input = command.c_str();
  char* end = nullptr;
  long requestedPercent = strtol(input, &end, 10);
  if (end == input || *end != '\0' || requestedPercent < 0 || requestedPercent > 100) {
    Log.warn("Rejected moisture target; enter an integer from 0 to 100");
    return -1;
  }

  if (!setTargetMoisturePercent(static_cast<int>(requestedPercent))) {
    return -1;
  }
  return static_cast<int>(requestedPercent);
}

// Configure the sensor pin and make the target function and latest readings
// accessible through Particle Cloud.
void setup() {
  pinMode(moisturePin, INPUT);
  if (!Particle.function("moistTarget", setMoistureTargetFunction)) {
    Log.error("Failed to register moistTarget Particle function");
  }
  if (!Particle.variable("moisture", currentMoisturePercent)) {
    Log.error("Failed to register moisture Particle variable");
  }
  if (!Particle.variable("rawReading", currentRawReading)) {
    Log.error("Failed to register rawReading Particle variable");
  }
}

// Repeatedly sample the sensor, report its status locally, and send periodic
// readings to Particle Cloud.
void loop() {
  // Use elapsed time instead of delay() so the device can keep servicing
  // Particle Cloud tasks between sensor readings.
  if (millis() - lastReadTime >= readIntervalMs) {
    lastReadTime = millis();
    int reading = analogRead(moisturePin);

    // Update the cloud variables and determine whether the target is met.
    currentRawReading = reading;
    currentMoisturePercent = moisturePercent(reading);
    const char* targetStatus = currentMoisturePercent >= targetMoisturePercent
        ? "TARGET MET"
        : "BELOW TARGET";
    Log.info("Moisture: %d%% (raw %d, %s), target: %d%% (%s)",
        currentMoisturePercent, reading, moistureLevel(reading),
        targetMoisturePercent, targetStatus);

    // Publish a private JSON event periodically rather than on every
    // one-second sensor read.
    if (lastCloudPublishTime == 0 ||
        millis() - lastCloudPublishTime >= cloudPublishIntervalMs) {
      char payload[128];
      int payloadLength = snprintf(payload, sizeof(payload),
          "{\"percent\":%d,\"raw\":%d,\"level\":\"%s\",\"target\":%d,\"status\":\"%s\"}",
          currentMoisturePercent, currentRawReading, moistureLevel(reading),
          targetMoisturePercent, targetStatus);

      // Check that the JSON fits in the buffer before sending it.
      if (payloadLength < 0 ||
          static_cast<size_t>(payloadLength) >= sizeof(payload)) {
        Log.error("Failed to format moisture cloud event");
      } else if (!Particle.publish("moisture", payload, PRIVATE)) {
        Log.warn("Failed to publish moisture reading to Particle Cloud");
      }
      // Start the next publishing interval, even if this publish attempt failed.
      lastCloudPublishTime = millis();
    }
  }
}
