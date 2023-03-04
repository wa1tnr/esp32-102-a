
 ( top of sub-program   OPERATIONAL 23:16z )
hex 500C2C00
hex 5 3 * .

: stripval ( n -- n<128 )
  dup 7f and
;

: nexchEmit ( n -- n+1 )
  dup c@ dup dup stripval = if
      dup 1F > if
          drop emit 1 + exit
      then
  then
  drop drop [char] . emit 1 + \ plus
;

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
