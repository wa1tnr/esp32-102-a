// Fri 18 Nov 01:14:05 UTC 2022
// +cpl +println

#include <Arduino.h>

void cpl() {
    int pin = LED_BUILTIN ;
    bool state = digitalRead(pin);
    state = !state;
    digitalWrite(pin, state);
}

void setup_gpio() {
    pinMode(LED_BUILTIN, OUTPUT);
    cpl();
}

void setup_serial() {
    Serial.begin(115200);
    while(!Serial);
    delay(700);
    Serial.println("begin program.");
}

void setup(void) {
    delay(700);
    setup_gpio();
    setup_serial();
}

void loop(void) {
    Serial.println("I am looping: bcdef ");
    cpl();
    delay(1000);
    // while(-1);
}

// END.
