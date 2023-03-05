
 ( top of sub-program   OPERATIONAL 23:16z )
hex 500C2C00
hex 5 3 * .

: stripval ( n -- n<128 )
  dup 7f and
;

: nexchEmit ( addr -- addr+1 )
  dup c@ dup dup stripval = if \ if it didn't need stripping, after all..
      dup 1F > if   \ only printable chars survive
          drop emit \ print to console
          1 + exit  \ promised address increment
      then
  then
  drop drop [char] . emit \ non-printable char
  1 + \ increment addr
;

\ everything below this line is an optional 'macro'

: nc ( -- ) nexchEmit ; \ alias
: row 8 dup + 0 do nc loop
;
: frow 4 0 do row loop ;
: stanz 4 0 do frow cr loop ;
: group 4 0 do stanz  loop ;

: bfscan 8 dup * 0 do group loop ;

: goferit  swap drop dup bfscan ;

: kurtz 3F803400 700 +  group space group space  ." canwulf "  ;

here 2000 - \ minus

( end )

\ Considering making the second one also a mask op:

\ dup 7f and

\ dup 1F > if

\ END.
