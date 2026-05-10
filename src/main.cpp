#include <Arduino.h>

const uint8_t testPins[] = {2, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33};
const size_t testPinCount = sizeof(testPins) / sizeof(testPins[0]);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Essiccatore ESP32 blink test");
  Serial.println("Accendo in sequenza i GPIO piu comuni per trovare il LED integrato.");

  for (size_t i = 0; i < testPinCount; i++) {
    pinMode(testPins[i], OUTPUT);
    digitalWrite(testPins[i], LOW);
  }
}

void loop() {
  for (size_t i = 0; i < testPinCount; i++) {
    const uint8_t pin = testPins[i];

    Serial.print("GPIO ");
    Serial.println(pin);

    digitalWrite(pin, HIGH);
    delay(500);
    digitalWrite(pin, LOW);
    delay(500);
  }
}
