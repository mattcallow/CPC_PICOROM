; Pico ROM control ROM
; 
ver_major		EQU 3
ver_minor		EQU 1
ver_patch		EQU 1
TXT_OUTPUT: 	EQU $BB5A
KM_WAIT_KEY:	EQU $BB18
IO_PORT:		EQU $DFFC

CMD_PICOLOAD	EQU $FF
CMD_LED:		EQU $FE
CMD_ROMDIR1		EQU $FD
CMD_ROMDIR2		EQU $FC
CMD_ROMLIST1	EQU $FB
CMD_ROMLIST2	EQU $FA
CMD_ROMIN:		EQU $F9
CMD_ROMOUT:		EQU $F8
CMD_ROMSET:		EQU $F7

		org $c000
		defb    1       ; background rom
		defb    ver_major
		defb    ver_minor
		defb    ver_patch
		defw NAME_TABLE
		jp INIT
		jp BOOT
		jp LED
		jp ROMSET
		jp ROMDIR
		jp ROMLIST
		jp ROMOUT
		jp ROMIN

NAME_TABLE:	
		defm  "PICO RO",'M'+128
		defm  "PUS", 'B'+128
		defm  "LE", 'D'+128
		defm  "ROMSE", 'T'+128
		defm  "PDI", 'R'+128
		defm  "ROM", 'S'+128
		defm  "ROMOU", 'T'+128
		defm  "ROMI", 'N'+128
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
		ld d, 22		; number of lines to display
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
BOOT:		CMD_0P_NOWAIT CMD_PICOLOAD
ROMDIR:		LIST_COMMAND CMD_ROMDIR1, CMD_ROMDIR2
ROMLIST:	LIST_COMMAND CMD_ROMLIST1, CMD_ROMLIST2

		MACRO WAIT_FOR_COMPLETION
		; wait for command to finish. Last seq value in A
		; display the result
		LOCAL wait
		ld	hl, RESP_BUF
.wait
		cp (hl)			; wait for the sequence number to be updated
		jr z, wait
		inc	hl		; skip status code # FIXME
		inc hl		; skip data type # FIXME
		inc hl		; point to start of response
		call disp_str
		call cr_nl
		ENDM

ROMIN:
		cp	2
		jr	nz, RI_USAGE

		LD   L,(IX+0)
        LD   H,(IX+1)   ; HL = string descriptor

		ld	A,(HL)			; length
		cp	0
		jr	z,	RI_DONE		; no file given

		ld BC, IO_PORT 	; command prefix
		out (c), c
		ld C, CMD_ROMIN
		out (c), c
		ld C, (IX+2)	; slot number
		out (c), c

		ld c, (HL)			; get length in C
		out (c), c			; and send it
		inc	HL
		ld 	E,(HL)
		inc	HL
		ld 	D,(HL)
		ex  DE, HL			; string address now in HL
		ld	de, RESP_BUF
		ld	d, (de)	; save current sequence number in D
	; send filename - length is in A
RI_LOOP:
		ld	c,(HL)
		out	(c),C
		inc	HL
		dec	A
		jr	nz,	RI_LOOP

		; wait for completion
		ld 	a,d			; get current seqeuence number
		WAIT_FOR_COMPLETION
		jr	RI_DONE
RI_USAGE:
		ld hl, RI_U_MSG
		call disp_str
RI_DONE:
		ret
RI_U_MSG:
		defm  " Usage |ROMIN,<ROM BANK>,<ROM filename>",0x0d,0x0a,0x0d,0x0a,0x00

ROMOUT:		CMD_1P CMD_ROMOUT, RO_U_MSG
RO_U_MSG:
		defm  " Usage |ROMOUT,<ROM BANK>",0x0d,0x0a,0x0d,0x0a,0x00


ROMSET:	; load a romset from file
		cp	1
		jr	nz, RS_USAGE
		LD   L,(IX+0)
        LD   H,(IX+1)   ; HL = string descriptor

		ld	A,(HL)			; length
		cp	0
		jr	z,	RS_USAGE	; no file given

		ld BC, IO_PORT 	; command prefix
		out (c), c
		ld C, CMD_ROMSET 	; command byte
		out (c), c

		ld c, (HL)			; length
		out (c), c
		inc	HL
		ld 	E,(HL)
		inc	HL
		ld 	D,(HL)
		ex  DE, HL			; string address now in HL

		ld	de, RESP_BUF
		ld	d, (de)	; save current sequence number in D

		; send filename - length is in A
RSLOOP:
		ld	c,(HL)
		out	(c),C
		inc	HL
		dec	A
		jr	nz,	RSLOOP

		; wait for completion
		ld 	a,d			; get saved seqeuence number
		WAIT_FOR_COMPLETION
		jr	RS_DONE
RS_USAGE:
		ld hl, RS_U_MSG
		call disp_str
RS_DONE:
		ret
RS_U_MSG:
		defm  " Usage |ROMSET,<CONFIG>",0x0d,0x0a,0x0d,0x0a,0x00

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
		or A
		jr z, disp_done
		call TXT_OUTPUT
		inc HL
		jr disp_str1
disp_done:
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
