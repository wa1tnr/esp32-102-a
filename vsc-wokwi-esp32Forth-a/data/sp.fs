\ sp@ tester
\ 'is the stack' or says the stack depth
decimal
: testv 55 -1 * dup 1 + dup 1 + sp@ . ;

\ 99 3 4 -99 -98 -97 22467 3 99182 17 44 212121 187 55 --> sp@ 2 4 * - @ .
\ 212121  ok

\ idiom:  sp@ @ .   print TOS to console

\ sp@ (stack pointer fetch) gives the address of TOS
\ @ (fetch) reads it into TOS, pushing down the stack one more element
\ THUS  'dup' could be coded this way:

: duppp sp@ @ ;

\ . (print destructively) pops that value and prints it to the console

\ or somesuch. ;)
