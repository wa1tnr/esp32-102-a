\ recent.fs
\ 24 March 2024

decimal
VARIABLE iterator
: lwaitfor 
  key? IF QUIT THEN
  iterator @ 1 + iterator !
  depth 0= IF QUIT THEN
  depth 10 - 0= IF ." ten minus" cr QUIT THEN
; 

hex
: picked HERE 8000 - 8000 - 1000 - ;
: g group ;
: scan g blink dup . ;
: scans 0 do scan loop ;

: bfscans ( n -- )
  0 do
      bfscan
      blink blink
      200 0 do lwaitfor loop
  loop
;

\ end.
