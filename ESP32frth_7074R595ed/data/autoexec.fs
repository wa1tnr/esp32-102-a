\ ." Tue 22 NOv 21:58:39 UTC 2022"

include bobby.fs
include cubby.fs
include roy.fs

: delay 500 * 0 do 1 drop loop ;

: bcr space space bobby 2000 delay
  cubby 2000 delay
  roy 2000 delay space space jerry cr ;

: fvlb space space ." flava bean" ;
: aexts
        ." Tue 22 Nov 22:07:11 UTC 2022"
;
: sayw ."  SAY:    'words'  or  'bye'" ;
: aexc cr ." This is your spiff - try 'make spiffs' at linux shell (or equivalent) " cr cr
  aexts
  ."   data/autoexec.fs   mst3k" fvlb cr cr sayw cr cr
  bcr cr cr
;

\ latest version of the program prints on the following line in the terminal (not here).

: LEDv2 13 ;
: ledSetup LEDv2 output pinmode ;
: blink high LEDv2 pin 44 delay low  LEDv2 pin 888 delay ;
: blinks 0 do blink loop ;

-98 -97 \ no idea why the stack is deficient by two stack items.. but it is
aexc ledsetup 5 blinks

\ end.
