
 ( top of sub-program   OPERATIONAL 00:56z Sunday 5 March 2023 )

hex 500C2C00
hex 5 3 * .

: stripval ( n -- n<128 )
  dup 7f and
;

: nexchEmit ( addr -- addr+1 )
  dup c@ dup dup stripval = if
      dup 1F > if
          drop emit
          1 + exit
      then
  then
  drop drop [char] . emit
  1 + \ plus
;

\ optional macros -----

: nc ( -- ) nexchEmit ; \ alias
: row 8 dup + 0 do nc loop
;
: frow 4 0 do row loop ;
: stanz 4 0 do frow cr loop ;
: group 4 0 do stanz  loop ;

: bfscan 8 dup * 0 do group loop ;

: goferit  swap drop dup bfscan ;

: kurtz 3F803400 700 +  group space group space  ." pirhana sp"  ;

here 2000 - \ minus

( end )

: version ." 0.0.0aa-" cr ;
\ END.
