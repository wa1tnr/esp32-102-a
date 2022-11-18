// Fri 18 Nov 11:37:36 UTC 2022
// +cpl +println

#define NOWDATE "Fri 18 Nov 11:37:36 UTC 2022"

#include <Arduino.h>
#include <Adafruit_TestBed.h>

extern Adafruit_TestBed TB;

#define cr _cr(); // carriage return
#define lf _lf(); // carriage return
#define crlf _cr();_lf();

void _cr() { Serial.write(0x0d); }
void _lf() { Serial.write(0x0a); }

void cpl() {
    int pin = LED_BUILTIN ;
    bool state = digitalRead(pin);
    state = !state;
    digitalWrite(pin, state);
}

#define NEOPIXEL_PIN 0

void setup_rgb() {
    pinMode(NEOPIXEL_I2C_POWER, OUTPUT);
    digitalWrite(NEOPIXEL_I2C_POWER, HIGH);
}

void setup_gpio() {
    pinMode(LED_BUILTIN, OUTPUT);
    setup_rgb();
}

void clrSerial() {
    int avbl = Serial.available();
    while (avbl > 0) {
        char ch = Serial.read(); // flush
    }
}


void signon_msg() {
    // https://docs.platformio.org/en/stable/projectconf/section_env_build.html
    // Serial.println("MYSTRING=<%s>\n", MYSTRING);
    Serial.println(MYSTRING);
}

void setup_serial() {
    if (!Serial) {
        // Serial.begin(115200);
        Serial.begin(74880);
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
    crlf
    crlf
    crlf
    crlf
    Serial.println("begin program.");
}

void setup(void) {
    delay(700);
    setup_serial();
    setup_gpio(); delay(700);
    crlf signon_msg(); delay(700);
}

uint8_t wheelColor=0;

void loop(void) {
    Serial.print("  I am looping: efghi ");
    // Serial.println(NOWDATE);
    // Serial.println(MYSTRING);
    Serial.println(MYOTHERSTR);
    cpl();
    delay(1000);
    // while(-1);
}

// END.
