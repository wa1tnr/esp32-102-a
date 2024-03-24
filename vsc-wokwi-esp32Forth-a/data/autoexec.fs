\ ." Sat 23 Mar 21:47:30 UTC 2024 "

\ ." Sun  5 Mar 18:45:51 UTC 2023"

\ include data_dumper-a.fs
include data_d_tests.fs

: delay 500 * 0 do 1 drop loop ;

: tstamp ." Arduino/ESP32wireless-b   tupelo  kenesis" cr
         ." Sun  5 Mar 18:45:51 UTC 2023" cr ;

: sayw ."  SAY:    'words'  or  'bye'" ;

\ : LEDv2 13 ;
: LEDthis 15 ;
: ledSetup LEDthis output pinmode ;
: blink high LEDthis pin 144 ms \ delay
  low  LEDthis pin 880 ms ; \ delay
: blinks 0 do blink loop ;

include cls.fs
include ddumpx-a.fs
\ include led_blink.fs
include sp.fs
include recent.fs
include kermit.fs
\ include minioff.fs
\ include tcpptp.fs
0 bg 2 fg
-98 -97 \ no idea why the stack is deficient by two stack items.. but it is
ledsetup cr sayw space space space tstamp cr blink

\ end.
