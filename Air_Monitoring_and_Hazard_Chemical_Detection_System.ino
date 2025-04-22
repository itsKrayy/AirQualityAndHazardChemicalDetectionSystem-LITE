#include <Arduino.h>
#include "BluetoothSerial.h"
#include <U8x8lib.h>
#include <EEPROM.h>
#include <MQ135.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define EEPROM_SIZE 512

#define MQ135_SENSOR_PIN 13

#define GREEN_LED_PIN 32
#define YELLOW_LED_PIN 33
#define RED_LED_PIN 25
#define BUZZER_PIN 26

enum AirQualityClassification {
  NORMAL,
  WARNING,
  CRITICAL,
  ERROR,
  CALIBRATING
};

struct Settings {
  bool enableDebugging;
  bool enableBuzzer;
  float calibrationValue = 0;
  float airClassificationWarningValStart;
  float airClassificationCriticalValStart;
  unsigned int calibrationTime;
} settings;

class AirQualityReading {
private:
  float RL = 10.0;
  float R0 = 9.83;
  float airQualReadingsArr[10];
  float airQualValue = 0;
  AirQualityClassification status = NORMAL;

  uint8_t setIAQClassification(float value) {
    if (millis() < settings.calibrationTime) status = CALIBRATING;
    else if (isnan(value)) status = ERROR;
    else if (value >= settings.airClassificationWarningValStart && value < settings.airClassificationCriticalValStart)
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

    if (value == NULL) {
      Serial.printf("[Error] IAQ Value is NULL\n");

      status = ERROR;

      return;
    } else if (value <= 0 || (value - settings.calibrationValue) < 0) airQualReadingsArr[arraySize - 1] = 0.00;
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

BluetoothSerial SerialBT;

U8X8_SH1106_128X64_NONAME_HW_I2C u8x8(/* reset=*/U8X8_PIN_NONE);

AirQualityReading sensor;
MQ135 gasSensor = MQ135(MQ135_SENSOR_PIN);

void setup() {
  Serial.begin(115200);

  pinMode(0, INPUT_PULLUP);

  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(YELLOW_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(GREEN_LED_PIN, HIGH);

  EEPROM.begin(EEPROM_SIZE);

  loadConfig();
  showConfig();

  u8x8.begin();
  u8x8.setPowerSave(0);
  u8x8.setFont(u8x8_font_chroma48medium8_r);

  u8x8.setInverseFont(1);
  u8x8.drawString(0, 2, "Chemical Hazard");
  u8x8.drawString(3, 3, "Detection");
  u8x8.drawString(4, 4, "System");
  u8x8.setInverseFont(0);
  u8x8.drawString(1, 7, "Initializing..");

  intro();

  delay(500);
  u8x8.clear();
  delay(500);

  Serial.println("[System] Air Quality Device Initialized");
}

unsigned long long displaySensorValueTimeout = millis() + 500;
unsigned long long readSensorTimeout = millis() + 250;
unsigned long long refreshOLEDDisplay = millis() + 15000;

unsigned long long flashingTimeout = millis() + 1000;

unsigned long long beepTimeout = 0;
unsigned int beepInterval = 100;
bool beep = false;

float currentIAQValue = 0;
uint8_t currentIAQClassification = NORMAL;

bool configMode = false;

void loop() {

  if (millis() > displaySensorValueTimeout && !configMode) {
    char displayBuf[16];
    char statusBuf[12];

    if (sensor.getStatus() == CALIBRATING) strncpy(statusBuf, "CALIBRATING", 12);
    else if (sensor.getStatus() == NORMAL) strncpy(statusBuf, "NORMAL", 12);
    else if (sensor.getStatus() == WARNING) strncpy(statusBuf, "WARNING", 12);
    else if (sensor.getStatus() == CRITICAL) strncpy(statusBuf, "CRITICAL", 12);

    float sensorValue = sensor.getIAQReading();

    Serial.printf("[Sensor] IAQ Value: %.2f ppm\n", sensorValue);

    sprintf(displayBuf, "%.1f", sensorValue);

    if (sensorValue != currentIAQValue) {
      u8x8.clearLine(1);
      u8x8.clearLine(2);

      currentIAQValue = sensorValue;
    }

    if (millis() < settings.calibrationTime) u8x8.draw2x2String(3, 1, "--.-");
    else if (strlen(displayBuf) > 6) {
      u8x8.drawString(1, 2, displayBuf);
    } else u8x8.draw2x2String((unsigned int)(6 - (unsigned int)((strlen(displayBuf) / 2))), 1, displayBuf);

    u8x8.drawString(7, 3, "ppm");

    memset(displayBuf, 0, 16);

    // sprintf(displayBuf, "Status: %s", statusBuf);

    // if (beep) digitalWrite(18, HIGH);
    // else digitalWrite(18, LOW);

    u8x8.drawString(0, 6, "Status: ");

    if (sensor.getStatus() != currentIAQClassification) {
      u8x8.clearLine(7);
      currentIAQClassification = sensor.getStatus();
    }

    u8x8.setInverseFont(1);
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

    setLEDStatus(sensor.getStatus());

    readSensorTimeout = millis() + 500;
  }

  if (sensor.getStatus() != NORMAL && sensor.getStatus() != CALIBRATING) {
    if (millis() - beepTimeout >= beepInterval) {
      beepTimeout = millis();
      beep = !beep;
      if (settings.enableBuzzer) digitalWrite(BUZZER_PIN, beep);
    }
  } else {
    beep = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  if (configMode) {
    u8x8.clear();

    u8x8.draw2x2String(2, 1, "N/A");
    u8x8.drawString(0, 6, "Status: ");
    u8x8.drawString(0, 7, "CONFIG MODE");
    processCommand();

    configMode = !configMode;
  }

  if (digitalRead(0) == LOW) {

    while (digitalRead(0) == LOW)
      ;

    configMode = true;
  }
}

void setLEDStatus(uint8_t statusValue) {
  switch (statusValue) {
    case NORMAL:
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, LOW);
      // if (settings.enableBuzzer) beep = false;
      break;
    case WARNING:
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, HIGH);
      digitalWrite(RED_LED_PIN, LOW);
      // if (settings.enableBuzzer) beep = true;
      break;
    case CRITICAL:
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, HIGH);
      // if (settings.enableBuzzer) beep = true;
      break;
    case ERROR:
      digitalWrite(GREEN_LED_PIN, LOW);
      digitalWrite(YELLOW_LED_PIN, ERROR);
      digitalWrite(RED_LED_PIN, HIGH);
      break;
    case CALIBRATING:
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(YELLOW_LED_PIN, HIGH);
      digitalWrite(RED_LED_PIN, LOW);
      break;
    default:
      digitalWrite(GREEN_LED_PIN, HIGH);
      digitalWrite(YELLOW_LED_PIN, LOW);
      digitalWrite(RED_LED_PIN, LOW);
      break;
  }

  return;
}

void showConfig() {
  Serial.printf("[Config] Current Configuration: \n \
  Debugging:\t%s\n \
  Buzzer:\t%s\n \
  IAQ Calibration Value:\t%.2f ppm\n \
  WARNING IAQ Classification Starting Value:\t%.2f ppm\n \
  CRITICAL IAQ Classification Starting Value:\t%.2f ppm\n \
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

    if (SerialBT.hasClient()) {
      SerialBT.println("\n[Config] Configuration not initialized. Applying default factory configuration..");
    }

    factoryResetConfig();
  }

  return;
}

void saveConfig() {
  EEPROM.put(0, settings);
  EEPROM.commit();

  Serial.println("[Config] Successfully Saved Configuration");

  if (SerialBT.hasClient()) {
    SerialBT.println("\n[Config] Successfully Saved Configuration");
  }

  return;
}

void factoryResetConfig() {
  Serial.println("[Config] Resetting Configuration to Factory Settings.");

  if (SerialBT.hasClient()) {
    SerialBT.println("\n[Config] Resetting Configuration to Factory Settings.");
  }

  settings.enableDebugging = false;
  settings.enableBuzzer = true;
  settings.calibrationValue = 0;
  settings.airClassificationWarningValStart = 1000;   // in Celsius
  settings.airClassificationCriticalValStart = 5001;  // in Celsius
  settings.calibrationTime = 120000;                   // in Celsius

  saveConfig();

  return;
}

void processCommand() {
  unsigned long long ledAlternate = millis() + 500;
  bool toggleGreenLED = 0;

  if (SerialBT.begin("AirQualityDevice")) {
    Serial.println("Sucessfully initialized Bluetooth");
    Serial.println("Bluetooth Device Name: AirQualityDevice");
  } else {
    Serial.println("[Error] Failed to initialize Bluetooth");

    u8x8.clear();

    return;
  }

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  while (true) {
    char buf[60];
    unsigned long long timeout = millis() + 50;
    unsigned int byteRead = 0;

    if (millis() > ledAlternate) {
      if (toggleGreenLED) {
        digitalWrite(GREEN_LED_PIN, HIGH);
        digitalWrite(YELLOW_LED_PIN, LOW);

        toggleGreenLED = 0;
      } else {
        digitalWrite(GREEN_LED_PIN, LOW);
        digitalWrite(YELLOW_LED_PIN, HIGH);

        toggleGreenLED = 1;
      }

      ledAlternate = millis() + 500;
    }

    if (SerialBT.available()) {
      // Serial.write(SerialBT.read());

      while (SerialBT.available()) {
        if (byteRead < sizeof(buf) - 1) buf[byteRead++] = SerialBT.read();

        timeout = millis() + 100;
      }

      buf[byteRead] = '\0';
    } else if (Serial.available()) {
      while (Serial.available() && millis() < timeout) {
        if (byteRead < sizeof(buf) - 1) buf[byteRead++] = Serial.read();

        timeout = millis() + 100;
      }

      buf[byteRead] = '\0';
    } else continue;

    if (byteRead > 0) {
      char *arguments[10];

      char *token = strtok(buf, " ");
      int numOfArgs = 0;
      while (token != NULL && numOfArgs < 10) {
        arguments[numOfArgs++] = token;
        token = strtok(NULL, " ");
      }

      if (numOfArgs == 0) {
        Serial.println("No command entered.");
        SerialBT.println("No command entered.");
        return;
      }

      if (strcmp(arguments[0], "show") == 0) {

        if (numOfArgs == 2 && strcmp(arguments[1], "config") == 0) {
          showConfig();

          SerialBT.printf("\n[Config] Current Configuration: \n \
          Debugging: %s\n \
          Buzzer: %s\n \
          IAQ Calibration Value: %.2f ppm\n \
          WARNING IAQ Classification Starting Value: %.2f ppm\n \
          CRITICAL IAQ Classification Starting Value: %.2f ppm\n \
          Calibration Time: %dms\n",
                          settings.enableDebugging ? "ENABLED" : "DISABLED",
                          settings.enableBuzzer ? "ENABLED" : "DISABLED",
                          settings.calibrationValue,
                          settings.airClassificationWarningValStart,
                          settings.airClassificationCriticalValStart,
                          settings.calibrationTime);
        } else {
          Serial.printf("Unknown argument to command \"show\".\n");
          SerialBT.printf("Unknown argument to command \"show\".\n");
        }

      } else if (strcmp(arguments[0], "enable") == 0) {
        if (numOfArgs == 2 && strcmp(arguments[1], "buzzer") == 0) {
          Serial.printf("Buzzer: %s -> %s\n", settings.enableBuzzer ? "ENABLED" : "DISABLED", "ENABLED");
          SerialBT.printf("\nBuzzer: %s -> %s\n", settings.enableBuzzer ? "ENABLED" : "DISABLED", "ENABLED");
          settings.enableBuzzer = true;

          saveConfig();
        } else {
          Serial.printf("Unknown argument to command \"enable\".\n");
          SerialBT.printf("Unknown argument to command \"enable\".\n");
        }
      } else if (strcmp(arguments[0], "disable") == 0) {
        if (numOfArgs == 2 && strcmp(arguments[1], "buzzer") == 0) {
          Serial.printf("Buzzer: %s -> %s\n", settings.enableBuzzer ? "ENABLED" : "DISABLED", "ENABLED");
          SerialBT.printf("\nBuzzer: %s -> %s\n", settings.enableBuzzer ? "ENABLED" : "DISABLED", "DISABLED");
          settings.enableBuzzer = false;

          saveConfig();
        } else {
          Serial.printf("Unknown argument to command \"disable\".\n");
          SerialBT.printf("Unknown argument to command \"disable\".\n");
        }
      } else if (strcmp(arguments[0], "set") == 0) {

        if (numOfArgs == 3 && strcmp(arguments[1], "calibration_value") == 0) {
          char *endptr;
          float value = strtof(arguments[2], &endptr);

          if (*endptr == '\0') {

            Serial.printf("IAQ Calibration Value: %.2f ppm -> %.2f ppm\n", settings.calibrationValue, value);
            SerialBT.printf("\nIAQ Calibration Value: %.2f ppm -> %.2f ppm\n", settings.calibrationValue, value);

            settings.calibrationValue = value;

            saveConfig();

          } else {
            Serial.printf("Invalid value for \"calibration_value\"");
            SerialBT.printf("Invalid value for \"calibration_value\"");
            continue;
          }


        } else if (numOfArgs == 3 && strcmp(arguments[1], "iaq_warning_start_val") == 0) {
          char *endptr;
          float value = strtof(arguments[2], &endptr);

          if (*endptr == '\0') {

            if (settings.airClassificationCriticalValStart != NULL && settings.airClassificationCriticalValStart > value) {
              Serial.printf("WARNING IAQ Classification Starting Value: %.2f ppm -> %.2f ppm\n", settings.airClassificationWarningValStart, value);
              SerialBT.printf("WARNING IAQ Classification Starting Value: %.2f ppm -> %.2f ppm\n", settings.airClassificationWarningValStart, value);

              settings.airClassificationWarningValStart = value;

              saveConfig();
            } else {
              Serial.printf("Error Saving Value. Make sure that the \'CRITICAL IAQ Classification Starting Value\'  is not NULL and value is greater than the input.\n");
              SerialBT.printf("\nError Saving Value. Make sure that the \'CRITICAL IAQ Classification Starting Value\'  is not NULL and value is greater than the input.\n");
            }

          } else {
            Serial.printf("Invalid value for \"iaq_warning_start_val\"\n");
            SerialBT.printf("Invalid value for \"iaq_warning_start_val\"\n");
            continue;
          }

        } else if (numOfArgs == 3 && strcmp(arguments[1], "iaq_critical_start_val") == 0) {
          char *endptr;
          float value = strtof(arguments[2], &endptr);

          if (*endptr == '\0') {

            if (settings.airClassificationWarningValStart != NULL && settings.airClassificationWarningValStart < value) {
              Serial.printf("CRITICAL IAQ Classification Starting Value: %.2f ppm -> %.2f ppm\n", settings.airClassificationCriticalValStart, value);
              SerialBT.printf("CRITICAL IAQ Classification Starting Value: %.2f ppm -> %.2f ppm\n", settings.airClassificationCriticalValStart, value);

              settings.airClassificationCriticalValStart = value;

              saveConfig();
            } else {
              Serial.printf("Error Saving Value. Make sure that the \'WARNING IAQ Classification Starting Value\'  is not NULL and value is less than the input.\n");
              SerialBT.printf("\nError Saving Value. Make sure that the \'WARNING IAQ Classification Starting Value\'  is not NULL and value is less than the input.\n");
            }

          } else {
            Serial.printf("Invalid value for \"iaq_critical_start_val\"\n");
            SerialBT.printf("Invalid value for \"iaq_critical_start_val\"\n");
            continue;
          }


        } else if (numOfArgs == 3 && strcmp(arguments[1], "calibration_time") == 0) {
          char *endptr;
          float value = strtof(arguments[2], &endptr);

          if (*endptr == '\0') {

            int intVal = (int)value;

            if (value >= 0) {
              Serial.printf("Calibration Time: %dms -> %dms\n", settings.calibrationTime, intVal);
              SerialBT.printf("\nCalibration Time: %d -> %dms\n", settings.calibrationTime, intVal);

              settings.calibrationTime = intVal;

              saveConfig();
            } else {
              Serial.printf("Error Saving Value. Make sure to not input values less than 0.\n");
              SerialBT.printf("\nError Saving Value. Make sure to not input values less than 0.\n");
            }

          } else {
            Serial.printf("Invalid value for \"calibration_time\"\n");
            SerialBT.printf("Invalid value for \"calibration_time\"\n");
            continue;
          }


        } else {
          Serial.printf("Unknown argument to command \"set\".\n");
          SerialBT.printf("Unknown argument to command \"set\".\n");
        }

      } else if (numOfArgs == 1 && strcmp(arguments[0], "exit") == 0) {
        Serial.printf("Exiting Configuration Mode..\n");
        SerialBT.printf("\nExiting Configuration Mode..\n");

        break;
      } else if (numOfArgs == 1 && strcmp(arguments[0], "restart") == 0) {
        Serial.printf("Restarting Device..\n");
        SerialBT.printf("\nRestarting Device..\n");
        delay(1000);

        ESP.restart();
        break;
      } else if (numOfArgs == 1 && strcmp(arguments[0], "reset") == 0) {
        Serial.printf("Resetting Device..\n");
        SerialBT.printf("\Resetting Device..\n");

        factoryResetConfig();
        delay(1000);

        ESP.restart();
        break;
      } else {
        Serial.printf("Unknown Command.\n");
        SerialBT.printf("Unknown Command.\n");
      }
    }
  }

  SerialBT.end();

  u8x8.clear();
}

void intro() {

  // FLASHING SECTION
  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(YELLOW_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  delay(250);

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  if (settings.enableBuzzer) digitalWrite(BUZZER_PIN, HIGH);

  delay(250);

  digitalWrite(GREEN_LED_PIN, HIGH);
  digitalWrite(YELLOW_LED_PIN, HIGH);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  delay(250);

  digitalWrite(GREEN_LED_PIN, LOW);
  digitalWrite(YELLOW_LED_PIN, LOW);
  digitalWrite(RED_LED_PIN, LOW);
  if (settings.enableBuzzer) digitalWrite(BUZZER_PIN, HIGH);

  delay(250);
}
