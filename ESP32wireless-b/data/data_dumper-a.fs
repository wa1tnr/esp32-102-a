 ( top of sub-program 18:26z 2023 05 Mar Sunday )
hex 500C2C00 ( -- addr )
hex 5 3 * . ( -- )
: stripval ( n -- n<128 )
  dup 7f and ;
: nexchEmit ( addr -- addr+1 )
  dup c@ dup dup stripval = if
      dup 1F > if
          drop emit 1 + exit
      then
  then
  drop drop [char] . emit 1 + ;
: nc ( addr -- addr+1 ) nexchEmit ; \ alias  ----- optional macros -----
: row ( addr -- addr+8 ) 8 dup + 0 do nc loop ;
: frow ( addr -- addr+32 ) 4 0 do row loop ;
: stanz ( addr -- addr+128 ) 4 0 do frow cr loop ;
: group ( addr -- addr+512 ) 4 0 do stanz  loop ;
: bfscan ( addr -- addr+32768 ) 8 dup * 0 do group loop ; \ 64 x 512
: goferit ( addr addr' -- addr'+32768 ) swap drop dup bfscan ;
: kurtz ( addr -- addr+1024 ) 3F803400 700 +  group space group space  ." octopii gmbh "  ;
: giveagoodhoot  40C00000 50000000 ffff - ffff - ffff - ffff -   ffff - ffff - ffff - ffff - ;
here 2000 - ( -- addr-0x2000 )
decimal
: testi ( n -- ) \ print chars above ascii 31
    dup 31 255 xor and if
        dup .
        emit space s" interior " type cr exit
    then
    drop
;
hex
variable ltcount
: granf
    DUP ltcount !
    0 DO
        ltcount @ DUP testi 1 + ltcount !
    LOOP
;
: version ( -- ) ." 05 Mar 2023   0.0.0bb-" cr ;
( end )
