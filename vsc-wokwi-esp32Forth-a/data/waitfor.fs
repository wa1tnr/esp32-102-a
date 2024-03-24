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

: waitfor 55555 0 do lwaitfor loop ;

\ end.
