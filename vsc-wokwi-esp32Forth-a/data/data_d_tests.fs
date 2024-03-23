\ Tue  7 Mar 19:45:52 UTC 2023

hex 5 3 * . ( -- )

decimal

-99 dup 1 + dup 1 +

: pctsgn   $25 ; \ '%' char
: dotsymb  $2e ; \ '.' char

: hi_gate ( -- n )  31 255 xor ; \ $e0
: lo_gate ( -- n ) $7f $ff xor ; \ $80

variable schar 0 schar !

\ return 0 if < 32:
: gated_hi? ( n -- n bool )
  dup hi_gate and 0=
  dup
  if
    dotsymb
    schar
    !
  then
;

: except7f ( n -- n )
  dup $7f = if pctsgn schar ! then \ $7f rx special handling
;

\ return 0 if > 127
: gated_lo? ( n -- n bool )
  dup lo_gate and 0= invert dup if pctsgn schar ! then
  swap except7f swap
;

: handledc
  gated_hi?  drop
  gated_lo?  drop
  schar @ 0= if ." char: " emit space exit then
  drop schar @  ." char: " emit space
;

: handled handledc 0 schar ! ;

: print_index ( n -- n )
  cr ." hex: "
  dup $10 < if space then
  dup hex . decimal
;

hex variable counted

: limits ( n -- n_limit )
  1 max $100 min
  counted @ 1 > if
  then
  $100 counted @ - min
;

: climits ( c -- c_limit )
  dup 0 < if 0 max then $ff min ;

: handling ( n -- ) print_index handled ;

: test ( start_ch count -- )
  cr ." params: start_char count " cr
  swap climits counted ! limits
  0 do
      counted @ handling counted @ 1 + counted !
  loop ;

: version ( -- ) ." gforth 07 Mar 2023   0.0.0bb--gf-jj-" cr ;
decimal

( end )
