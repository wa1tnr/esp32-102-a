\ recent.fs
\ 24 March 2024

\ waitfor.fs  24 March 20:45z
decimal
VARIABLE iterator
: lwaitfor 
  key? IF QUIT THEN
  iterator @ 1 + dup . iterator !
  cr .s cr
  depth 0= IF QUIT THEN
  depth 10 - 0= IF ." ten minus" cr QUIT THEN
; 

hex
: picked HERE 8000 - 8000 - 1000 - ;
: g group ;
: scan g blink dup . ;
: scans 0 do scan loop ;

: bfscans ( -- )
  0 do
      bfscan
      blink blink cr dup . cr
      lwaitfor
      8000 ms
  loop
;

\ end.
