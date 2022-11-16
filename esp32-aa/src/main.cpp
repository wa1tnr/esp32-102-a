#include <Arduino.h>

void setup(void) {
    delay(700);
    Serial.begin(9600);
    while(!Serial);
    Serial.println("begin program.");
}

void loop(void) {
    while(-1);
}

// END.
