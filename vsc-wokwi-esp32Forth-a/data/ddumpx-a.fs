
 ( top of sub-program   OPERATIONAL 00:56z Sunday 5 March 2023 )

." Sat 23 Mar 21:47:30 UTC 2024 "

\ FFFFFF9D 500C2C00 3FFF0030 3FFE5F00 3FFEC718

hex
: faves
  ." unknown: "   ." 500C2C00 "  500C2C00
  ." basal mem: " ." 3F400000 "  3F400000
  ." spiffs: "    ." 3FFCC000 "  3FFCC000 ;

hex 500C2C00
    3F400000 \ low or lowest address with forth source code seen 24 March 20:47z
    3FFF0030 \ wonder what - is listed after a 'bye'
    3FFE5F00 \ pretty much the start of printable forth dump
    \ 3FFE5F00
    \ 3FFEC718 \ about 26k offset from the one above
    \ no idea how it gets there though ;)
    \ compare with HERE

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
