\ Mon 21 Nov 00:57:58 UTC 2022

: fvlb space space ." flava bean" ;
: aexc cr ." This is your spiff - try 'make spiffs' " cr cr
  ." Sun 20 Nov 22:04:41 UTC 2022   data/autoexec.fs   kelto" fvlb ;

: delay 500 * 0 do 1 drop loop ;
: LEDv2 13 ;
: ledSetup LEDv2 output pinmode ;
: blink high LEDv2 pin 44 delay low  LEDv2 pin 888 delay ;
: blinks 0 do blink loop ;

-98 -97 \ no idea why the stack is deficient by two stack items.. but it is
aexc ledsetup 5 blinks

\ end.
