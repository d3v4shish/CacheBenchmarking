#include "textflag.h"

// func T2(addr unsafe.Pointer)
TEXT ·T2(SB), NOSPLIT, $0-8
	MOVQ addr+0(FP), AX
	PREFETCHT2 (AX)
	RET
