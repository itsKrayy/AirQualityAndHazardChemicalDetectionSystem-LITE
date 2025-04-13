#include <Arduino.h>

#define MQ135_SENSOR_PIN 12

struct Settings {
  bool enableDebugging = false;
  float calibrationValue;
} settings;

class AirQualityReading {
private:
  float airQualReadingsArr[10];
  float airQualValue = 0;
public:
  void addSensorValue(float value) {
    int arraySize = (int)sizeof(airQualReadingsArr) / sizeof(airQualReadingsArr[0]);

    for (int i = 0; i < arraySize - 1; i++) airQualReadingsArr[i] = airQualReadingsArr[i + 1];

    airQualReadingsArr[arraySize - 1] = (float)(value + (settings.calibrationValue));

    /* DEBUGGING */
    if (settings.enableDebugging) {
      Serial.println("Temperature Array Elements: ");
      for (int i = 0; i < arraySize; i++) {
        Serial.printf("Element %d: %.2f\n", i, airQualReadingsArr[i]);
      }
    }

    float arrangedTempReadArr[arraySize];

    memcpy(arrangedTempReadArr, airQualReadingsArr, arraySize * sizeof(float));

    int numofZeros = 0;

    for (int i = 0; i < arraySize - 1; i++) {
      if (arrangedTempReadArr[i] == 0) numofZeros++;
      for (int j = i + 1; j < arraySize; j++) {
        if (arrangedTempReadArr[i] > arrangedTempReadArr[j]) {
          float temp = arrangedTempReadArr[i];
          arrangedTempReadArr[i] = arrangedTempReadArr[j];
          arrangedTempReadArr[j] = temp;
        }
      }
    }

    /* DEBUGGING */
    if (settings.enableDebugging) {
      Serial.println("Arranged Array Elements: ");
      for (int i = 0; i < arraySize; i++) {
        Serial.printf("Element %d: ", i);
        Serial.println(arrangedTempReadArr[i]);
      }
    }

    float medianData;

    if ((arraySize - numofZeros) % 2 == 0) airQualValue = (float)(arrangedTempReadArr[numofZeros + ((arraySize - numofZeros) / 2)] + arrangedTempReadArr[(numofZeros + ((arraySize - numofZeros) / 2) - 1)]) / 2;
    else airQualValue = (float)arrangedTempReadArr[numofZeros + ((arraySize - numofZeros) / 2)];

    /* DEBUGGING */
    if (settings.enableDebugging) {
      Serial.print("Median: ");
      Serial.println(airQualValue);
    }
  }
  float getReading() {
    return airQualValue;
  }
};


AirQualityReading sensor;

void setup() {
  Serial.begin(115200);

  Serial.println("Hello World!");
}

unsigned long long displaySensorValueTimeout = millis() + 1000;
unsigned long long readSensorTimeout = millis() + 500;

void loop() {

  if(millis() > displaySensorValueTimeout) {
    Serial.printf("Sensor Value: %.2f\n", sensor.getReading());

    displaySensorValueTimeout = millis() + 1000;
  }

  if(millis() > readSensorTimeout) {
    sensor.addSensorValue((float)analogRead(MQ135_SENSOR_PIN));

    readSensorTimeout = millis() + 500;
  }

}