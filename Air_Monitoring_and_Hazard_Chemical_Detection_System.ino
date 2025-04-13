#include <Arduino.h>
#include <U8x8lib.h>

#define MQ135_SENSOR_PIN 12

#define buzzer

struct Settings {
  bool enableDebugging = false;
  bool enableBuzzer = true;
  float calibrationValue = 0;
  float airClassificationWarningValStart = 22.0;
  float airClassificationCriticalValStart = 30.0;
  unsigned int calibrationTime = 0;
} settings;

enum AirQualityClassification {
  NORMAL,
  WARNING,
  CRITICAL
};

class AirQualityReading {
private:
  float airQualReadingsArr[10];
  float airQualValue = 0;
  AirQualityClassification status = NORMAL;

  uint8_t setIAQClassification(float value) {
    if (value >= settings.airClassificationWarningValStart && value < settings.airClassificationCriticalValStart)
      status = WARNING;
    else if (value >= settings.airClassificationCriticalValStart)
      status = CRITICAL;
    else status = NORMAL;

    return status;
  }
public:
  void addIAQValue(float value) {
    int arraySize = (int)sizeof(airQualReadingsArr) / sizeof(airQualReadingsArr[0]);

    for (int i = 0; i < arraySize - 1; i++) airQualReadingsArr[i] = airQualReadingsArr[i + 1];

    if (value <= 0) airQualReadingsArr[arraySize - 1] = 0;
    else airQualReadingsArr[arraySize - 1] = (float)(value + (settings.calibrationValue));

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

    if ((arraySize - numofZeros) % 2 == 0) airQualValue = (float)(arrangedTempReadArr[numofZeros + ((arraySize - numofZeros) / 2)] + arrangedTempReadArr[(numofZeros + ((arraySize - numofZeros) / 2) - 1)]) / 2;
    else airQualValue = (float)arrangedTempReadArr[numofZeros + ((arraySize - numofZeros) / 2)];

    setIAQClassification(airQualValue);

    /* DEBUGGING */
    if (settings.enableDebugging) {
      Serial.print("Median: ");
      Serial.println(airQualValue);
    }
  }

  float getIAQReading() {
    return airQualValue;
  }

  uint8_t getStatus() {
    return status;
  }
};

U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);

AirQualityReading sensor;

void setup() {
  Serial.begin(115200);

  pinMode(18, OUTPUT);

  u8x8.begin();
  u8x8.setPowerSave(0);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  Serial.println("Hello World!");

  // digitalWrite(18, HIGH);
}

unsigned long long displaySensorValueTimeout = millis() + 500;
unsigned long long readSensorTimeout = millis() + 250;
unsigned long long refreshOLEDDisplay = millis() + 15000;

unsigned long long flashingTimeout = millis() + 1000;


unsigned long long beepTimeout = 0;
unsigned int beepInterval = 100;
bool beep = false;

void loop() {

  if (millis() > displaySensorValueTimeout) {
    char displayBuf[16];
    char statusBuf[10];

    if (sensor.getStatus() == NORMAL) strncpy(statusBuf, "NORMAL", 10);
    if (sensor.getStatus() == WARNING) strncpy(statusBuf, "WARNING", 10);
    else if (sensor.getStatus() == CRITICAL) strncpy(statusBuf, "CRITICAL", 10);

    if (millis() > refreshOLEDDisplay) {
      u8x8.clearLine(1);
      u8x8.clearLine(2);
      u8x8.clearLine(7);

      refreshOLEDDisplay = millis() + 15000;
    }

    float sensorValue = sensor.getIAQReading();

    Serial.printf("[Sensor] IAQ Value: %.2f ppm\n", sensorValue);

    sprintf(displayBuf, "%.1f", sensorValue);

    u8x8.draw2x2String((unsigned int)(6 - (unsigned int)((strlen(displayBuf) / 2))), 1, displayBuf);

    // u8x8.setInverseFont(1);
    u8x8.drawString(7, 3, "ppm");

    memset(displayBuf, 0, 16);

    // sprintf(displayBuf, "Status: %s", statusBuf);

    // if (beep) digitalWrite(18, HIGH);
    // else digitalWrite(18, LOW);

    u8x8.drawString(0, 6, "Status: ");
    u8x8.drawString(0, 7, statusBuf);

    u8x8.setInverseFont(0);


    displaySensorValueTimeout = millis() + 250;
  }

  if (millis() > readSensorTimeout) {
    float sensorValue = (float)analogRead(MQ135_SENSOR_PIN);

    sensor.addIAQValue(sensorValue);
    // if (sensor.getStatus() != NORMAL) isStatusNotNormal = true;
    // else isStatusNotNormal = false;

    readSensorTimeout = millis() + 500;
  }

  if (sensor.getStatus() != NORMAL) {
    if (millis() - beepTimeout >= beepInterval) {
      beepTimeout = millis();
      beep = !beep;
      digitalWrite(18, beep);
    }
  } else beep = true;
}