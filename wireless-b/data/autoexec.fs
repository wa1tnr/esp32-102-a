\ ." Sat 26 Nov 02:17:31 UTC 2022"

include minioff.fs
include tcpptp.fs

: delay 500 * 0 do 1 drop loop ;

: tstamp ." Sat 26 Nov 02:17:31 UTC 2022";
: sayw ."  SAY:    'words'  or  'bye'" ;

: LEDv2 13 ;
: ledSetup LEDv2 output pinmode ;
: blink high LEDv2 pin 44 delay low  LEDv2 pin 888 delay ;
: blinks 0 do blink loop ;

0 bg 2 fg
-98 -97 \ no idea why the stack is deficient by two stack items.. but it is
ledsetup cr sayw space space space tstamp cr blink

\ end.
