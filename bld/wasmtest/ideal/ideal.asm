		IDEAL
		P486
		MASM
DATA		SEGMENT USE32 DWORD PUBLIC 'DATA'
Value1		DD	12345678h
DATA		ENDS
		IDEAL
SEGMENT		BSS	USE32 DWORD PUBLIC 'BSS'
Value2		DD	?
ENDS
GROUP		DGROUP	DATA, BSS
SEGMENT		CODE	USE32 DWORD PUBLIC 'CODE'
		ASSUME	CS:CODE, DS:DGROUP, es:DGROUP
PROC		Main
		mov	eax,[Value1]
		mov	[Value2],eax
ENDP
ENDS
		END
