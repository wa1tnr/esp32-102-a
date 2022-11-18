// Fri 18 Nov 01:52:29 UTC 2022
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

    while (!Serial) {
      ;
    }

    Serial.println("Hello Arduino!");

#if 0
    bool ser_state = Serial ; // true == connected?

    if (ser_state) { // connected?
        Serial.print(" Serial, rather than !Serial "); // this one prints
    }
    if (!ser_state) {
        Serial.print(" !Serial, rather than Serial "); // this one does not print
    }
#endif

    delay(700);
    Serial.println("begin program.");
}

void setup(void) {
    delay(700);
    setup_gpio();
    setup_serial();
}

void loop(void) {
    Serial.println("I am looping: cdefg ");
    cpl();
    delay(1000);
    // while(-1);
}

// END.
