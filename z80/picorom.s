; Pico ROM control ROM
; 
ver_major		EQU 2
ver_minor		EQU 0
ver_patch		EQU 0
TXT_OUTPUT: 	EQU $BB5A
KM_WAIT_KEY:	EQU $BB18
IO_PORT:		EQU $DFFC
CMD_PICOLOAD	EQU $FF
CMD_LED:		EQU $FE
CMD_CFGLOAD:	EQU $FD
CMD_CFGSAVE:	EQU $FC
CMD_ROM7:		EQU $FB
CMD_CFGDEF:		EQU	$FA
CMD_ROMDIR1		EQU $1
CMD_ROMDIR2		EQU $2
CMD_464:		EQU $3
CMD_6128:		EQU $4
CMD_664:		EQU $5
CMD_FW31:		EQU $6
CMD_ROMLIST1	EQU $7
CMD_ROMLIST2	EQU $8
CMD_CFGLIST1	EQU $9
CMD_CFGLIST2	EQU $A
CMD_ROMIN:		EQU $10
CMD_ROMOUT:		EQU $11

		org $c000
		defb    1       ; background rom
		defb    ver_major
		defb    ver_minor
		defb    ver_patch
		defw NAME_TABLE
		jp INIT
		jp BOOT
		jp LED
		jp ROMDIR
		jp CFGLIST
		jp ROMIN
		jp ROMOUT
		jp ROMLIST
		jp CFGLOAD
		jp CFGSAVE
		jp ROM7
		jp CFGDEF

NAME_TABLE:	
		defm  "PICO RO",'M'+128
		defm  "PICOLOA", 'D'+128
		defm  "LE", 'D'+128
		defm  "ROMDI", 'R'+128
		defm  "CFGLIS", 'T'+128
		defm  "ROMI", 'N'+128
		defm  "ROMOU", 'T'+128
		defm  "ROMLIS",'T'+128
		defm  "CFGLOA",'D'+128
		defm  "CFGSAV",'E'+128
		defm  "ROM", '7'+128
		defm  "CFGDE", 'F'+128
		defb    0
INIT:	
		push HL
		ld HL, START_MSG
		call disp_str
		pop HL
		ret
		
START_MSG:	
		defm  " Pico ROM v",'0'+ver_major,'.','0'+ver_minor,'0'+ver_patch,0x0d,0x0a,0x0d,0x0a,0x00

; handle a single parameter command
		MACRO CMD_1P cmd, invalid_msg
		LOCAL wait, invalid
		cp a, 1		; num params
		jp	nz,	invalid
		ld hl, RESP_BUF
		ld a, (hl)		; get current sequence number in A
		ld BC, IO_PORT
		out (c), c
		ld c, cmd
		out (c),c
		ld c,(IX)	; param
		out (c), c
.wait
		cp (hl)			; wait for the sequence number to be updated
		jr z,wait
		ret
.invalid
		ld hl, invalid_msg
		call disp_str
		ret
		ENDM

;
		MACRO CMD_0P_NOWAIT cmd
		ld BC, IO_PORT 	; command prefix
		out (c), c
		ld c, cmd 	; command byte
		out (c), c
		ret
		ENDM

		MACRO LIST_COMMAND cmd1, cmd2
		LOCAL wait, done ,nokey
		ld d, 22
		ld hl, RESP_BUF
		ld a, (hl)		; get current sequence number in A
		ld BC, IO_PORT 	; command prefix
		out (c), c
		ld C, cmd1 	; command byte
		out (c), c
.wait
		cp (hl)			; wait for the sequence number to be updated
		jr z, wait
		inc hl		; point to status code 
		ld a, (hl)
		or a
		jr nz,  done
		dec d
		jr nz, nokey
		push hl
		ld hl, KEY_MSG
		call disp_str
		pop hl
		call KM_WAIT_KEY
		ld d,22
.nokey
		inc hl		; skip data type # FIXME
		inc hl		; point to start of response
		call disp_str
		call cr_nl
		; get next
		ld hl, RESP_BUF
		ld a, (hl)		; get current sequence number in A
		ld BC, IO_PORT	; command prefix
		out (c), c
		ld C, cmd2 	; command byte
		out (c), c
		jr		wait	; wait for next command to complete
.done
		call cr_nl
		ret
		ENDM

KEY_MSG:	defm 0x0d,0x0a,"*** Press any key ***",0x0d,0x0a,0x0d,0x0a,0x00


IP_MSG:	
		defm  "Invalid parameters",0x0d,0x0a,0x0d,0x0a,0x00


LED:		CMD_1P CMD_LED, IP_MSG
CFGLOAD:	CMD_1P CMD_CFGLOAD, IP_MSG
CFGSAVE:	CMD_1P CMD_CFGSAVE, IP_MSG
CFGDEF:	CMD_1P CMD_CFGDEF, IP_MSG
ROM7:		CMD_1P CMD_ROM7, IP_MSG

BOOT:		CMD_0P_NOWAIT CMD_PICOLOAD
CFGLIST: 	LIST_COMMAND CMD_CFGLIST1, CMD_CFGLIST2
ROMDIR:		LIST_COMMAND CMD_ROMDIR1, CMD_ROMDIR2
ROMLIST:	LIST_COMMAND CMD_ROMLIST1, CMD_ROMLIST2


ROMIN:
		cp	2
		jr	nz, RI_USAGE
		ld BC, IO_PORT 	; command prefix
		out (c), c
		ld C, $10 	; command byte
		out (c), c
		ld C, (IX+0)
		out (c), c
		ld C, (IX+2)
		out (c), c
		jr	RI_DONE
RI_USAGE:
		ld hl, RI_U_MSG
		call disp_str
RI_DONE:
		ret
RI_U_MSG:
		defm  " Usage |ROMIN,<ROM BANK>,<ROM Number>",0x0d,0x0a,0x0d,0x0a,0x00

ROMOUT:		CMD_1P CMD_ROMOUT, RO_U_MSG
RO_U_MSG:
		defm  " Usage |ROMOUT,<ROM BANK>",0x0d,0x0a,0x0d,0x0a,0x00


cr_nl:
		push af
		ld A, 0x0d
		call TXT_OUTPUT
		ld A, 0x0a
		call TXT_OUTPUT
		pop af
		ret

disp_str:	; display 0 terminated string, pointed to by HL
		push af
disp_str1:	
		ld A, (HL)
		call TXT_OUTPUT
		inc HL
		or A
		jr nz, disp_str1
		pop af
		ret
END:
		DEFS $4000-END-$100
RESP_BUF:
		; Response buffer
		; 0 - sequence number, increased by Pico for each response
		; 1 - status code. 0=OK
		; 2 - data type. 1 = 0 terminated string
		; 3.. data
		DEFS $100, 0
