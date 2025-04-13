#include <Arduino.h>

#define MQ135_SENSOR_PIN  12

class AirQualityReading {
  private:
    int airQualReadingsArr[10];
    int airQualValue = 0;
  public:
    void addValue(int value) {
      for(int i = 0; i < sizeof(airQualReadingsArr); i++) airQualReadingsArr[i] = airQualReadingsArr[i + 1];
      airQualReadingsArr[sizeof(airQualReadingsArr) - 1] = value;
    }
    int getReading() {
      
    }
};


void setup() {
  Serial.begin(115200);

  Serial.println("Hello World!");
}

void loop() {
  int sensorValue = analogRead(MQ135_SENSOR_PIN);

  Serial.printf("Sensor Value: %d\n", sensorValue);

  delay(1000);

}