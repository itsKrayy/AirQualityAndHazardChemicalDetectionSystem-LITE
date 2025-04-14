#include <Arduino.h>
#include <U8x8lib.h>
#include <EEPROM.h>
#include <MQ135.h>

#define EEPROM_SIZE 512

#define MQ135_SENSOR_PIN 12

#define RZERO 76.63

#define buzzer

struct Settings {
  bool enableDebugging;
  bool enableBuzzer;
  float calibrationValue = 0;
  float airClassificationWarningValStart;
  float airClassificationCriticalValStart;
  unsigned int calibrationTime;
} settings;

enum AirQualityClassification {
  NORMAL,
  WARNING,
  CRITICAL
};

class AirQualityReading {
private:
  float RL = 10.0;
  float R0 = 9.83;
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

    // float voltage = value * (3.3 / 4095.0);  
    // float RS = ((3.3 - voltage) * RL) / voltage;

    // float ratio = RS / R0;

    // float ppm = pow(10, -2.769 * log10(ratio) + 2.091);
    
    // Serial.printf("Voltage: %.2f\nRS: %.2f\nRatio: %.2f\nPPM: %.2f\n", voltage, RS, ratio, ppm);

    // if (voltage <= 0) airQualReadingsArr[arraySize - 1] = 0;
    // else airQualReadingsArr[arraySize - 1] = (float)(voltage + (settings.calibrationValue));

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

void showConfig();
void loadConfig();
void saveConfig();
void factoryResetConfig();

U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);

AirQualityReading sensor;

MQ135 gasSensor = MQ135(MQ135_SENSOR_PIN);

void setup() {
  Serial.begin(115200);

  EEPROM.begin(EEPROM_SIZE);

  loadConfig();

  showConfig();

  delay(3000);

  pinMode(18, OUTPUT);

  u8x8.begin();
  u8x8.setPowerSave(0);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  Serial.println("Hello World!");
}

unsigned long long displaySensorValueTimeout = millis() + 500;
unsigned long long readSensorTimeout = millis() + 250;
unsigned long long refreshOLEDDisplay = millis() + 15000;

unsigned long long flashingTimeout = millis() + 1000;

unsigned long long beepTimeout = 0;
unsigned int beepInterval = 100;
bool beep = false;

float currentIAQValue = 0;
uint8_t currentIAQClassification =  NORMAL;

void loop() {

  if (millis() > displaySensorValueTimeout) {
    char displayBuf[16];
    char statusBuf[10];

    if (sensor.getStatus() == NORMAL) strncpy(statusBuf, "NORMAL", 10);
    if (sensor.getStatus() == WARNING) strncpy(statusBuf, "WARNING", 10);
    else if (sensor.getStatus() == CRITICAL) strncpy(statusBuf, "CRITICAL", 10);

    // if (millis() > refreshOLEDDisplay) {
    //   u8x8.clearLine(1);
    //   u8x8.clearLine(2);
    //   u8x8.clearLine(7);

    //   refreshOLEDDisplay = millis() + 15000;
    // }

    float sensorValue = sensor.getIAQReading();

    Serial.printf("[Sensor] IAQ Value: %.2f ppm\n", sensorValue);

    sprintf(displayBuf, "%.1f", sensorValue);

    if(sensorValue != currentIAQValue) {
      u8x8.clearLine(1);
      u8x8.clearLine(2);

      currentIAQValue = sensorValue;
    }

    u8x8.draw2x2String((unsigned int)(6 - (unsigned int)((strlen(displayBuf) / 2))), 1, displayBuf);

    // u8x8.setInverseFont(1);
    u8x8.drawString(7, 3, "ppm");

    memset(displayBuf, 0, 16);

    // sprintf(displayBuf, "Status: %s", statusBuf);

    // if (beep) digitalWrite(18, HIGH);
    // else digitalWrite(18, LOW);

    u8x8.drawString(0, 6, "Status: ");

    if(sensor.getStatus() != currentIAQClassification) {
      u8x8.clearLine(7);
      currentIAQClassification = sensor.getStatus();
    }

    u8x8.drawString(0, 7, statusBuf);

    u8x8.setInverseFont(0);


    displaySensorValueTimeout = millis() + 250;
  }

  if (millis() > readSensorTimeout) {
    // float sensorValue = (float)analogRead(MQ135_SENSOR_PIN);

    Serial.printf("FROM LIBRARY MQ135 Value: %.2f\n", gasSensor.getPPM());

    // sensor.addIAQValue(sensorValue);
    sensor.addIAQValue(gasSensor.getPPM());
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
  } else {
    beep = false;
    digitalWrite(18, LOW);
  }
}

void showConfig() {
  Serial.printf("[Config] Current Configuration: \n \
  Debugging:\t%s\n \
  Buzzer:\t%s\n \
  IAQ Calibration Value:\t%.2f C\n \
  WARNING IAQ Classification Starting Value:\t%.2f C\n \
  CRITICAL IAQ Classification Starting Value:\t%.2f C\n \
  Calibration Time:\t%dms\n",
                settings.enableDebugging ? "ENABLED" : "DISABLED",
                settings.enableBuzzer ? "ENABLED" : "DISABLED",
                settings.calibrationValue,
                settings.airClassificationWarningValStart,
                settings.airClassificationCriticalValStart,
                settings.calibrationTime);
}

void loadConfig() {
  EEPROM.get(0, settings);

  if (isnan(settings.calibrationValue) && isnan(settings.airClassificationWarningValStart) && isnan(settings.airClassificationCriticalValStart)) {
    Serial.println("[Config] Configuration not initialized. Applying default factory configuration..");

    factoryResetConfig();
  }

  return;
}

void saveConfig() {
  EEPROM.put(0, settings);
  EEPROM.commit();

  Serial.println("[Config] Successfully Saved Configuration");

  return;
}

void factoryResetConfig() {
  settings.enableDebugging = false;
  settings.enableBuzzer = false;
  settings.calibrationValue = 0;
  settings.airClassificationWarningValStart = 1000;   // in Celsius
  settings.airClassificationCriticalValStart = 5001;  // in Celsius
  settings.calibrationTime = 60000;                 // in Celsius

  saveConfig();

  return;
}