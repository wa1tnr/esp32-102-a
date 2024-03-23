decimal \ cls.fs
: ESC ( -- n ) 27 ;
: e2j ( -- )
  [char] J [char] 2 [char] [ ESC
  emit emit emit emit ;
: cyanclr ( -- n'...n 9x elements )
  e2j [char] m [char] 6 [char] 3 [char] ; [char] 0 \ was '1' for bright
  [char] ; [char] 0 [char] [ ESC ;
: cls ( n 9x elements -- ) cyanclr 9 0 do emit loop ;
cls \ do it
\ end.
