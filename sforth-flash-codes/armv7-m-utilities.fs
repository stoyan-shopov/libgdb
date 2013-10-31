( leave gdb server mode and enter sforth mode, in case this sforth runs on the vortex probe $.#2e ( )

\ armv7m debug support words

( utility words )
: page 100 0 do cr loop ;

page

unused constant total-core
: dump-stats total-core dup . ." bytes total" cr
	unused - . ." bytes used" cr
	unused . ." bytes remaining" cr ;

: bit ( bit-nr -- bitmask ) 1 swap lshift ;
: bconst ( bit-nr -- ) bit constant ;

( armv7-m utility words )
hex

( debug register support in the SCS )
e000edf0 constant DHCSR

( DHCSR bits )
a05f0000 constant DBGKEY

decimal

16 bconst S_REGRDY
17 bconst S_HALT
3 bconst C_MASKINTS
2 bconst C_STEP
1 bconst C_HALT
0 bconst C_DEBUGEN

hex
e000edf4 constant DCRSR

( DCRSR bits )
decimal
16 bit constant REGWnR
hex
e000edf8 constant DCRDR

( flash patch and breakpoint - FPB - unit registers )
e0002000 constant FP_CTRL
( FP_CTRL bits )
decimal
1 bconst FP_CTRL_KEY
0 bconst FP_CTRL_ENABLE
hex
e0002008 constant FP_COMP0
( FP_COMP0 bits )
decimal
30 bconst FP_COMP_BREAK_ON_LOWER_HALFWORD
31 bconst FP_COMP_BREAK_ON_UPPER_HALFWORD
0 bconst FP_COMP_ENABLE


( local words )
: wait-reg-xfer-ready ( -- ) begin DHCSR t@ S_REGRDY and until ;
: dhcsr-write ( val -- ) DBGKEY C_DEBUGEN or or DHCSR t! ;

( global words )

: armv7m-init ( -- ) FP_CTRL_KEY FP_CTRL_ENABLE or FP_CTRL t! C_DEBUGEN dhcsr-write ;

: armv7m-reg-read ( reg-nr -- reg-val ) DCRSR t! wait-reg-xfer-ready DCRDR t@ ;

: armv7m-reg-write ( reg-nr val -- ) DCRDR t!
		REGWnR or DCRSR t!
		wait-reg-xfer-ready ;

: armv7m-wait-halted ( -- ) begin DHCSR t@ S_HALT and until ;
: armv7m-request-halt ( -- ) C_HALT dhcsr-write ;
: armv7m-halt ( -- ) armv7m-request-halt armv7m-wait-halted ;
: armv7m-run ( -- ) 0 dhcsr-write ;
hex
: armv7m-insert-bkpt ( nr-hw-bkpt bkpt-addr -- ) dup 1 bit and
		if FP_COMP_BREAK_ON_UPPER_HALFWORD else FP_COMP_BREAK_ON_LOWER_HALFWORD then
		swap c0000003 invert and or 0 bit or FP_COMP0 rot cells + t! ;
: armv7m-remove-bkpt ( nr-hw-bkpt -- ) 0 swap cells FP_COMP0 + t! ;
: armv7m-step ( -- ) C_STEP dhcsr-write armv7m-wait-halted ;
: armv7m-step-no-ints ( -- ) C_MASKINTS C_HALT or dhcsr-write
		C_STEP C_MASKINTS or dhcsr-write
		armv7m-wait-halted C_HALT dhcsr-write ;
: armv7m-mem-dump ( addr cnt -- ) 0 do dup t@ . 4 + loop drop ;

( shortcuts )

: rr armv7m-reg-read ; 
: rw armv7m-reg-write ; 
: mdump armv7m-mem-dump ;
: cont armv7m-run ;
: halt armv7m-request-halt ;

( vx board specific section)
( configure pb0 as output on the vx - this is the target reset signal)
hex
40010c00 constant pb-base
( control register low)
pb-base 0 + constant pb-crl
( port set register)
pb-base 10 + constant pb-sr
( port reset register)
pb-base 14 + constant pb-rr

( port b reset pin declaration)
0 bconst rst-pin
( configure reset pin as an output)
pb-crl dup @ f invert and 3 or swap !

: rst-low rst-pin pb-rr ! ;
: rst-hi rst-pin pb-sr ! ;


( initialization )

armv7m-init

decimal

