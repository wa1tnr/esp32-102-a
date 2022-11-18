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
    // cpl();
}

void clrSerial() {
    int avbl = Serial.available();
    while (avbl > 0) {
        char ch = Serial.read(); // flush
    }
}

void setup_serial() {
    if (!Serial) {
        Serial.begin(115200);
        clrSerial();
    }

#if 0

    while (!Serial) {
      ; // no new benefit
    }

    Serial.println("Hello Arduino!");

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
    setup_serial();
    setup_gpio();
    delay(700);
}

void loop(void) {
    Serial.println("  I am looping: cdefg 02:39:30z  ");
    cpl();
    delay(1000);
    // while(-1);
}

// END.
