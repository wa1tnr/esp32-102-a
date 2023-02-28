#include <Arduino.h>

// Tue 28 Feb 19:20:09 UTC 2023

// using -dd as model

// while(!Serial) does not seem to wait for serial connection

void setup_gpio() {
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, 0);
    delay(220);
}

void cpl() {
    int pin = LED_BUILTIN ;
    bool state = digitalRead(pin);
    state = !state;
    digitalWrite(pin, state);
}

void blink() {
    cpl();
    delay(4);
    cpl();
    delay(4);
}

// #define LARGE_COUNT 65532
#define LARGE_COUNT   131064 // foundation
#define COUNT_DIVISOR      2 // 65532
#define COUNT_2ND_DIV      3 // 21844
#define COUNT_MULTIPLIER  12 // 6291072 for 288  0x5FFE80 abt 23 bits

#define COUNT_ABS ( ( ( LARGE_COUNT / COUNT_DIVISOR ) / COUNT_2ND_DIV ) * COUNT_MULTIPLIER )

// int a = ( ( ( LARGE_COUNT / COUNT_DIVISOR ) / COUNT_2ND_DIV ) * COUNT_MULTIPLIER )

const uint32_t counter_iter = COUNT_ABS ;

void wait_short() {
    for (volatile uint32_t counting = counter_iter; counting > 1; counting--) {
        // nothing
    }
}

void blink_durational() {
    for (volatile int looping = 65; looping > 1; looping--) { // was 254 ;)
        wait_short();
    }
}

bool validate_serial() {
    uint8_t ser_count = Serial.available();
    bool result = 0;
    if (ser_count > 0) { // may prevent underflow - did not verify
        char ch = Serial.read();
        result = -1 ; // yes serial read
        return result ;
    }
    result = 0 ; // force desired state
    return result ;
}

void setup_serial() {
    Serial.begin(115200);

    bool serial_is_valid = 0 ;

    do {
        serial_is_valid = validate_serial() ;
        if (!serial_is_valid) { // blink while awaiting connection
            blink();
            blink_durational();
        }
    } while (!serial_is_valid);

}

void best_setup(void) {
    setup_gpio();
    setup_serial();
}

void setup(void) {
    delay(700);
    best_setup();
    Serial.println("\r\n\r\n   begin program  28 Feb 19:20z\r\n\r\n");
}

void loop(void) {
    Serial.print(" . "); // heartbeat tty serial
    // blink();
    blink_durational(); // wait about as long as a blink between heartbeats
}

// END.
