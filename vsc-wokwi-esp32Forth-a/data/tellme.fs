

: waitfor 
    17 555 0 DO 
        key? \ seems to map to a keystroke
        32 - 0= IF
        EXIT
        THEN
        cr cr .s cr cr blink blink blink 
    LOOP 
; 


: waitfor 
    17 555 0 DO 
        key? \ seems to map to a keystroke
        32 - 0= IF
        EXIT
        THEN
        cr cr .s cr cr blink blink blink 
    LOOP 
; 

decimal
VARIABLE iterator
: waitfor 
    17 555 0 DO 
        key? \ seems to map to a keystroke
        32 - 0=
        IF
            EXIT
        THEN
        iterator @ 1 + dup . iterator !
        cr cr .s cr cr blink
    LOOP 
; 

0 --> 17 FRED !
 ok
0 --> FRED @ .
17  ok
0 --> 


\ end.
