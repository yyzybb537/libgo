#define XHOOKS_INTERNAL 1
#include <Windows.h>
#include "xhook.h"

// #define XHOOK_DEBUG 1

#if defined(XHOOKS_X86)
#elif defined(XHOOKS_X64)
#elif defined(XHOOKS_IA64)
#elif defined(XHOOKS_ARM)
#else
#error Must define one of XHOOKS_X86, XHOOKS_X64, XHOOKS_IA64, or XHOOKS_ARM
#endif

#if !defined(XHOOKS_32BIT) && !defined(XHOOKS_64BIT)
#error Must define one of XHOOKS_32BIT or XHOOKS_64BIT
#endif

#undef ASSERT
#define ASSERT(x)

#include <stddef.h>
#if (_MSC_VER < 1299)
typedef DWORD DWORD_PTR;
#endif
#if (_MSC_VER < 1310)
#else
#include <strsafe.h>
#endif

#include <limits.h>

#if (_MSC_VER < 1299)
#pragma warning(disable: 4710)
#endif

//////////////////////////////////////////////////////////////////////////////
//
//  Function:
//      XHookCopyInstruction(PVOID pDst,
//                            PVOID *ppDstPool
//                            PVOID pSrc,
//                            PVOID *ppTarget,
//                            LONG *plExtra)
//  Purpose:
//      Copy a single instruction from pSrc to pDst.
//
//  Arguments:
//      pDst:
//          Destination address for the instruction.  May be NULL in which
//          case XHookCopyInstruction is used to measure an instruction.
//          If not NULL then the source instruction is copied to the
//          destination instruction and any relative arguments are adjusted.
//      ppDstPool:
//          Destination address for the end of the constant pool.  The
//          constant pool works backwards toward pDst.  All memory between
//          pDst and *ppDstPool must be available for use by this function.
//          ppDstPool may be NULL if pDst is NULL.
//      pSrc:
//          Source address of the instruction.
//      ppTarget:
//          Out parameter for any target instruction address pointed to by
//          the instruction.  For example, a branch or a jump insruction has
//          a target, but a load or store instruction doesn't.  A target is
//          another instruction that may be executed as a result of this
//          instruction.  ppTarget may be NULL.
//      plExtra:
//          Out parameter for the number of extra bytes needed by the
//          instruction to reach the target.  For example, lExtra = 3 if the
//          instruction had an 8-bit relative offset, but needs a 32-bit
//          relative offset.
//
//  Returns:
//      Returns the address of the next instruction (following in the source)
//      instruction.  By subtracting pSrc from the return value, the caller
//      can determinte the size of the instruction copied.
//
//  Comments:
//      By following the pTarget, the caller can follow alternate
//      instruction streams.  However, it is not always possible to determine
//      the target based on static analysis.  For example, the destination of
//      a jump relative to a register cannot be determined from just the
//      instruction stream.  The output value, pTarget, can have any of the
//      following outputs:
//          XHOOK_INSTRUCTION_TARGET_NONE:
//              The instruction has no targets.
//          XHOOK_INSTRUCTION_TARGET_DYNAMIC:
//              The instruction has a non-deterministic (dynamic) target.
//              (i.e. the jump is to an address held in a register.)
//          Address:   The instruction has the specified target.
//
//      When copying instructions, XHookCopyInstruction insures that any
//      targets remain constant.  It does so by adjusting any IP relative
//      offsets.
//

//////////////////////////////////////////////////// X86 and X64 Disassembler.
//
//  Includes full support for all x86 chips prior to the Pentium III.
//
#if defined(XHOOKS_X64) || defined(XHOOKS_X86)

class CXHookDis
{
public:
	CXHookDis(PBYTE *ppbTarget, LONG *plExtra);

	PBYTE   CopyInstruction(PBYTE pbDst, PBYTE pbSrc);
	static BOOL SanityCheckSystem();

public:
	struct COPYENTRY;
	typedef const COPYENTRY * REFCOPYENTRY;

	typedef PBYTE(CXHookDis::* COPYFUNC)(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);

	enum {
		DYNAMIC = 0x1u,
		ADDRESS = 0x2u,
		NOENLARGE = 0x4u,
		RAX = 0x8u,

		SIB = 0x10u,
		RIP = 0x20u,
		NOTSIB = 0x0fu,
	};
	struct COPYENTRY
	{
		ULONG       nOpcode : 8;    // Opcode
		ULONG       nFixedSize : 4;    // Fixed size of opcode
		ULONG       nFixedSize16 : 4;    // Fixed size when 16 bit operand
		ULONG       nModOffset : 4;    // Offset to mod/rm byte (0=none)
		LONG        nRelOffset : 4;    // Offset to relative target.
		LONG        nTargetBack : 4;    // Offset back to absolute or rip target
		ULONG       nFlagBits : 4;    // Flags for DYNAMIC, etc.
		COPYFUNC    pfCopy;                 // Function pointer.
	};

protected:
	// These macros define common uses of nFixedSize..pfCopy.
#define ENTRY_CopyBytes1            1, 1, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes1Dynamic     1, 1, 0, 0, 0, DYNAMIC, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2            2, 2, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2Jump        2, 2, 0, 1, 0, 0, &CXHookDis::CopyBytesJump
#define ENTRY_CopyBytes2CantJump    2, 2, 0, 1, 0, NOENLARGE, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2Dynamic     2, 2, 0, 0, 0, DYNAMIC, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3            3, 3, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Dynamic     3, 3, 0, 0, 0, DYNAMIC, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Or5         5, 3, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Or5Rax      5, 3, 0, 0, 0, RAX, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Or5Target   5, 3, 0, 1, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes5Or7Dynamic  7, 5, 0, 0, 0, DYNAMIC, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Or5Address  5, 3, 0, 0, 0, ADDRESS, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes4            4, 4, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes5            5, 5, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes7            7, 7, 0, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2Mod         2, 2, 1, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2Mod1        3, 3, 1, 0, 1, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes2ModOperand  6, 4, 1, 0, 4, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytes3Mod         3, 3, 2, 0, 0, 0, &CXHookDis::CopyBytes
#define ENTRY_CopyBytesPrefix       1, 1, 0, 0, 0, 0, &CXHookDis::CopyBytesPrefix
#define ENTRY_CopyBytesRax          1, 1, 0, 0, 0, 0, &CXHookDis::CopyBytesRax
#define ENTRY_Copy0F                1, 1, 0, 0, 0, 0, &CXHookDis::Copy0F
#define ENTRY_Copy66                1, 1, 0, 0, 0, 0, &CXHookDis::Copy66
#define ENTRY_Copy67                1, 1, 0, 0, 0, 0, &CXHookDis::Copy67
#define ENTRY_CopyF6                0, 0, 0, 0, 0, 0, &CXHookDis::CopyF6
#define ENTRY_CopyF7                0, 0, 0, 0, 0, 0, &CXHookDis::CopyF7
#define ENTRY_CopyFF                0, 0, 0, 0, 0, 0, &CXHookDis::CopyFF
#define ENTRY_Invalid               1, 1, 0, 0, 0, 0, &CXHookDis::Invalid
#define ENTRY_End                   0, 0, 0, 0, 0, 0, NULL

	PBYTE CopyBytes(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyBytesPrefix(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyBytesRax(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyBytesJump(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);

	PBYTE Invalid(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);

	PBYTE AdjustTarget(PBYTE pbDst, PBYTE pbSrc, LONG cbOp,
		LONG cbTargetOffset, LONG cbTargetSize);

protected:
	PBYTE Copy0F(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE Copy66(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE Copy67(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyF6(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyF7(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);
	PBYTE CopyFF(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc);

protected:
	static const COPYENTRY  s_rceCopyTable[257];
	static const COPYENTRY  s_rceCopyTable0F[257];
	static const BYTE       s_rbModRm[256];

protected:
	BOOL                m_bOperandOverride;
	BOOL                m_bAddressOverride;
	BOOL                m_bRaxOverride;

	PBYTE *             m_ppbTarget;
	LONG *              m_plExtra;

	LONG                m_lScratchExtra;
	PBYTE               m_pbScratchTarget;
	BYTE                m_rbScratchDst[64];
};

PVOID WINAPI XHookCopyInstruction(PVOID pDst,
	PVOID *ppDstPool,
	PVOID pSrc,
	PVOID *ppTarget,
	LONG *plExtra)
{
	(void)ppDstPool; // x86 & x64 don't use a constant pool.
	CXHookDis oXHookDisasm((PBYTE*)ppTarget, plExtra);
	return oXHookDisasm.CopyInstruction((PBYTE)pDst, (PBYTE)pSrc);
}

/////////////////////////////////////////////////////////// Disassembler Code.
//
CXHookDis::CXHookDis(PBYTE *ppbTarget, LONG *plExtra)
{
	m_bOperandOverride = FALSE;
	m_bAddressOverride = FALSE;
	m_bRaxOverride = FALSE;

	m_ppbTarget = ppbTarget ? ppbTarget : &m_pbScratchTarget;
	m_plExtra = plExtra ? plExtra : &m_lScratchExtra;

	*m_ppbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_NONE;
	*m_plExtra = 0;
}

PBYTE CXHookDis::CopyInstruction(PBYTE pbDst, PBYTE pbSrc)
{
	// Configure scratch areas if real areas are not available.
	if (NULL == pbDst) {
		pbDst = m_rbScratchDst;
	}
	if (NULL == pbSrc) {
		// We can't copy a non-existent instruction.
		SetLastError(ERROR_INVALID_DATA);
		return NULL;
	}

	// Figure out how big the instruction is, do the appropriate copy,
	// and figure out what the target of the instruction is if any.
	//
	REFCOPYENTRY pEntry = &s_rceCopyTable[pbSrc[0]];
	return (this->*pEntry->pfCopy)(pEntry, pbDst, pbSrc);
}

PBYTE CXHookDis::CopyBytes(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
#ifdef XHOOKS_X64
	LONG nBytesFixed = (pEntry->nFlagBits & ADDRESS)
		? (m_bAddressOverride ? 5 : 9)      // For move A0-A3
		: ((pEntry->nFlagBits & RAX)
		? (m_bRaxOverride ? 9 : 5)      // For move B8
		: (m_bOperandOverride ? pEntry->nFixedSize16 : pEntry->nFixedSize));
#else
	LONG nBytesFixed = (pEntry->nFlagBits & ADDRESS)
		? (m_bAddressOverride ? pEntry->nFixedSize16 : pEntry->nFixedSize)
		: (m_bOperandOverride ? pEntry->nFixedSize16 : pEntry->nFixedSize);
#endif

	LONG nBytes = nBytesFixed;
	LONG nRelOffset = pEntry->nRelOffset;
	LONG cbTarget = nBytes - nRelOffset;
	if (pEntry->nModOffset > 0) {
		BYTE bModRm = pbSrc[pEntry->nModOffset];
		BYTE bFlags = s_rbModRm[bModRm];

		nBytes += bFlags & NOTSIB;

		if (bFlags & SIB) {
			BYTE bSib = pbSrc[pEntry->nModOffset + 1];

			if ((bSib & 0x07) == 0x05) {
				if ((bModRm & 0xc0) == 0x00) {
					nBytes += 4;
				}
				else if ((bModRm & 0xc0) == 0x40) {
					nBytes += 1;
				}
				else if ((bModRm & 0xc0) == 0x80) {
					nBytes += 4;
				}
			}
			cbTarget = nBytes - nRelOffset;
		}
		else if (bFlags & RIP) {
#ifdef XHOOKS_X64
			nBytesFixed = nBytes;
			nRelOffset = nBytes - (4 + pEntry->nTargetBack);
			cbTarget = 4;
#endif
		}
	}
	CopyMemory(pbDst, pbSrc, nBytes);

	if (nRelOffset) {
		*m_ppbTarget = AdjustTarget(pbDst, pbSrc, nBytesFixed, nRelOffset, cbTarget);
#ifdef XHOOKS_X64
		if (pEntry->nRelOffset == 0) {
			// This is a data target, not a code target, so we shoulnd't return it.
			*m_ppbTarget = NULL;
		}
#endif
	}
	if (pEntry->nFlagBits & NOENLARGE) {
		*m_plExtra = -*m_plExtra;
	}
	if (pEntry->nFlagBits & DYNAMIC) {
		*m_ppbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	}
	return pbSrc + nBytes;
}

PBYTE CXHookDis::CopyBytesPrefix(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	CopyBytes(pEntry, pbDst, pbSrc);

	pEntry = &s_rceCopyTable[pbSrc[1]];
	return (this->*pEntry->pfCopy)(pEntry, pbDst + 1, pbSrc + 1);
}

PBYTE CXHookDis::CopyBytesRax(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	CopyBytes(pEntry, pbDst, pbSrc);

	if (*pbSrc & 0x8) {
		m_bRaxOverride = TRUE;
	}

	pEntry = &s_rceCopyTable[pbSrc[1]];
	return (this->*pEntry->pfCopy)(pEntry, pbDst + 1, pbSrc + 1);
}

PBYTE CXHookDis::CopyBytesJump(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	(void)pEntry;

	PVOID pvSrcAddr = &pbSrc[1];
	PVOID pvDstAddr = NULL;
	LONG_PTR nOldOffset = (LONG_PTR)*(CHAR*&)pvSrcAddr;
	LONG_PTR nNewOffset = 0;

	*m_ppbTarget = pbSrc + 2 + nOldOffset;

	if (pbSrc[0] == 0xeb) {
		pbDst[0] = 0xe9;
		pvDstAddr = &pbDst[1];
		nNewOffset = nOldOffset - ((pbDst - pbSrc) + 3);
		*(LONG*&)pvDstAddr = (LONG)nNewOffset;

		*m_plExtra = 3;
		return pbSrc + 2;
	}

	ASSERT(pbSrc[0] >= 0x70 && pbSrc[0] <= 0x7f);

	pbDst[0] = 0x0f;
	pbDst[1] = 0x80 | (pbSrc[0] & 0xf);
	pvDstAddr = &pbDst[2];
	nNewOffset = nOldOffset - ((pbDst - pbSrc) + 4);
	*(LONG*&)pvDstAddr = (LONG)nNewOffset;

	*m_plExtra = 4;
	return pbSrc + 2;
}

PBYTE CXHookDis::AdjustTarget(PBYTE pbDst, PBYTE pbSrc, LONG cbOp,
	LONG cbTargetOffset, LONG cbTargetSize)
{
	PBYTE pbTarget = NULL;
	PVOID pvTargetAddr = &pbDst[cbTargetOffset];
	LONG_PTR nOldOffset = 0;

	switch (cbTargetSize) {
	case 1:
		nOldOffset = (LONG_PTR)*(CHAR*&)pvTargetAddr;
		break;
	case 2:
		nOldOffset = (LONG_PTR)*(SHORT*&)pvTargetAddr;
		break;
	case 4:
		nOldOffset = (LONG_PTR)*(LONG*&)pvTargetAddr;
		break;
	case 8:
		nOldOffset = (LONG_PTR)*(LONG_PTR*&)pvTargetAddr;
		break;
	default:
		ASSERT(!"cbTargetSize is invalid.");
		break;
	}

	pbTarget = pbSrc + cbOp + nOldOffset;
	LONG_PTR nNewOffset = nOldOffset - (pbDst - pbSrc);

	switch (cbTargetSize) {
	case 1:
		*(CHAR*&)pvTargetAddr = (CHAR)nNewOffset;
		if (nNewOffset < SCHAR_MIN || nNewOffset > SCHAR_MAX) {
			*m_plExtra = sizeof(ULONG) - 1;
		}
		break;
	case 2:
		*(SHORT*&)pvTargetAddr = (SHORT)nNewOffset;
		if (nNewOffset < SHRT_MIN || nNewOffset > SHRT_MAX) {
			*m_plExtra = sizeof(ULONG) - 2;
		}
		break;
	case 4:
		*(LONG*&)pvTargetAddr = (LONG)nNewOffset;
		if (nNewOffset < LONG_MIN || nNewOffset > LONG_MAX) {
			*m_plExtra = sizeof(ULONG) - 4;
		}
		break;
	case 8:
		*(LONG_PTR*&)pvTargetAddr = (LONG_PTR)nNewOffset;
		break;
	}
	ASSERT(pbDst + cbOp + nNewOffset == pbTarget);
	return pbTarget;
}

PBYTE CXHookDis::Invalid(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	(void)pbDst;
	(void)pEntry;
	ASSERT(!"Invalid Instruction");
	return pbSrc + 1;
}

////////////////////////////////////////////////////// Individual Bytes Codes.
//
PBYTE CXHookDis::Copy0F(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	CopyBytes(pEntry, pbDst, pbSrc);

	pEntry = &s_rceCopyTable0F[pbSrc[1]];
	return (this->*pEntry->pfCopy)(pEntry, pbDst + 1, pbSrc + 1);
}

PBYTE CXHookDis::Copy66(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{   // Operand-size override prefix
	m_bOperandOverride = TRUE;
	return CopyBytesPrefix(pEntry, pbDst, pbSrc);
}

PBYTE CXHookDis::Copy67(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{   // Address size override prefix
	m_bAddressOverride = TRUE;
	return CopyBytesPrefix(pEntry, pbDst, pbSrc);
}

PBYTE CXHookDis::CopyF6(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	(void)pEntry;

	// TEST BYTE /0
	if (0x00 == (0x38 & pbSrc[1])) {    // reg(bits 543) of ModR/M == 0
		const COPYENTRY ce = { 0xf6, ENTRY_CopyBytes2Mod1 };
		return (this->*ce.pfCopy)(&ce, pbDst, pbSrc);
	}
	// DIV /6
	// IDIV /7
	// IMUL /5
	// MUL /4
	// NEG /3
	// NOT /2

	const COPYENTRY ce = { 0xf6, ENTRY_CopyBytes2Mod };
	return (this->*ce.pfCopy)(&ce, pbDst, pbSrc);
}

PBYTE CXHookDis::CopyF7(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{
	(void)pEntry;

	// TEST WORD /0
	if (0x00 == (0x38 & pbSrc[1])) {    // reg(bits 543) of ModR/M == 0
		const COPYENTRY ce = { 0xf7, ENTRY_CopyBytes2ModOperand };
		return (this->*ce.pfCopy)(&ce, pbDst, pbSrc);
	}

	// DIV /6
	// IDIV /7
	// IMUL /5
	// MUL /4
	// NEG /3
	// NOT /2
	const COPYENTRY ce = { 0xf7, ENTRY_CopyBytes2Mod };
	return (this->*ce.pfCopy)(&ce, pbDst, pbSrc);
}

PBYTE CXHookDis::CopyFF(REFCOPYENTRY pEntry, PBYTE pbDst, PBYTE pbSrc)
{   // CALL /2
	// CALL /3
	// INC /0
	// JMP /4
	// JMP /5
	// PUSH /6
	(void)pEntry;

	if (0x15 == pbSrc[1] || 0x25 == pbSrc[1]) {         // CALL [], JMP []
#ifdef XHOOKS_X64
		INT32 offset = *(INT32 *)&pbSrc[2];
		PBYTE *ppbTarget = (PBYTE *)(pbSrc + 6 + offset);
		*m_ppbTarget = *ppbTarget;
#else
		PBYTE *ppbTarget = *(PBYTE**)&pbSrc[2];
		*m_ppbTarget = *ppbTarget;
#endif
	}
	else if (0x10 == (0x38 & pbSrc[1]) || // CALL /2 --> reg(bits 543) of ModR/M == 010
		0x18 == (0x38 & pbSrc[1]) || // CALL /3 --> reg(bits 543) of ModR/M == 011
		0x20 == (0x38 & pbSrc[1]) || // JMP /4 --> reg(bits 543) of ModR/M == 100
		0x28 == (0x38 & pbSrc[1])    // JMP /5 --> reg(bits 543) of ModR/M == 101
		) {
		*m_ppbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	}
	const COPYENTRY ce = { 0xff, ENTRY_CopyBytes2Mod };
	return (this->*ce.pfCopy)(&ce, pbDst, pbSrc);
}

///////////////////////////////////////////////////////// Disassembler Tables.
//
const BYTE CXHookDis::s_rbModRm[256] = {
	0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, 0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, // 0x
	0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, 0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, // 1x
	0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, 0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, // 2x
	0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, 0, 0, 0, 0, SIB | 1, RIP | 4, 0, 0, // 3x
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,                 // 4x
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,                 // 5x
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,                 // 6x
	1, 1, 1, 1, 2, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 1,                 // 7x
	4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4,                 // 8x
	4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4,                 // 9x
	4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4,                 // Ax
	4, 4, 4, 4, 5, 4, 4, 4, 4, 4, 4, 4, 5, 4, 4, 4,                 // Bx
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // Cx
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // Dx
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,                 // Ex
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0                  // Fx
};

const CXHookDis::COPYENTRY CXHookDis::s_rceCopyTable[257] =
{
	{ 0x00, ENTRY_CopyBytes2Mod },                      // ADD /r
	{ 0x01, ENTRY_CopyBytes2Mod },                      // ADD /r
	{ 0x02, ENTRY_CopyBytes2Mod },                      // ADD /r
	{ 0x03, ENTRY_CopyBytes2Mod },                      // ADD /r
	{ 0x04, ENTRY_CopyBytes2 },                         // ADD ib
	{ 0x05, ENTRY_CopyBytes3Or5 },                      // ADD iw
	{ 0x06, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x07, ENTRY_CopyBytes1 },                         // POP
	{ 0x08, ENTRY_CopyBytes2Mod },                      // OR /r
	{ 0x09, ENTRY_CopyBytes2Mod },                      // OR /r
	{ 0x0A, ENTRY_CopyBytes2Mod },                      // OR /r
	{ 0x0B, ENTRY_CopyBytes2Mod },                      // OR /r
	{ 0x0C, ENTRY_CopyBytes2 },                         // OR ib
	{ 0x0D, ENTRY_CopyBytes3Or5 },                      // OR iw
	{ 0x0E, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x0F, ENTRY_Copy0F },                             // Extension Ops
	{ 0x10, ENTRY_CopyBytes2Mod },                      // ADC /r
	{ 0x11, ENTRY_CopyBytes2Mod },                      // ADC /r
	{ 0x12, ENTRY_CopyBytes2Mod },                      // ADC /r
	{ 0x13, ENTRY_CopyBytes2Mod },                      // ADC /r
	{ 0x14, ENTRY_CopyBytes2 },                         // ADC ib
	{ 0x15, ENTRY_CopyBytes3Or5 },                      // ADC id
	{ 0x16, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x17, ENTRY_CopyBytes1 },                         // POP
	{ 0x18, ENTRY_CopyBytes2Mod },                      // SBB /r
	{ 0x19, ENTRY_CopyBytes2Mod },                      // SBB /r
	{ 0x1A, ENTRY_CopyBytes2Mod },                      // SBB /r
	{ 0x1B, ENTRY_CopyBytes2Mod },                      // SBB /r
	{ 0x1C, ENTRY_CopyBytes2 },                         // SBB ib
	{ 0x1D, ENTRY_CopyBytes3Or5 },                      // SBB id
	{ 0x1E, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x1F, ENTRY_CopyBytes1 },                         // POP
	{ 0x20, ENTRY_CopyBytes2Mod },                      // AND /r
	{ 0x21, ENTRY_CopyBytes2Mod },                      // AND /r
	{ 0x22, ENTRY_CopyBytes2Mod },                      // AND /r
	{ 0x23, ENTRY_CopyBytes2Mod },                      // AND /r
	{ 0x24, ENTRY_CopyBytes2 },                         // AND ib
	{ 0x25, ENTRY_CopyBytes3Or5 },                      // AND id
	{ 0x26, ENTRY_CopyBytesPrefix },                    // ES prefix
	{ 0x27, ENTRY_CopyBytes1 },                         // DAA
	{ 0x28, ENTRY_CopyBytes2Mod },                      // SUB /r
	{ 0x29, ENTRY_CopyBytes2Mod },                      // SUB /r
	{ 0x2A, ENTRY_CopyBytes2Mod },                      // SUB /r
	{ 0x2B, ENTRY_CopyBytes2Mod },                      // SUB /r
	{ 0x2C, ENTRY_CopyBytes2 },                         // SUB ib
	{ 0x2D, ENTRY_CopyBytes3Or5 },                      // SUB id
	{ 0x2E, ENTRY_CopyBytesPrefix },                    // CS prefix
	{ 0x2F, ENTRY_CopyBytes1 },                         // DAS
	{ 0x30, ENTRY_CopyBytes2Mod },                      // XOR /r
	{ 0x31, ENTRY_CopyBytes2Mod },                      // XOR /r
	{ 0x32, ENTRY_CopyBytes2Mod },                      // XOR /r
	{ 0x33, ENTRY_CopyBytes2Mod },                      // XOR /r
	{ 0x34, ENTRY_CopyBytes2 },                         // XOR ib
	{ 0x35, ENTRY_CopyBytes3Or5 },                      // XOR id
	{ 0x36, ENTRY_CopyBytesPrefix },                    // SS prefix
	{ 0x37, ENTRY_CopyBytes1 },                         // AAA
	{ 0x38, ENTRY_CopyBytes2Mod },                      // CMP /r
	{ 0x39, ENTRY_CopyBytes2Mod },                      // CMP /r
	{ 0x3A, ENTRY_CopyBytes2Mod },                      // CMP /r
	{ 0x3B, ENTRY_CopyBytes2Mod },                      // CMP /r
	{ 0x3C, ENTRY_CopyBytes2 },                         // CMP ib
	{ 0x3D, ENTRY_CopyBytes3Or5 },                      // CMP id
	{ 0x3E, ENTRY_CopyBytesPrefix },                    // DS prefix
	{ 0x3F, ENTRY_CopyBytes1 },                         // AAS
#ifdef XHOOKS_X64 // For Rax Prefix
	{ 0x40, ENTRY_CopyBytesRax },                       // Rax
	{ 0x41, ENTRY_CopyBytesRax },                       // Rax
	{ 0x42, ENTRY_CopyBytesRax },                       // Rax
	{ 0x43, ENTRY_CopyBytesRax },                       // Rax
	{ 0x44, ENTRY_CopyBytesRax },                       // Rax
	{ 0x45, ENTRY_CopyBytesRax },                       // Rax
	{ 0x46, ENTRY_CopyBytesRax },                       // Rax
	{ 0x47, ENTRY_CopyBytesRax },                       // Rax
	{ 0x48, ENTRY_CopyBytesRax },                       // Rax
	{ 0x49, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4A, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4B, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4C, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4D, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4E, ENTRY_CopyBytesRax },                       // Rax
	{ 0x4F, ENTRY_CopyBytesRax },                       // Rax
#else
	{ 0x40, ENTRY_CopyBytes1 },                         // INC
	{ 0x41, ENTRY_CopyBytes1 },                         // INC
	{ 0x42, ENTRY_CopyBytes1 },                         // INC
	{ 0x43, ENTRY_CopyBytes1 },                         // INC
	{ 0x44, ENTRY_CopyBytes1 },                         // INC
	{ 0x45, ENTRY_CopyBytes1 },                         // INC
	{ 0x46, ENTRY_CopyBytes1 },                         // INC
	{ 0x47, ENTRY_CopyBytes1 },                         // INC
	{ 0x48, ENTRY_CopyBytes1 },                         // DEC
	{ 0x49, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4A, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4B, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4C, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4D, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4E, ENTRY_CopyBytes1 },                         // DEC
	{ 0x4F, ENTRY_CopyBytes1 },                         // DEC
#endif
	{ 0x50, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x51, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x52, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x53, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x54, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x55, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x56, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x57, ENTRY_CopyBytes1 },                         // PUSH
	{ 0x58, ENTRY_CopyBytes1 },                         // POP
	{ 0x59, ENTRY_CopyBytes1 },                         // POP
	{ 0x5A, ENTRY_CopyBytes1 },                         // POP
	{ 0x5B, ENTRY_CopyBytes1 },                         // POP
	{ 0x5C, ENTRY_CopyBytes1 },                         // POP
	{ 0x5D, ENTRY_CopyBytes1 },                         // POP
	{ 0x5E, ENTRY_CopyBytes1 },                         // POP
	{ 0x5F, ENTRY_CopyBytes1 },                         // POP
	{ 0x60, ENTRY_CopyBytes1 },                         // PUSHAD
	{ 0x61, ENTRY_CopyBytes1 },                         // POPAD
	{ 0x62, ENTRY_CopyBytes2Mod },                      // BOUND /r
	{ 0x63, ENTRY_CopyBytes2Mod },                      // ARPL /r
	{ 0x64, ENTRY_CopyBytesPrefix },                    // FS prefix
	{ 0x65, ENTRY_CopyBytesPrefix },                    // GS prefix
	{ 0x66, ENTRY_Copy66 },                             // Operand Prefix
	{ 0x67, ENTRY_Copy67 },                             // Address Prefix
	{ 0x68, ENTRY_CopyBytes3Or5 },                      // PUSH
	{ 0x69, ENTRY_CopyBytes2ModOperand },               //
	{ 0x6A, ENTRY_CopyBytes2 },                         // PUSH
	{ 0x6B, ENTRY_CopyBytes2Mod1 },                     // IMUL /r ib
	{ 0x6C, ENTRY_CopyBytes1 },                         // INS
	{ 0x6D, ENTRY_CopyBytes1 },                         // INS
	{ 0x6E, ENTRY_CopyBytes1 },                         // OUTS/OUTSB
	{ 0x6F, ENTRY_CopyBytes1 },                         // OUTS/OUTSW
	{ 0x70, ENTRY_CopyBytes2Jump },                     // JO           // 0f80
	{ 0x71, ENTRY_CopyBytes2Jump },                     // JNO          // 0f81
	{ 0x72, ENTRY_CopyBytes2Jump },                     // JB/JC/JNAE   // 0f82
	{ 0x73, ENTRY_CopyBytes2Jump },                     // JAE/JNB/JNC  // 0f83
	{ 0x74, ENTRY_CopyBytes2Jump },                     // JE/JZ        // 0f84
	{ 0x75, ENTRY_CopyBytes2Jump },                     // JNE/JNZ      // 0f85
	{ 0x76, ENTRY_CopyBytes2Jump },                     // JBE/JNA      // 0f86
	{ 0x77, ENTRY_CopyBytes2Jump },                     // JA/JNBE      // 0f87
	{ 0x78, ENTRY_CopyBytes2Jump },                     // JS           // 0f88
	{ 0x79, ENTRY_CopyBytes2Jump },                     // JNS          // 0f89
	{ 0x7A, ENTRY_CopyBytes2Jump },                     // JP/JPE       // 0f8a
	{ 0x7B, ENTRY_CopyBytes2Jump },                     // JNP/JPO      // 0f8b
	{ 0x7C, ENTRY_CopyBytes2Jump },                     // JL/JNGE      // 0f8c
	{ 0x7D, ENTRY_CopyBytes2Jump },                     // JGE/JNL      // 0f8d
	{ 0x7E, ENTRY_CopyBytes2Jump },                     // JLE/JNG      // 0f8e
	{ 0x7F, ENTRY_CopyBytes2Jump },                     // JG/JNLE      // 0f8f
	{ 0x80, ENTRY_CopyBytes2Mod1 },                     // ADC/2 ib, etc.s
	{ 0x81, ENTRY_CopyBytes2ModOperand },               //
	{ 0x82, ENTRY_CopyBytes2 },                         // MOV al,x
	{ 0x83, ENTRY_CopyBytes2Mod1 },                     // ADC/2 ib, etc.
	{ 0x84, ENTRY_CopyBytes2Mod },                      // TEST /r
	{ 0x85, ENTRY_CopyBytes2Mod },                      // TEST /r
	{ 0x86, ENTRY_CopyBytes2Mod },                      // XCHG /r @todo
	{ 0x87, ENTRY_CopyBytes2Mod },                      // XCHG /r @todo
	{ 0x88, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x89, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x8A, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x8B, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x8C, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x8D, ENTRY_CopyBytes2Mod },                      // LEA /r
	{ 0x8E, ENTRY_CopyBytes2Mod },                      // MOV /r
	{ 0x8F, ENTRY_CopyBytes2Mod },                      // POP /0
	{ 0x90, ENTRY_CopyBytes1 },                         // NOP
	{ 0x91, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x92, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x93, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x94, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x95, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x96, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x97, ENTRY_CopyBytes1 },                         // XCHG
	{ 0x98, ENTRY_CopyBytes1 },                         // CWDE
	{ 0x99, ENTRY_CopyBytes1 },                         // CDQ
	{ 0x9A, ENTRY_CopyBytes5Or7Dynamic },               // CALL cp
	{ 0x9B, ENTRY_CopyBytes1 },                         // WAIT/FWAIT
	{ 0x9C, ENTRY_CopyBytes1 },                         // PUSHFD
	{ 0x9D, ENTRY_CopyBytes1 },                         // POPFD
	{ 0x9E, ENTRY_CopyBytes1 },                         // SAHF
	{ 0x9F, ENTRY_CopyBytes1 },                         // LAHF
	{ 0xA0, ENTRY_CopyBytes3Or5Address },               // MOV
	{ 0xA1, ENTRY_CopyBytes3Or5Address },               // MOV
	{ 0xA2, ENTRY_CopyBytes3Or5Address },               // MOV
	{ 0xA3, ENTRY_CopyBytes3Or5Address },               // MOV
	{ 0xA4, ENTRY_CopyBytes1 },                         // MOVS
	{ 0xA5, ENTRY_CopyBytes1 },                         // MOVS/MOVSD
	{ 0xA6, ENTRY_CopyBytes1 },                         // CMPS/CMPSB
	{ 0xA7, ENTRY_CopyBytes1 },                         // CMPS/CMPSW
	{ 0xA8, ENTRY_CopyBytes2 },                         // TEST
	{ 0xA9, ENTRY_CopyBytes3Or5 },                      // TEST
	{ 0xAA, ENTRY_CopyBytes1 },                         // STOS/STOSB
	{ 0xAB, ENTRY_CopyBytes1 },                         // STOS/STOSW
	{ 0xAC, ENTRY_CopyBytes1 },                         // LODS/LODSB
	{ 0xAD, ENTRY_CopyBytes1 },                         // LODS/LODSW
	{ 0xAE, ENTRY_CopyBytes1 },                         // SCAS/SCASB
	{ 0xAF, ENTRY_CopyBytes1 },                         // SCAS/SCASD
	{ 0xB0, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB1, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB2, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB3, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB4, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB5, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB6, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB7, ENTRY_CopyBytes2 },                         // MOV B0+rb
	{ 0xB8, ENTRY_CopyBytes3Or5Rax },                   // MOV B8+rb
	{ 0xB9, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBA, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBB, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBC, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBD, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBE, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xBF, ENTRY_CopyBytes3Or5 },                      // MOV B8+rb
	{ 0xC0, ENTRY_CopyBytes2Mod1 },                     // RCL/2 ib, etc.
	{ 0xC1, ENTRY_CopyBytes2Mod1 },                     // RCL/2 ib, etc.
	{ 0xC2, ENTRY_CopyBytes3 },                         // RET
	{ 0xC3, ENTRY_CopyBytes1 },                         // RET
	{ 0xC4, ENTRY_CopyBytes2Mod },                      // LES
	{ 0xC5, ENTRY_CopyBytes2Mod },                      // LDS
	{ 0xC6, ENTRY_CopyBytes2Mod1 },                     // MOV
	{ 0xC7, ENTRY_CopyBytes2ModOperand },               // MOV
	{ 0xC8, ENTRY_CopyBytes4 },                         // ENTER
	{ 0xC9, ENTRY_CopyBytes1 },                         // LEAVE
	{ 0xCA, ENTRY_CopyBytes3Dynamic },                  // RET
	{ 0xCB, ENTRY_CopyBytes1Dynamic },                  // RET
	{ 0xCC, ENTRY_CopyBytes1Dynamic },                  // INT 3
	{ 0xCD, ENTRY_CopyBytes2Dynamic },                  // INT ib
	{ 0xCE, ENTRY_CopyBytes1Dynamic },                  // INTO
	{ 0xCF, ENTRY_CopyBytes1Dynamic },                  // IRET
	{ 0xD0, ENTRY_CopyBytes2Mod },                      // RCL/2, etc.
	{ 0xD1, ENTRY_CopyBytes2Mod },                      // RCL/2, etc.
	{ 0xD2, ENTRY_CopyBytes2Mod },                      // RCL/2, etc.
	{ 0xD3, ENTRY_CopyBytes2Mod },                      // RCL/2, etc.
	{ 0xD4, ENTRY_CopyBytes2 },                         // AAM
	{ 0xD5, ENTRY_CopyBytes2 },                         // AAD
	{ 0xD6, ENTRY_Invalid },                            //
	{ 0xD7, ENTRY_CopyBytes1 },                         // XLAT/XLATB
	{ 0xD8, ENTRY_CopyBytes2Mod },                      // FADD, etc.
	{ 0xD9, ENTRY_CopyBytes2Mod },                      // F2XM1, etc.
	{ 0xDA, ENTRY_CopyBytes2Mod },                      // FLADD, etc.
	{ 0xDB, ENTRY_CopyBytes2Mod },                      // FCLEX, etc.
	{ 0xDC, ENTRY_CopyBytes2Mod },                      // FADD/0, etc.
	{ 0xDD, ENTRY_CopyBytes2Mod },                      // FFREE, etc.
	{ 0xDE, ENTRY_CopyBytes2Mod },                      // FADDP, etc.
	{ 0xDF, ENTRY_CopyBytes2Mod },                      // FBLD/4, etc.
	{ 0xE0, ENTRY_CopyBytes2CantJump },                 // LOOPNE cb
	{ 0xE1, ENTRY_CopyBytes2CantJump },                 // LOOPE cb
	{ 0xE2, ENTRY_CopyBytes2CantJump },                 // LOOP cb
	{ 0xE3, ENTRY_CopyBytes2Jump },                     // JCXZ/JECXZ
	{ 0xE4, ENTRY_CopyBytes2 },                         // IN ib
	{ 0xE5, ENTRY_CopyBytes2 },                         // IN id
	{ 0xE6, ENTRY_CopyBytes2 },                         // OUT ib
	{ 0xE7, ENTRY_CopyBytes2 },                         // OUT ib
	{ 0xE8, ENTRY_CopyBytes3Or5Target },                // CALL cd
	{ 0xE9, ENTRY_CopyBytes3Or5Target },                // JMP cd
	{ 0xEA, ENTRY_CopyBytes5Or7Dynamic },               // JMP cp
	{ 0xEB, ENTRY_CopyBytes2Jump },                     // JMP cb
	{ 0xEC, ENTRY_CopyBytes1 },                         // IN ib
	{ 0xED, ENTRY_CopyBytes1 },                         // IN id
	{ 0xEE, ENTRY_CopyBytes1 },                         // OUT
	{ 0xEF, ENTRY_CopyBytes1 },                         // OUT
	{ 0xF0, ENTRY_CopyBytesPrefix },                    // LOCK prefix
	{ 0xF1, ENTRY_Invalid },                            //
	{ 0xF2, ENTRY_CopyBytesPrefix },                    // REPNE prefix
	{ 0xF3, ENTRY_CopyBytesPrefix },                    // REPE prefix
	{ 0xF4, ENTRY_CopyBytes1 },                         // HLT
	{ 0xF5, ENTRY_CopyBytes1 },                         // CMC
	{ 0xF6, ENTRY_CopyF6 },                             // TEST/0, DIV/6
	{ 0xF7, ENTRY_CopyF7 },                             // TEST/0, DIV/6
	{ 0xF8, ENTRY_CopyBytes1 },                         // CLC
	{ 0xF9, ENTRY_CopyBytes1 },                         // STC
	{ 0xFA, ENTRY_CopyBytes1 },                         // CLI
	{ 0xFB, ENTRY_CopyBytes1 },                         // STI
	{ 0xFC, ENTRY_CopyBytes1 },                         // CLD
	{ 0xFD, ENTRY_CopyBytes1 },                         // STD
	{ 0xFE, ENTRY_CopyBytes2Mod },                      // DEC/1,INC/0
	{ 0xFF, ENTRY_CopyFF },                             // CALL/2
	{ 0, ENTRY_End },
};

const CXHookDis::COPYENTRY CXHookDis::s_rceCopyTable0F[257] =
{
	{ 0x00, ENTRY_CopyBytes2Mod },                      // LLDT/2, etc.
	{ 0x01, ENTRY_CopyBytes2Mod },                      // INVLPG/7, etc.
	{ 0x02, ENTRY_CopyBytes2Mod },                      // LAR/r
	{ 0x03, ENTRY_CopyBytes2Mod },                      // LSL/r
	{ 0x04, ENTRY_Invalid },                            // _04
	{ 0x05, ENTRY_Invalid },                            // _05
	{ 0x06, ENTRY_CopyBytes2 },                         // CLTS
	{ 0x07, ENTRY_Invalid },                            // _07
	{ 0x08, ENTRY_CopyBytes2 },                         // INVD
	{ 0x09, ENTRY_CopyBytes2 },                         // WBINVD
	{ 0x0A, ENTRY_Invalid },                            // _0A
	{ 0x0B, ENTRY_CopyBytes2 },                         // UD2
	{ 0x0C, ENTRY_Invalid },                            // _0C
	{ 0x0D, ENTRY_CopyBytes2Mod },                      // PREFETCH
	{ 0x0E, ENTRY_CopyBytes2 },                         // FEMMS
	{ 0x0F, ENTRY_CopyBytes3Mod },                      // 3DNow Opcodes
	{ 0x10, ENTRY_CopyBytes2Mod },                      // MOVSS MOVUPD MOVSD
	{ 0x11, ENTRY_CopyBytes2Mod },                      // MOVSS MOVUPD MOVSD
	{ 0x12, ENTRY_CopyBytes2Mod },                      // MOVLPD
	{ 0x13, ENTRY_CopyBytes2Mod },                      // MOVLPD
	{ 0x14, ENTRY_CopyBytes2Mod },                      // UNPCKLPD
	{ 0x15, ENTRY_CopyBytes2Mod },                      // UNPCKHPD
	{ 0x16, ENTRY_CopyBytes2Mod },                      // MOVHPD
	{ 0x17, ENTRY_CopyBytes2Mod },                      // MOVHPD
	{ 0x18, ENTRY_CopyBytes2Mod },                      // PREFETCHINTA...
	{ 0x19, ENTRY_Invalid },                            // _19
	{ 0x1A, ENTRY_Invalid },                            // _1A
	{ 0x1B, ENTRY_Invalid },                            // _1B
	{ 0x1C, ENTRY_Invalid },                            // _1C
	{ 0x1D, ENTRY_Invalid },                            // _1D
	{ 0x1E, ENTRY_Invalid },                            // _1E
	{ 0x1F, ENTRY_CopyBytes2Mod },                      // NOP/r
	{ 0x20, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x21, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x22, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x23, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x24, ENTRY_Invalid },                            // _24
	{ 0x25, ENTRY_Invalid },                            // _25
	{ 0x26, ENTRY_Invalid },                            // _26
	{ 0x27, ENTRY_Invalid },                            // _27
	{ 0x28, ENTRY_CopyBytes2Mod },                      // MOVAPS MOVAPD
	{ 0x29, ENTRY_CopyBytes2Mod },                      // MOVAPS MOVAPD
	{ 0x2A, ENTRY_CopyBytes2Mod },                      // CVPI2PS &
	{ 0x2B, ENTRY_CopyBytes2Mod },                      // MOVNTPS MOVNTPD
	{ 0x2C, ENTRY_CopyBytes2Mod },                      // CVTTPS2PI &
	{ 0x2D, ENTRY_CopyBytes2Mod },                      // CVTPS2PI &
	{ 0x2E, ENTRY_CopyBytes2Mod },                      // UCOMISS UCOMISD
	{ 0x2F, ENTRY_CopyBytes2Mod },                      // COMISS COMISD
	{ 0x30, ENTRY_CopyBytes2 },                         // WRMSR
	{ 0x31, ENTRY_CopyBytes2 },                         // RDTSC
	{ 0x32, ENTRY_CopyBytes2 },                         // RDMSR
	{ 0x33, ENTRY_CopyBytes2 },                         // RDPMC
	{ 0x34, ENTRY_CopyBytes2 },                         // SYSENTER
	{ 0x35, ENTRY_CopyBytes2 },                         // SYSEXIT
	{ 0x36, ENTRY_Invalid },                            // _36
	{ 0x37, ENTRY_Invalid },                            // _37
	{ 0x38, ENTRY_Invalid },                            // _38
	{ 0x39, ENTRY_Invalid },                            // _39
	{ 0x3A, ENTRY_Invalid },                            // _3A
	{ 0x3B, ENTRY_Invalid },                            // _3B
	{ 0x3C, ENTRY_Invalid },                            // _3C
	{ 0x3D, ENTRY_Invalid },                            // _3D
	{ 0x3E, ENTRY_Invalid },                            // _3E
	{ 0x3F, ENTRY_Invalid },                            // _3F
	{ 0x40, ENTRY_CopyBytes2Mod },                      // CMOVO (0F 40)
	{ 0x41, ENTRY_CopyBytes2Mod },                      // CMOVNO (0F 41)
	{ 0x42, ENTRY_CopyBytes2Mod },                      // CMOVB & CMOVNE (0F 42)
	{ 0x43, ENTRY_CopyBytes2Mod },                      // CMOVAE & CMOVNB (0F 43)
	{ 0x44, ENTRY_CopyBytes2Mod },                      // CMOVE & CMOVZ (0F 44)
	{ 0x45, ENTRY_CopyBytes2Mod },                      // CMOVNE & CMOVNZ (0F 45)
	{ 0x46, ENTRY_CopyBytes2Mod },                      // CMOVBE & CMOVNA (0F 46)
	{ 0x47, ENTRY_CopyBytes2Mod },                      // CMOVA & CMOVNBE (0F 47)
	{ 0x48, ENTRY_CopyBytes2Mod },                      // CMOVS (0F 48)
	{ 0x49, ENTRY_CopyBytes2Mod },                      // CMOVNS (0F 49)
	{ 0x4A, ENTRY_CopyBytes2Mod },                      // CMOVP & CMOVPE (0F 4A)
	{ 0x4B, ENTRY_CopyBytes2Mod },                      // CMOVNP & CMOVPO (0F 4B)
	{ 0x4C, ENTRY_CopyBytes2Mod },                      // CMOVL & CMOVNGE (0F 4C)
	{ 0x4D, ENTRY_CopyBytes2Mod },                      // CMOVGE & CMOVNL (0F 4D)
	{ 0x4E, ENTRY_CopyBytes2Mod },                      // CMOVLE & CMOVNG (0F 4E)
	{ 0x4F, ENTRY_CopyBytes2Mod },                      // CMOVG & CMOVNLE (0F 4F)
	{ 0x50, ENTRY_CopyBytes2Mod },                      // MOVMSKPD MOVMSKPD
	{ 0x51, ENTRY_CopyBytes2Mod },                      // SQRTPS &
	{ 0x52, ENTRY_CopyBytes2Mod },                      // RSQRTTS RSQRTPS
	{ 0x53, ENTRY_CopyBytes2Mod },                      // RCPPS RCPSS
	{ 0x54, ENTRY_CopyBytes2Mod },                      // ANDPS ANDPD
	{ 0x55, ENTRY_CopyBytes2Mod },                      // ANDNPS ANDNPD
	{ 0x56, ENTRY_CopyBytes2Mod },                      // ORPS ORPD
	{ 0x57, ENTRY_CopyBytes2Mod },                      // XORPS XORPD
	{ 0x58, ENTRY_CopyBytes2Mod },                      // ADDPS &
	{ 0x59, ENTRY_CopyBytes2Mod },                      // MULPS &
	{ 0x5A, ENTRY_CopyBytes2Mod },                      // CVTPS2PD &
	{ 0x5B, ENTRY_CopyBytes2Mod },                      // CVTDQ2PS &
	{ 0x5C, ENTRY_CopyBytes2Mod },                      // SUBPS &
	{ 0x5D, ENTRY_CopyBytes2Mod },                      // MINPS &
	{ 0x5E, ENTRY_CopyBytes2Mod },                      // DIVPS &
	{ 0x5F, ENTRY_CopyBytes2Mod },                      // MASPS &
	{ 0x60, ENTRY_CopyBytes2Mod },                      // PUNPCKLBW/r
	{ 0x61, ENTRY_CopyBytes2Mod },                      // PUNPCKLWD/r
	{ 0x62, ENTRY_CopyBytes2Mod },                      // PUNPCKLWD/r
	{ 0x63, ENTRY_CopyBytes2Mod },                      // PACKSSWB/r
	{ 0x64, ENTRY_CopyBytes2Mod },                      // PCMPGTB/r
	{ 0x65, ENTRY_CopyBytes2Mod },                      // PCMPGTW/r
	{ 0x66, ENTRY_CopyBytes2Mod },                      // PCMPGTD/r
	{ 0x67, ENTRY_CopyBytes2Mod },                      // PACKUSWB/r
	{ 0x68, ENTRY_CopyBytes2Mod },                      // PUNPCKHBW/r
	{ 0x69, ENTRY_CopyBytes2Mod },                      // PUNPCKHWD/r
	{ 0x6A, ENTRY_CopyBytes2Mod },                      // PUNPCKHDQ/r
	{ 0x6B, ENTRY_CopyBytes2Mod },                      // PACKSSDW/r
	{ 0x6C, ENTRY_CopyBytes2Mod },                      // PUNPCKLQDQ
	{ 0x6D, ENTRY_CopyBytes2Mod },                      // PUNPCKHQDQ
	{ 0x6E, ENTRY_CopyBytes2Mod },                      // MOVD/r
	{ 0x6F, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x70, ENTRY_CopyBytes2Mod1 },                     // PSHUFW/r ib
	{ 0x71, ENTRY_CopyBytes2Mod1 },                     // PSLLW/6 ib,PSRAW/4 ib,PSRLW/2 ib
	{ 0x72, ENTRY_CopyBytes2Mod1 },                     // PSLLD/6 ib,PSRAD/4 ib,PSRLD/2 ib
	{ 0x73, ENTRY_CopyBytes2Mod1 },                     // PSLLQ/6 ib,PSRLQ/2 ib
	{ 0x74, ENTRY_CopyBytes2Mod },                      // PCMPEQB/r
	{ 0x75, ENTRY_CopyBytes2Mod },                      // PCMPEQW/r
	{ 0x76, ENTRY_CopyBytes2Mod },                      // PCMPEQD/r
	{ 0x77, ENTRY_CopyBytes2 },                         // EMMS
	{ 0x78, ENTRY_Invalid },                            // _78
	{ 0x79, ENTRY_Invalid },                            // _79
	{ 0x7A, ENTRY_Invalid },                            // _7A
	{ 0x7B, ENTRY_Invalid },                            // _7B
	{ 0x7C, ENTRY_Invalid },                            // _7C
	{ 0x7D, ENTRY_Invalid },                            // _7D
	{ 0x7E, ENTRY_CopyBytes2Mod },                      // MOVD/r
	{ 0x7F, ENTRY_CopyBytes2Mod },                      // MOV/r
	{ 0x80, ENTRY_CopyBytes3Or5Target },                // JO
	{ 0x81, ENTRY_CopyBytes3Or5Target },                // JNO
	{ 0x82, ENTRY_CopyBytes3Or5Target },                // JB,JC,JNAE
	{ 0x83, ENTRY_CopyBytes3Or5Target },                // JAE,JNB,JNC
	{ 0x84, ENTRY_CopyBytes3Or5Target },                // JE,JZ,JZ
	{ 0x85, ENTRY_CopyBytes3Or5Target },                // JNE,JNZ
	{ 0x86, ENTRY_CopyBytes3Or5Target },                // JBE,JNA
	{ 0x87, ENTRY_CopyBytes3Or5Target },                // JA,JNBE
	{ 0x88, ENTRY_CopyBytes3Or5Target },                // JS
	{ 0x89, ENTRY_CopyBytes3Or5Target },                // JNS
	{ 0x8A, ENTRY_CopyBytes3Or5Target },                // JP,JPE
	{ 0x8B, ENTRY_CopyBytes3Or5Target },                // JNP,JPO
	{ 0x8C, ENTRY_CopyBytes3Or5Target },                // JL,NGE
	{ 0x8D, ENTRY_CopyBytes3Or5Target },                // JGE,JNL
	{ 0x8E, ENTRY_CopyBytes3Or5Target },                // JLE,JNG
	{ 0x8F, ENTRY_CopyBytes3Or5Target },                // JG,JNLE
	{ 0x90, ENTRY_CopyBytes2Mod },                      // CMOVO (0F 40)
	{ 0x91, ENTRY_CopyBytes2Mod },                      // CMOVNO (0F 41)
	{ 0x92, ENTRY_CopyBytes2Mod },                      // CMOVB & CMOVC & CMOVNAE (0F 42)
	{ 0x93, ENTRY_CopyBytes2Mod },                      // CMOVAE & CMOVNB & CMOVNC (0F 43)
	{ 0x94, ENTRY_CopyBytes2Mod },                      // CMOVE & CMOVZ (0F 44)
	{ 0x95, ENTRY_CopyBytes2Mod },                      // CMOVNE & CMOVNZ (0F 45)
	{ 0x96, ENTRY_CopyBytes2Mod },                      // CMOVBE & CMOVNA (0F 46)
	{ 0x97, ENTRY_CopyBytes2Mod },                      // CMOVA & CMOVNBE (0F 47)
	{ 0x98, ENTRY_CopyBytes2Mod },                      // CMOVS (0F 48)
	{ 0x99, ENTRY_CopyBytes2Mod },                      // CMOVNS (0F 49)
	{ 0x9A, ENTRY_CopyBytes2Mod },                      // CMOVP & CMOVPE (0F 4A)
	{ 0x9B, ENTRY_CopyBytes2Mod },                      // CMOVNP & CMOVPO (0F 4B)
	{ 0x9C, ENTRY_CopyBytes2Mod },                      // CMOVL & CMOVNGE (0F 4C)
	{ 0x9D, ENTRY_CopyBytes2Mod },                      // CMOVGE & CMOVNL (0F 4D)
	{ 0x9E, ENTRY_CopyBytes2Mod },                      // CMOVLE & CMOVNG (0F 4E)
	{ 0x9F, ENTRY_CopyBytes2Mod },                      // CMOVG & CMOVNLE (0F 4F)
	{ 0xA0, ENTRY_CopyBytes2 },                         // PUSH
	{ 0xA1, ENTRY_CopyBytes2 },                         // POP
	{ 0xA2, ENTRY_CopyBytes2 },                         // CPUID
	{ 0xA3, ENTRY_CopyBytes2Mod },                      // BT  (0F A3)
	{ 0xA4, ENTRY_CopyBytes2Mod1 },                     // SHLD
	{ 0xA5, ENTRY_CopyBytes2Mod },                      // SHLD
	{ 0xA6, ENTRY_Invalid },                            // _A6
	{ 0xA7, ENTRY_Invalid },                            // _A7
	{ 0xA8, ENTRY_CopyBytes2 },                         // PUSH
	{ 0xA9, ENTRY_CopyBytes2 },                         // POP
	{ 0xAA, ENTRY_CopyBytes2 },                         // RSM
	{ 0xAB, ENTRY_CopyBytes2Mod },                      // BTS (0F AB)
	{ 0xAC, ENTRY_CopyBytes2Mod1 },                     // SHRD
	{ 0xAD, ENTRY_CopyBytes2Mod },                      // SHRD
	{ 0xAE, ENTRY_CopyBytes2Mod },                      // FXRSTOR/1,FXSAVE/0
	{ 0xAF, ENTRY_CopyBytes2Mod },                      // IMUL (0F AF)
	{ 0xB0, ENTRY_CopyBytes2Mod },                      // CMPXCHG (0F B0)
	{ 0xB1, ENTRY_CopyBytes2Mod },                      // CMPXCHG (0F B1)
	{ 0xB2, ENTRY_CopyBytes2Mod },                      // LSS/r
	{ 0xB3, ENTRY_CopyBytes2Mod },                      // BTR (0F B3)
	{ 0xB4, ENTRY_CopyBytes2Mod },                      // LFS/r
	{ 0xB5, ENTRY_CopyBytes2Mod },                      // LGS/r
	{ 0xB6, ENTRY_CopyBytes2Mod },                      // MOVZX/r
	{ 0xB7, ENTRY_CopyBytes2Mod },                      // MOVZX/r
	{ 0xB8, ENTRY_Invalid },                            // _B8
	{ 0xB9, ENTRY_Invalid },                            // _B9
	{ 0xBA, ENTRY_CopyBytes2Mod1 },                     // BT & BTC & BTR & BTS (0F BA)
	{ 0xBB, ENTRY_CopyBytes2Mod },                      // BTC (0F BB)
	{ 0xBC, ENTRY_CopyBytes2Mod },                      // BSF (0F BC)
	{ 0xBD, ENTRY_CopyBytes2Mod },                      // BSR (0F BD)
	{ 0xBE, ENTRY_CopyBytes2Mod },                      // MOVSX/r
	{ 0xBF, ENTRY_CopyBytes2Mod },                      // MOVSX/r
	{ 0xC0, ENTRY_CopyBytes2Mod },                      // XADD/r
	{ 0xC1, ENTRY_CopyBytes2Mod },                      // XADD/r
	{ 0xC2, ENTRY_CopyBytes2Mod },                      // CMPPS &
	{ 0xC3, ENTRY_CopyBytes2Mod },                      // MOVNTI
	{ 0xC4, ENTRY_CopyBytes2Mod1 },                     // PINSRW /r ib
	{ 0xC5, ENTRY_CopyBytes2Mod1 },                     // PEXTRW /r ib
	{ 0xC6, ENTRY_CopyBytes2Mod1 },                     // SHUFPS & SHUFPD
	{ 0xC7, ENTRY_CopyBytes2Mod },                      // CMPXCHG8B (0F C7)
	{ 0xC8, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xC9, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xCA, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xCB, ENTRY_CopyBytes2 },                         //CVTPD2PI BSWAP 0F C8 + rd
	{ 0xCC, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xCD, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xCE, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xCF, ENTRY_CopyBytes2 },                         // BSWAP 0F C8 + rd
	{ 0xD0, ENTRY_Invalid },                            // _D0
	{ 0xD1, ENTRY_CopyBytes2Mod },                      // PSRLW/r
	{ 0xD2, ENTRY_CopyBytes2Mod },                      // PSRLD/r
	{ 0xD3, ENTRY_CopyBytes2Mod },                      // PSRLQ/r
	{ 0xD4, ENTRY_CopyBytes2Mod },                      // PADDQ
	{ 0xD5, ENTRY_CopyBytes2Mod },                      // PMULLW/r
	{ 0xD6, ENTRY_CopyBytes2Mod },                      // MOVDQ2Q / MOVQ2DQ
	{ 0xD7, ENTRY_CopyBytes2Mod },                      // PMOVMSKB/r
	{ 0xD8, ENTRY_CopyBytes2Mod },                      // PSUBUSB/r
	{ 0xD9, ENTRY_CopyBytes2Mod },                      // PSUBUSW/r
	{ 0xDA, ENTRY_CopyBytes2Mod },                      // PMINUB/r
	{ 0xDB, ENTRY_CopyBytes2Mod },                      // PAND/r
	{ 0xDC, ENTRY_CopyBytes2Mod },                      // PADDUSB/r
	{ 0xDD, ENTRY_CopyBytes2Mod },                      // PADDUSW/r
	{ 0xDE, ENTRY_CopyBytes2Mod },                      // PMAXUB/r
	{ 0xDF, ENTRY_CopyBytes2Mod },                      // PANDN/r
	{ 0xE0, ENTRY_CopyBytes2Mod },                     // PAVGB
	{ 0xE1, ENTRY_CopyBytes2Mod },                      // PSRAW/r
	{ 0xE2, ENTRY_CopyBytes2Mod },                      // PSRAD/r
	{ 0xE3, ENTRY_CopyBytes2Mod },                      // PAVGW
	{ 0xE4, ENTRY_CopyBytes2Mod },                      // PMULHUW/r
	{ 0xE5, ENTRY_CopyBytes2Mod },                      // PMULHW/r
	{ 0xE6, ENTRY_CopyBytes2Mod },                      // CTDQ2PD &
	{ 0xE7, ENTRY_CopyBytes2Mod },                      // MOVNTQ
	{ 0xE8, ENTRY_CopyBytes2Mod },                      // PSUBB/r
	{ 0xE9, ENTRY_CopyBytes2Mod },                      // PSUBW/r
	{ 0xEA, ENTRY_CopyBytes2Mod },                      // PMINSW/r
	{ 0xEB, ENTRY_CopyBytes2Mod },                      // POR/r
	{ 0xEC, ENTRY_CopyBytes2Mod },                      // PADDSB/r
	{ 0xED, ENTRY_CopyBytes2Mod },                      // PADDSW/r
	{ 0xEE, ENTRY_CopyBytes2Mod },                      // PMAXSW /r
	{ 0xEF, ENTRY_CopyBytes2Mod },                      // PXOR/r
	{ 0xF0, ENTRY_Invalid },                            // _F0
	{ 0xF1, ENTRY_CopyBytes2Mod },                      // PSLLW/r
	{ 0xF2, ENTRY_CopyBytes2Mod },                      // PSLLD/r
	{ 0xF3, ENTRY_CopyBytes2Mod },                      // PSLLQ/r
	{ 0xF4, ENTRY_CopyBytes2Mod },                      // PMULUDQ/r
	{ 0xF5, ENTRY_CopyBytes2Mod },                      // PMADDWD/r
	{ 0xF6, ENTRY_CopyBytes2Mod },                      // PSADBW/r
	{ 0xF7, ENTRY_CopyBytes2Mod },                      // MASKMOVQ
	{ 0xF8, ENTRY_CopyBytes2Mod },                      // PSUBB/r
	{ 0xF9, ENTRY_CopyBytes2Mod },                      // PSUBW/r
	{ 0xFA, ENTRY_CopyBytes2Mod },                      // PSUBD/r
	{ 0xFB, ENTRY_CopyBytes2Mod },                      // FSUBQ/r
	{ 0xFC, ENTRY_CopyBytes2Mod },                      // PADDB/r
	{ 0xFD, ENTRY_CopyBytes2Mod },                      // PADDW/r
	{ 0xFE, ENTRY_CopyBytes2Mod },                      // PADDD/r
	{ 0xFF, ENTRY_Invalid },                            // _FF
	{ 0, ENTRY_End },
};

BOOL CXHookDis::SanityCheckSystem()
{
	ULONG n = 0;
	for (; n < 256; n++) {
		REFCOPYENTRY pEntry = &s_rceCopyTable[n];

		if (n != pEntry->nOpcode) {
			ASSERT(n == pEntry->nOpcode);
			return FALSE;
		}
	}
	if (s_rceCopyTable[256].pfCopy != NULL) {
		ASSERT(!"Missing end marker.");
		return FALSE;
	}

	for (n = 0; n < 256; n++) {
		REFCOPYENTRY pEntry = &s_rceCopyTable0F[n];

		if (n != pEntry->nOpcode) {
			ASSERT(n == pEntry->nOpcode);
			return FALSE;
		}
	}
	if (s_rceCopyTable0F[256].pfCopy != NULL) {
		ASSERT(!"Missing end marker.");
		return FALSE;
	}

	return TRUE;
}
#endif // defined(XHOOKS_X64) || defined(XHOOKS_X86)

/////////////////////////////////////////////////////////// IA64 Disassembler.
//
#ifdef XHOOKS_IA64
const XHOOK_IA64_BUNDLE::XHOOK_IA64_METADATA XHOOK_IA64_BUNDLE::s_rceCopyTable[33] =
{
	{ 0x00, M_UNIT, I_UNIT, I_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x01, M_UNIT, I_UNIT, I_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x02, M_UNIT, I_UNIT | STOP, I_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x03, M_UNIT, I_UNIT | STOP, I_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x04, M_UNIT, L_UNIT, X_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesMLX },
	{ 0x05, M_UNIT, L_UNIT, X_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesMLX },
	{ 0x06, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x07, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x08, M_UNIT, M_UNIT, I_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x09, M_UNIT, M_UNIT, I_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0a, M_UNIT | STOP, M_UNIT, I_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0b, M_UNIT | STOP, M_UNIT, I_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0c, M_UNIT, F_UNIT, I_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0d, M_UNIT, F_UNIT, I_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0e, M_UNIT, M_UNIT, F_UNIT, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x0f, M_UNIT, M_UNIT, F_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x10, M_UNIT, I_UNIT, B_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x11, M_UNIT, I_UNIT, B_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x12, M_UNIT, B_UNIT, B_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesMBB },
	{ 0x13, M_UNIT, B_UNIT, B_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesMBB },
	{ 0x14, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x15, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x16, B_UNIT, B_UNIT, B_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesBBB },
	{ 0x17, B_UNIT, B_UNIT, B_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesBBB },
	{ 0x18, M_UNIT, M_UNIT, B_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x19, M_UNIT, M_UNIT, B_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x1a, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x1b, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x1c, M_UNIT, F_UNIT, B_UNIT, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x1d, M_UNIT, F_UNIT, B_UNIT | STOP, &XHOOK_IA64_BUNDLE::CopyBytesMMB },
	{ 0x1e, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x1f, 0, 0, 0, &XHOOK_IA64_BUNDLE::CopyBytes },
	{ 0x00, 0, 0, 0, NULL },
};

// 120 112 104 96 88 80 72 64 56 48 40 32 24 16  8  0
//  f.  e.  d. c. b. a. 9. 8. 7. 6. 5. 4. 3. 2. 1. 0.

//                                      00
// f.e. d.c. b.a. 9.8. 7.6. 5.4. 3.2. 1.0.
// 0000 0000 0000 0000 0000 0000 0000 001f : Template [4..0]
// 0000 0000 0000 0000 0000 03ff ffff ffe0 : Zero [ 41..  5]
// 0000 0000 0000 0000 0000 3c00 0000 0000 : Zero [ 45.. 42]
// 0000 0000 0007 ffff ffff c000 0000 0000 : One  [ 82.. 46]
// 0000 0000 0078 0000 0000 0000 0000 0000 : One  [ 86.. 83]
// 0fff ffff ff80 0000 0000 0000 0000 0000 : Two  [123.. 87]
// f000 0000 0000 0000 0000 0000 0000 0000 : Two  [127..124]
BYTE XHOOK_IA64_BUNDLE::GetTemplate() const
{
	return (data[0] & 0x1f);
}

BYTE XHOOK_IA64_BUNDLE::GetInst0() const
{
	return ((data[5] & 0x3c) >> 2);
}

BYTE XHOOK_IA64_BUNDLE::GetInst1() const
{
	return ((data[10] & 0x78) >> 3);
}

BYTE XHOOK_IA64_BUNDLE::GetInst2() const
{
	return ((data[15] & 0xf0) >> 4);
}

BYTE XHOOK_IA64_BUNDLE::GetUnit0() const
{
	return s_rceCopyTable[data[0] & 0x1f].nUnit0;
}

BYTE XHOOK_IA64_BUNDLE::GetUnit1() const
{
	return s_rceCopyTable[data[0] & 0x1f].nUnit1;
}

BYTE XHOOK_IA64_BUNDLE::GetUnit2() const
{
	return s_rceCopyTable[data[0] & 0x1f].nUnit2;
}

UINT64 XHOOK_IA64_BUNDLE::GetData0() const
{
	return (((wide[0] & 0x000003ffffffffe0) >> 5));
}

UINT64 XHOOK_IA64_BUNDLE::GetData1() const
{
	return (((wide[0] & 0xffffc00000000000) >> 46) |
		((wide[1] & 0x000000000007ffff) << 18));
}

UINT64 XHOOK_IA64_BUNDLE::GetData2() const
{
	return (((wide[1] & 0x0fffffffff800000) >> 23));
}

VOID XHOOK_IA64_BUNDLE::SetInst0(BYTE nInst)
{
	data[5] = (data[5] & ~0x3c) | ((nInst << 2) & 0x3c);
}

VOID XHOOK_IA64_BUNDLE::SetInst1(BYTE nInst)
{
	data[10] = (data[10] & ~0x78) | ((nInst << 3) & 0x78);
}

VOID XHOOK_IA64_BUNDLE::SetInst2(BYTE nInst)
{
	data[15] = (data[15] & ~0xf0) | ((nInst << 4) & 0xf0);
}

VOID XHOOK_IA64_BUNDLE::SetData0(UINT64 nData)
{
	wide[0] = (wide[0] & ~0x000003ffffffffe0) | ((nData << 5) & 0x000003ffffffffe0);
}

VOID XHOOK_IA64_BUNDLE::SetData1(UINT64 nData)
{
	wide[0] = (wide[0] & ~0xffffc00000000000) | ((nData << 46) & 0xffffc00000000000);
	wide[1] = (wide[1] & ~0x000000000007ffff) | ((nData >> 18) & 0x000000000007ffff);
}

VOID XHOOK_IA64_BUNDLE::SetData2(UINT64 nData)
{
	wide[1] = (wide[1] & ~0x0fffffffff800000) | ((nData << 23) & 0x0fffffffff800000);
}

BOOL XHOOK_IA64_BUNDLE::IsBrl() const
{
	// f.e. d.c. b.a. 9.8. 7.6. 5. 4. 3. 2. 1. 0.
	// c000 0070 0000 0000 0000 00 01 00 00 00 05 : brl.sptk.few
	// c8ff fff0 007f fff0 ffff 00 01 00 00 00 05 : brl.sptk.few
	// c000 0048 0000 0000 0001 00 00 00 00 00 05 : brl.sptk.many
	return ((wide[0] & 0x000000000000001e) == 0x0000000000000004 && // 4 or 5.
		(wide[1] & 0xe000000000000000) == 0xc000000000000000);  // c or d.
}

VOID XHOOK_IA64_BUNDLE::SetBrl()
{
	wide[0] = 0x0000000100000005;   // few
	//wide[0] = 0x0000000180000005; // many
	wide[1] = 0xc000000800000000;
}

UINT64 XHOOK_IA64_BUNDLE::GetBrlImm() const
{
	return (
		//          0x0000000000fffff0
		((wide[1] & 0x00fffff000000000) >> 32) |    // all 20 bits of imm20b.
		//          0x000000ffff000000
		((wide[0] & 0xffff000000000000) >> 24) |    // bottom 16 bits of imm39.
		//          0x7fffff0000000000
		((wide[1] & 0x00000000007fffff) << 40) |    // top 23 bits of imm39.
		//          0x8000000000000000
		((wide[1] & 0x0800000000000000) << 4)      // single bit of i.
		);
}

VOID XHOOK_IA64_BUNDLE::SetBrlImm(UINT64 imm)
{
	wide[0] = ((wide[0] & ~0xffff000000000000) |
		//      0xffff000000000000
		((imm & 0x000000ffff000000) << 24)       // bottom 16 bits of imm39.
		);
	wide[1] = ((wide[1] & ~0x08fffff0007fffff) |
		//      0x00fffff000000000
		((imm & 0x0000000000fffff0) << 32) |     // all 20 bits of imm20b.
		//      0x00000000007fffff
		((imm & 0x7fffff0000000000) >> 40) |     // top 23 bits of imm39.
		//      0x0800000000000000
		((imm & 0x8000000000000000) >> 4)       // single bit of i.
		);
}

UINT64 XHOOK_IA64_BUNDLE::GetBrlTarget() const
{
	return (UINT64)this + GetBrlImm();
}

VOID XHOOK_IA64_BUNDLE::SetBrl(UINT64 target)
{
	UINT64 imm = target - (UINT64)this;
	SetBrl();
	SetBrlImm(imm);
}

VOID XHOOK_IA64_BUNDLE::SetBrlTarget(UINT64 target)
{
	UINT64 imm = target - (UINT64)this;
	SetBrlImm(imm);
}

BOOL XHOOK_IA64_BUNDLE::IsMovlGp() const
{
	// f.e. d.c. b.a. 9.8. 7.6. 5.4. 3.2. 1.0.
	// 6fff f7f0 207f ffff ffff c001 0000 0004
	// 6000 0000 2000 0000 0000 0001 0000 0004
	return ((wide[0] & 0x00003ffffffffffe) == 0x0000000100000004 &&
		(wide[1] & 0xf000080fff800000) == 0x6000000020000000);
}

UINT64 XHOOK_IA64_BUNDLE::GetMovlGp() const
{
	UINT64 raw = (
		//          0x0000000000000070
		((wide[1] & 0x000007f000000000) >> 36) |
		//          0x000000000000ff80
		((wide[1] & 0x07fc000000000000) >> 43) |
		//          0x00000000001f0000
		((wide[1] & 0x0003e00000000000) >> 29) |
		//          0x0000000000200000
		((wide[1] & 0x0000100000000000) >> 23) |
		//          0x000000ffffc00000
		((wide[0] & 0xffffc00000000000) >> 24) |
		//          0x7fffff0000000000
		((wide[1] & 0x00000000007fffff) << 40) |
		//          0x8000000000000000
		((wide[1] & 0x0800000000000000) << 4)
		);

	return (INT64)raw;
}

VOID XHOOK_IA64_BUNDLE::SetMovlGp(UINT64 gp)
{
	UINT64 raw = (UINT64)gp;

	wide[0] = (0x0000000100000005 |
		//      0xffffc00000000000
		((raw & 0x000000ffffc00000) << 24)
		);
	wide[1] = (
		0x6000000020000000 |
		//      0x0000070000000000
		((raw & 0x0000000000000070) << 36) |
		//      0x07fc000000000000
		((raw & 0x000000000000ff80) << 43) |
		//      0x0003e00000000000
		((raw & 0x00000000001f0000) << 29) |
		//      0x0000100000000000
		((raw & 0x0000000000200000) << 23) |
		//      0x00000000007fffff
		((raw & 0x7fffff0000000000) >> 40) |
		//      0x0800000000000000
		((raw & 0x8000000000000000) >> 4)
		);
}

BOOL XHOOK_IA64_BUNDLE::CopyBytes(const XHOOK_IA64_METADATA *pMeta,
	XHOOK_IA64_BUNDLE *pDst) const
{
	(void)pMeta;
	pDst->wide[0] = wide[0];
	pDst->wide[1] = wide[1];

	return true;
}

BOOL XHOOK_IA64_BUNDLE::CopyBytesMMB(const XHOOK_IA64_METADATA *pMeta,
	XHOOK_IA64_BUNDLE *pDst) const
{
	(void)pMeta;
	pDst->wide[0] = wide[0];
	pDst->wide[1] = wide[1];

	BYTE nInst2 = GetInst2();
	if ((nInst2 == 0x0 || nInst2 == 0x1 || nInst2 == 0x2)) {
		return true;
	}
	return false;
}

BOOL XHOOK_IA64_BUNDLE::CopyBytesMBB(const XHOOK_IA64_METADATA *pMeta,
	XHOOK_IA64_BUNDLE *pDst) const
{
	(void)pMeta;
	pDst->wide[0] = wide[0];
	pDst->wide[1] = wide[1];

	BYTE nInst1 = GetInst1();
	BYTE nInst2 = GetInst2();
	if ((nInst1 == 0x0 || nInst1 == 0x1 || nInst1 == 0x2) &&
		(nInst2 == 0x0 || nInst2 == 0x1 || nInst2 == 0x2)) {
		return true;
	}
	return false;
}

BOOL XHOOK_IA64_BUNDLE::CopyBytesBBB(const XHOOK_IA64_METADATA *pMeta,
	XHOOK_IA64_BUNDLE *pDst) const
{
	(void)pMeta;
	pDst->wide[0] = wide[0];
	pDst->wide[1] = wide[1];

	BYTE nInst0 = GetInst0();
	BYTE nInst1 = GetInst1();
	BYTE nInst2 = GetInst2();
	if ((nInst0 == 0x0 || nInst0 == 0x1 || nInst0 == 0x2) &&
		(nInst1 == 0x0 || nInst1 == 0x1 || nInst1 == 0x2) &&
		(nInst2 == 0x0 || nInst2 == 0x1 || nInst2 == 0x2)) {
		return true;
	}

	return false;
}

BOOL XHOOK_IA64_BUNDLE::CopyBytesMLX(const XHOOK_IA64_METADATA *pMeta,
	XHOOK_IA64_BUNDLE *pDst) const
{
	(void)pMeta;
	pDst->wide[0] = wide[0];
	pDst->wide[1] = wide[1];

#if XHOOK_DEBUG
	{
		const char szUnitNames[17] = "?aimbflx?AIMBFLX";
		BYTE nTemplate = GetTemplate();
		BYTE nInst0 = GetInst0();
		BYTE nInst1 = GetInst1();
		BYTE nInst2 = GetInst2();
		BYTE nUnit0 = GetUnit0();
		BYTE nUnit1 = GetUnit1();
		BYTE nUnit2 = GetUnit2();
		if (nUnit1 == L_UNIT) { // MLX instruction
			UINT64 d2 = (
				//          0x0000000000fffff0
				((wide[1] & 0x00fffff000000000) >> 32) |
				//          0x000000ffff000000
				((wide[0] & 0xffff000000000000) >> 24) |
				//          0x7fffff0000000000
				((wide[1] & 0x00000000007fffff) << 40) |
				//          0x8000000000000000
				((wide[1] & 0x0800000000000000) << 4)
				);
			printf("%p: %02x %c%01x %010I64lx %c%01x %016I64lx\n",
				this,
				nTemplate,
				szUnitNames[nUnit0], nInst0, GetData0(),
				szUnitNames[nUnit2], nInst2, d2);
		}
		else {
			printf("%p: %02x %c%01x %010I64lx %c%01x %010I64lx %c%01x %010I64lx\n",
				this,
				nTemplate,
				szUnitNames[nUnit0], nInst0, GetData0(),
				szUnitNames[nUnit1], nInst1, GetData1(),
				szUnitNames[nUnit2], nInst2, GetData2());
		}
	}
#endif

	if (IsBrl()) {
		pDst->SetBrlTarget(GetBrlTarget());
		return true;
	}
	BYTE nInst2 = GetInst2();

	if (nInst2 == 0x06 && nInst2 == 0x0d) {
		return false;
	}
	return true;
}

BOOL XHOOK_IA64_BUNDLE::Copy(XHOOK_IA64_BUNDLE *pDst) const
{
	const XHOOK_IA64_METADATA *pce = &s_rceCopyTable[GetTemplate()];
	return (this->*pce->pfCopy)(pce, pDst);
}

BOOL XHOOK_IA64_BUNDLE::SetNop0()
{
	const XHOOK_IA64_METADATA *pce = &s_rceCopyTable[GetTemplate()];

	switch (pce->nUnit0 & UNIT_MASK) {
	case I_UNIT:
	case M_UNIT:
	case F_UNIT:
		SetInst0(0);
		SetData0(0x8000000);
		return true;
	case B_UNIT:
		SetInst0(2);
		SetData0(0);
		return true;
	}
	DebugBreak();
	return false;
}

BOOL XHOOK_IA64_BUNDLE::SetNop1()
{
	const XHOOK_IA64_METADATA *pce = &s_rceCopyTable[GetTemplate()];

	switch (pce->nUnit1 & UNIT_MASK) {
	case I_UNIT:
	case M_UNIT:
	case F_UNIT:
		SetInst1(0);
		SetData1(0x8000000);
		return true;
	case B_UNIT:
		SetInst1(2);
		SetData1(0);
		return true;
	}
	DebugBreak();
	return false;
}

BOOL XHOOK_IA64_BUNDLE::SetNop2()
{
	const XHOOK_IA64_METADATA *pce = &s_rceCopyTable[GetTemplate()];

	switch (pce->nUnit2 & UNIT_MASK) {
	case I_UNIT:
	case M_UNIT:
	case F_UNIT:
		SetInst2(0);
		SetData2(0x8000000);
		return true;
	case B_UNIT:
		SetInst2(2);
		SetData2(0);
		return true;
	}
	DebugBreak();
	return false;
}

BOOL XHOOK_IA64_BUNDLE::SetStop()
{
	data[0] |= 0x01;
	return true;
}

PVOID WINAPI XHookCopyInstruction(PVOID pDst,
	PVOID *ppDstPool,
	PVOID pSrc,
	PVOID *ppTarget,
	LONG *plExtra)
{
	(void)ppDstPool; // IA64 doesn't use a constant pool.
	XHOOK_IA64_BUNDLE bExtra;

	XHOOK_IA64_BUNDLE *pbSrc = (XHOOK_IA64_BUNDLE *)pSrc;
	XHOOK_IA64_BUNDLE *pbDst = pDst ? (XHOOK_IA64_BUNDLE *)pDst : &bExtra;

	if (ppTarget != NULL) {
		if (pbSrc->IsBrl()) {
			*ppTarget = (PVOID)pbSrc->GetBrlTarget();
		}
		else {
			*ppTarget = XHOOK_INSTRUCTION_TARGET_NONE;
		}
	}
	if (pbSrc->Copy(pbDst)) {
		if (plExtra != NULL) {
			*plExtra = 0;
		}
	}
	else {
		if (plExtra != NULL) {
			*plExtra = sizeof(XHOOK_IA64_BUNDLE);
		}
	}

	return pbSrc + 1;
}

#endif // XHOOKS_IA64

#ifdef XHOOKS_ARM

#define XHOOKS_PFUNC_TO_PBYTE(p)  ((PBYTE)(((ULONG_PTR)(p)) & ~(ULONG_PTR)1))
#define XHOOKS_PBYTE_TO_PFUNC(p)  ((PBYTE)(((ULONG_PTR)(p)) | (ULONG_PTR)1))

#define c_PCAdjust  4       // The PC value of an instruction is the PC address plus 4.
#define c_PC        15      // The register number for the Program Counter
#define c_LR        14      // The register number for the Link Register
#define c_SP        13      // The register number for the Stack Pointer
#define c_NOP       0xbf00  // A nop instruction
#define c_BREAK     0xdefe  // A nop instruction

class CXHookDis
{
public:
	CXHookDis();

	PBYTE   CopyInstruction(PBYTE pDst,
		PBYTE *ppDstPool,
		PBYTE pSrc,
		PBYTE *ppTarget,
		LONG *plExtra);

public:
	typedef BYTE(CXHookDis::* COPYFUNC)(PBYTE pbDst, PBYTE pbSrc);

	struct COPYENTRY {
		USHORT      nOpcode;
		COPYFUNC    pfCopy;
	};

	typedef const COPYENTRY * REFCOPYENTRY;

	struct Branch5
	{
		DWORD Register : 3;
		DWORD Imm5 : 5;
		DWORD Padding : 1;
		DWORD I : 1;
		DWORD OpCode : 6;
	};

	struct Branch5Target
	{
		DWORD Padding : 1;
		DWORD Imm5 : 5;
		DWORD I : 1;
		DWORD Padding2 : 25;
	};

	struct Branch8
	{
		DWORD Imm8 : 8;
		DWORD Condition : 4;
		DWORD OpCode : 4;
	};

	struct Branch8Target
	{
		DWORD Padding : 1;
		DWORD Imm8 : 8;
		DWORD Padding2 : 23;
	};

	struct Branch11
	{
		DWORD Imm11 : 11;
		DWORD OpCode : 5;
	};

	struct Branch11Target
	{
		DWORD Padding : 1;
		DWORD Imm11 : 11;
		DWORD Padding2 : 20;
	};

	struct Branch20
	{
		DWORD Imm11 : 11;
		DWORD J2 : 1;
		DWORD IT : 1;
		DWORD J1 : 1;
		DWORD Other : 2;
		DWORD Imm6 : 6;
		DWORD Condition : 4;
		DWORD Sign : 1;
		DWORD OpCode : 5;
	};

	struct Branch20Target
	{
		DWORD Padding : 1;
		DWORD Imm11 : 11;
		DWORD Imm6 : 6;
		DWORD J1 : 1;
		DWORD J2 : 1;
		DWORD Sign : 1;
		DWORD Padding2 : 11;
	};

	struct Branch24
	{
		DWORD Imm11 : 11;
		DWORD J2 : 1;
		DWORD InstructionSet : 1;
		DWORD J1 : 1;
		DWORD Link : 1;
		DWORD Branch : 1;
		DWORD Imm10 : 10;
		DWORD Sign : 1;
		DWORD OpCode : 5;
	};

	struct Branch24Target
	{
		DWORD Padding : 1;
		DWORD Imm11 : 11;
		DWORD Imm10 : 10;
		DWORD I2 : 1;
		DWORD I1 : 1;
		DWORD Sign : 1;
		DWORD Padding2 : 7;
	};

	struct LiteralLoad8
	{
		DWORD Imm8 : 8;
		DWORD Register : 3;
		DWORD OpCode : 5;
	};

	struct LiteralLoad8Target
	{
		DWORD Padding : 2;
		DWORD Imm8 : 8;
		DWORD Padding2 : 22;
	};

	struct LiteralLoad12
	{
		DWORD Imm12 : 12;
		DWORD Register : 4;
		DWORD OpCodeSuffix : 7;
		DWORD Add : 1;
		DWORD OpCodePrefix : 8;
	};

	struct LiteralLoad12Target
	{
		DWORD Imm12 : 12;
		DWORD Padding : 20;
	};

	struct ImmediateRegisterLoad32
	{
		DWORD Imm12 : 12;
		DWORD DestinationRegister : 4;
		DWORD SourceRegister : 4;
		DWORD OpCode : 12;
	};

	struct ImmediateRegisterLoad16
	{
		DWORD DestinationRegister : 3;
		DWORD SourceRegister : 3;
		DWORD OpCode : 10;
	};

	struct TableBranch
	{
		DWORD IndexRegister : 4;
		DWORD HalfWord : 1;
		DWORD OpCodeSuffix : 11;
		DWORD BaseRegister : 4;
		DWORD OpCodePrefix : 12;
	};

	struct Shift
	{
		DWORD Imm2 : 2;
		DWORD Imm3 : 3;
	};

	struct Add32
	{
		DWORD SecondOperandRegister : 4;
		DWORD Type : 2;
		DWORD Imm2 : 2;
		DWORD DestinationRegister : 4;
		DWORD Imm3 : 3;
		DWORD Padding : 1;
		DWORD FirstOperandRegister : 4;
		DWORD SetFlags : 1;
		DWORD OpCode : 11;
	};

	struct LogicalShiftLeft32
	{
		DWORD SourceRegister : 4;
		DWORD Padding : 2;
		DWORD Imm2 : 2;
		DWORD DestinationRegister : 4;
		DWORD Imm3 : 3;
		DWORD Padding2 : 5;
		DWORD SetFlags : 1;
		DWORD OpCode : 11;
	};

	struct StoreImmediate12
	{
		DWORD Imm12 : 12;
		DWORD SourceRegister : 4;
		DWORD BaseRegister : 4;
		DWORD OpCode : 12;
	};

protected:
	BYTE    PureCopy16(BYTE* pSource, BYTE* pDest);
	BYTE    PureCopy32(BYTE* pSource, BYTE* pDest);
	BYTE    CopyMiscellaneous16(BYTE* pSource, BYTE* pDest);
	BYTE    CopyConditionalBranchOrOther16(BYTE* pSource, BYTE* pDest);
	BYTE    CopyUnConditionalBranch16(BYTE* pSource, BYTE* pDest);
	BYTE    CopyLiteralLoad16(BYTE* pSource, BYTE* pDest);
	BYTE    CopyBranchExchangeOrDataProcessing16(BYTE* pSource, BYTE* pDest);
	BYTE    CopyBranch24(BYTE* pSource, BYTE* pDest);
	BYTE    CopyBranchOrMiscellaneous32(BYTE* pSource, BYTE* pDest);
	BYTE    CopyLiteralLoad32(BYTE* pSource, BYTE* pDest);
	BYTE    CopyLoadAndStoreSingle(BYTE* pSource, BYTE* pDest);
	BYTE    CopyLoadAndStoreMultipleAndSRS(BYTE* pSource, BYTE* pDest);
	BYTE    CopyTableBranch(BYTE* pSource, BYTE* pDest);
	BYTE    BeginCopy32(BYTE* pSource, BYTE* pDest);

	LONG    DecodeBranch5(ULONG opcode);
	USHORT  EncodeBranch5(ULONG originalOpCode, LONG delta);
	LONG    DecodeBranch8(ULONG opcode);
	USHORT  EncodeBranch8(ULONG originalOpCode, LONG delta);
	LONG    DecodeBranch11(ULONG opcode);
	USHORT  EncodeBranch11(ULONG originalOpCode, LONG delta);
	BYTE    EmitBranch11(PUSHORT& pDest, LONG relativeAddress);
	LONG    DecodeBranch20(ULONG opcode);
	ULONG   EncodeBranch20(ULONG originalOpCode, LONG delta);
	LONG    DecodeBranch24(ULONG opcode, BOOL& fLink);
	ULONG   EncodeBranch24(ULONG originalOpCode, LONG delta, BOOL fLink);
	LONG    DecodeLiteralLoad8(ULONG instruction);
	LONG    DecodeLiteralLoad12(ULONG instruction);
	BYTE    EmitLiteralLoad8(PUSHORT& pDest, BYTE targetRegister, PBYTE pLiteral);
	BYTE    EmitLiteralLoad12(PUSHORT& pDest, BYTE targetRegister, PBYTE pLiteral);
	BYTE    EmitImmediateRegisterLoad32(PUSHORT& pDest, BYTE reg);
	BYTE    EmitImmediateRegisterLoad16(PUSHORT& pDest, BYTE reg);
	BYTE    EmitLongLiteralLoad(PUSHORT& pDest, BYTE reg, PVOID pTarget);
	BYTE    EmitLongBranch(PUSHORT& pDest, PVOID pTarget);
	USHORT  CalculateExtra(BYTE sourceLength, BYTE* pDestStart, BYTE* pDestEnd);

protected:
	ULONG GetLongInstruction(BYTE* pSource)
	{
		return (((PUSHORT)pSource)[0] << 16) | (((PUSHORT)pSource)[1]);
	}

	BYTE EmitLongInstruction(PUSHORT& pDstInst, ULONG instruction)
	{
		*pDstInst++ = instruction >> 16;
		*pDstInst++ = (USHORT)instruction;
		return sizeof(ULONG);
	}

	BYTE EmitShortInstruction(PUSHORT& pDstInst, USHORT instruction)
	{
		*pDstInst++ = instruction;
		return sizeof(USHORT);
	}

	PBYTE Align4(PBYTE pValue)
	{
		return (PBYTE)(((ULONG)pValue) & ~(ULONG)3u);
	}

	PBYTE CalculateTarget(PBYTE pSource, LONG delta)
	{
		return (pSource + delta + c_PCAdjust);
	}

	LONG CalculateNewDelta(PBYTE pTarget, BYTE* pDest)
	{
		return (pTarget - (pDest + c_PCAdjust));

	}

	BYTE    EmitAdd32(PUSHORT& pDstInst, BYTE op1Reg, BYTE op2Reg, BYTE dstReg, BYTE shiftAmount)
	{
		Shift& shift = (Shift&)(shiftAmount);
		const BYTE shiftType = 0x00; // LSL
		Add32 add = { op2Reg, shiftType, shift.Imm2, dstReg, shift.Imm3,
			0x0, op1Reg, 0x0, 0x758 };
		return EmitLongInstruction(pDstInst, (ULONG&)add);
	}

	BYTE    EmitLogicalShiftLeft32(PUSHORT& pDstInst, BYTE srcReg, BYTE dstReg, BYTE shiftAmount)
	{
		Shift& shift = (Shift&)(shiftAmount);
		LogicalShiftLeft32 shiftLeft = { srcReg, 0x00, shift.Imm2, dstReg, shift.Imm3, 0x1E,
			0x00, 0x752 };
		return EmitLongInstruction(pDstInst, (ULONG&)shiftLeft);
	}

	BYTE    EmitStoreImmediate12(PUSHORT& pDstInst, BYTE srcReg, BYTE baseReg, USHORT offset)
	{
		StoreImmediate12 store = { offset, srcReg, baseReg, 0xF8C };
		return EmitLongInstruction(pDstInst, (ULONG&)store);
	}

protected:
	PBYTE   m_pbTarget;
	PBYTE   m_pbPool;
	LONG    m_lExtra;

	BYTE    m_rbScratchDst[64];

	static const COPYENTRY s_rceTable[32];
};

LONG CXHookDis::DecodeBranch5(ULONG opcode)
{
	Branch5& branch = (Branch5&)(opcode);

	Branch5Target target = {};
	target.Imm5 = branch.Imm5;
	target.I = branch.I;

	// Return zero-extended value
	return (LONG&)target;
}

USHORT CXHookDis::EncodeBranch5(ULONG originalOpCode, LONG delta)
{
	// Too large for a 5 bit branch (5 bit branches can be up to 7 bits due to I and the trailing 0)
	if (delta < 0 || delta > 0x7F) {
		return 0;
	}

	Branch5& branch = (Branch5&)(originalOpCode);
	Branch5Target& target = (Branch5Target&)(delta);

	branch.Imm5 = target.Imm5;
	branch.I = target.I;

	return (USHORT&)branch;
}

LONG CXHookDis::DecodeBranch8(ULONG opcode)
{
	Branch8& branch = (Branch8&)(opcode);

	Branch8Target target = {};
	target.Imm8 = branch.Imm8;

	// Return sign extended value
	return (((LONG&)target) << 23) >> 23;
}

USHORT CXHookDis::EncodeBranch8(ULONG originalOpCode, LONG delta)
{
	// Too large for 8 bit branch (8 bit branches can be up to 9 bits due to the trailing 0)
	if (delta < (-(int)0x100) || delta > 0xFF) {
		return 0;
	}

	Branch8& branch = (Branch8&)(originalOpCode);
	Branch8Target& target = (Branch8Target&)(delta);

	branch.Imm8 = target.Imm8;

	return (USHORT&)branch;
}

LONG CXHookDis::DecodeBranch11(ULONG opcode)
{
	Branch11& branch = (Branch11&)(opcode);

	Branch11Target target = {};
	target.Imm11 = branch.Imm11;

	// Return sign extended value
	return (((LONG&)target) << 20) >> 20;
}

USHORT CXHookDis::EncodeBranch11(ULONG originalOpCode, LONG delta)
{
	// Too large for an 11 bit branch (11 bit branches can be up to 12 bits due to the trailing 0)
	if (delta < (-(int)0x800) || delta > 0x7FF) {
		return 0;
	}

	Branch11& branch = (Branch11&)(originalOpCode);
	Branch11Target& target = (Branch11Target&)(delta);

	branch.Imm11 = target.Imm11;

	return (USHORT&)branch;
}

BYTE CXHookDis::EmitBranch11(PUSHORT& pDest, LONG relativeAddress)
{
	Branch11Target& target = (Branch11Target&)(relativeAddress);
	Branch11 branch11 = { target.Imm11, 0x1C };

	*pDest++ = (USHORT&)branch11;
	return sizeof(USHORT);
}

LONG CXHookDis::DecodeBranch20(ULONG opcode)
{
	Branch20& branch = (Branch20&)(opcode);

	Branch20Target target = {};
	target.Imm11 = branch.Imm11;
	target.Imm6 = branch.Imm6;
	target.Sign = branch.Sign;
	target.J1 = branch.J1;
	target.J2 = branch.J2;

	// Sign extend
	if (target.Sign) {
		target.Padding2 = UINT_MAX;
	}

	return (LONG&)target;
}

ULONG CXHookDis::EncodeBranch20(ULONG originalOpCode, LONG delta)
{
	// Too large for 20 bit branch (20 bit branches can be up to 21 bits due to the trailing 0)
	if (delta < (-(int)0x100000) || delta > 0xFFFFF) {
		return 0;
	}

	Branch20& branch = (Branch20&)(originalOpCode);
	Branch20Target& target = (Branch20Target&)(delta);

	branch.Imm11 = target.Imm11;
	branch.Imm6 = target.Imm6;
	branch.Sign = target.Sign;
	branch.J1 = target.J1;
	branch.J2 = target.J2;

	return (ULONG&)branch;
}

LONG CXHookDis::DecodeBranch24(ULONG opcode, BOOL& fLink)
{
	Branch24& branch = (Branch24&)(opcode);

	Branch24Target target = {};
	target.Imm11 = branch.Imm11;
	target.Imm10 = branch.Imm10;
	target.Sign = branch.Sign;
	target.I1 = ~(branch.J1 ^ target.Sign);
	target.I2 = ~(branch.J2 ^ target.Sign);
	fLink = branch.Link;

	// Sign extend
	if (target.Sign) {
		target.Padding2 = UINT_MAX;
	}

	return (LONG&)target;
}

ULONG CXHookDis::EncodeBranch24(ULONG originalOpCode, LONG delta, BOOL fLink)
{
	// Too large for 24 bit branch (24 bit branches can be up to 25 bits due to the trailing 0)
	if (delta < static_cast<int>(0xFF000000) || delta > static_cast<int>(0xFFFFFF)) {
		return 0;
	}

	Branch24& branch = (Branch24&)(originalOpCode);
	Branch24Target& target = (Branch24Target&)(delta);

	branch.Imm11 = target.Imm11;
	branch.Imm10 = target.Imm10;
	branch.Link = fLink;
	branch.Sign = target.Sign;
	branch.J1 = ~(target.I1 ^ branch.Sign);
	branch.J2 = ~(target.I2 ^ branch.Sign);

	return (ULONG&)branch;
}

LONG CXHookDis::DecodeLiteralLoad8(ULONG instruction)
{
	LiteralLoad8& load = (LiteralLoad8&)(instruction);

	LiteralLoad8Target target = {};
	target.Imm8 = load.Imm8;

	return (LONG&)target;
}

BYTE CXHookDis::EmitLiteralLoad8(PUSHORT& pDest, BYTE targetRegister, PBYTE pLiteral)
{
	// Note: We add 2 (which gets rounded down) because literals must be 32-bit
	//       aligned, but the ldr can be 16-bit aligned.
	LONG newDelta = CalculateNewDelta((PBYTE)pLiteral + 2, (PBYTE)pDest);
	LONG relative = ((newDelta > 0 ? newDelta : -newDelta) & 0x3FF);

	LiteralLoad8Target& target = (LiteralLoad8Target&)(relative);
	LiteralLoad8 load = { target.Imm8, targetRegister, 0x9 };

	return EmitShortInstruction(pDest, (USHORT&)load);
}

LONG CXHookDis::DecodeLiteralLoad12(ULONG instruction)
{
	LiteralLoad12& load = (LiteralLoad12&)(instruction);

	LiteralLoad12Target target = {};
	target.Imm12 = load.Imm12;

	return (LONG&)target;
}

BYTE CXHookDis::EmitLiteralLoad12(PUSHORT& pDest, BYTE targetRegister, PBYTE pLiteral)
{
	// Note: We add 2 (which gets rounded down) because literals must be 32-bit
	//       aligned, but the ldr can be 16-bit aligned.
	LONG newDelta = CalculateNewDelta((PBYTE)pLiteral + 2, (PBYTE)pDest);
	LONG relative = ((newDelta > 0 ? newDelta : -newDelta) & 0xFFF);

	LiteralLoad12Target& target = (LiteralLoad12Target&)(relative);
	target.Imm12 -= target.Imm12 & 3;
	LiteralLoad12 load = { target.Imm12, targetRegister, 0x5F, (newDelta > 0), 0xF8 };

	return EmitLongInstruction(pDest, (ULONG&)load);
}

BYTE CXHookDis::EmitImmediateRegisterLoad32(PUSHORT& pDest, BYTE reg)
{
	ImmediateRegisterLoad32 load = { 0, reg, reg, 0xF8D };
	return EmitLongInstruction(pDest, (ULONG&)load);
}

BYTE CXHookDis::EmitImmediateRegisterLoad16(PUSHORT& pDest, BYTE reg)
{
	ImmediateRegisterLoad16 load = { reg, reg, 0x680 >> 2 };
	return EmitShortInstruction(pDest, (USHORT&)load);
}

BYTE CXHookDis::EmitLongLiteralLoad(PUSHORT& pDest, BYTE targetRegister, PVOID pTarget)
{
	*--((PULONG&)m_pbPool) = ((ULONG)pTarget);

	// ldr rn, target.
	BYTE size = EmitLiteralLoad12(pDest, targetRegister, m_pbPool);

	// This only makes sense if targetRegister != PC;
	// otherwise, we would have branched with the previous instruction anyway
	if (targetRegister != c_PC) {
		// ldr rn, [rn]
		if (targetRegister <= 7) {
			size += EmitImmediateRegisterLoad16(pDest, targetRegister);
		}
		else {
			size += EmitImmediateRegisterLoad32(pDest, targetRegister);
		}
	}

	return size;
}

BYTE CXHookDis::EmitLongBranch(PUSHORT& pDest, PVOID pTarget)
{
	// Emit a long literal load into PC
	BYTE size = EmitLongLiteralLoad(pDest, c_PC, pTarget);
	return size;
}

BYTE CXHookDis::PureCopy16(BYTE* pSource, BYTE* pDest)
{
	*(USHORT *)pDest = *(USHORT *)pSource;
	return sizeof(USHORT);
}

BYTE CXHookDis::PureCopy32(BYTE* pSource, BYTE* pDest)
{
	*(ULONG *)pDest = *(ULONG*)pSource;
	return sizeof(DWORD);
}

USHORT CXHookDis::CalculateExtra(BYTE sourceLength, BYTE* pDestStart, BYTE* pDestEnd)
{
	ULONG destinationLength = pDestEnd - pDestStart;
	return static_cast<USHORT>((destinationLength > sourceLength) ? (destinationLength - sourceLength) : 0);
}

BYTE CXHookDis::CopyMiscellaneous16(BYTE* pSource, BYTE* pDest)
{
	USHORT instruction = *(PUSHORT)(pSource);

	// Compare and branch imm5 (CBZ, CBNZ)
	if ((instruction & 0x100) && !(instruction & 0x400)) { // (1011x0x1xxxxxxxx)
		LONG oldDelta = DecodeBranch5(instruction);
		PBYTE pTarget = CalculateTarget(pSource, oldDelta);
		m_pbTarget = pTarget;

		LONG newDelta = CalculateNewDelta(pTarget, pDest);
		instruction = EncodeBranch5(instruction, newDelta);

		if (instruction) {
			// Copy the 16 bit instruction over
			*(PUSHORT)(pDest) = instruction;
			return sizeof(USHORT); // The source instruction was 16 bits
		}

		// If that fails, re-encode with 'conditional branch' logic, without using the condition flags
		// For example, cbz r2,+0x56 (0x90432) becomes:
		//
		//  001df73a b92a     cbnz        r2,001df748
		//  001df73c e002     b           001df744
		//  001df73e bf00     nop
		//  001df740 0432     dc.h        0432
		//  001df742 0009     dc.h        0009
		//  001df744 f85ff008 ldr         pc,=0x90432
		//

		// Store where we will be writing our conditional branch, and move past it so we can emit a long branch
		PUSHORT pDstInst = (PUSHORT)(pDest);
		PUSHORT pConditionalBranchInstruction = pDstInst++;

		// Emit the long branch instruction
		BYTE longBranchSize = EmitLongBranch(pDstInst, pTarget);

		// Invert the CBZ/CBNZ instruction to move past our 'long branch' if the inverse comparison succeeds
		// Write the CBZ/CBNZ instruction *before* the long branch we emitted above
		// This had to be done out of order, since the size of a long branch can vary due to alignment restrictions
		instruction = EncodeBranch5(*(PUSHORT)(pSource), longBranchSize - c_PCAdjust + sizeof(USHORT));
		Branch5& branch = (Branch5&)(instruction);
		branch.OpCode = (branch.OpCode & 0x02) ? 0x2C : 0x2E; // Invert the CBZ/CBNZ comparison
		*pConditionalBranchInstruction = instruction;

		// Compute the extra space needed for the branch sequence
		m_lExtra = CalculateExtra(sizeof(USHORT), pDest, (BYTE*)(pDstInst));
		return sizeof(USHORT); // The source instruction was 16 bits
	}

	// If-Then Instruction (IT)
	if ((instruction >> 8 == 0xBF) && (instruction & 0xF)) { //(10111111xxxx(mask != 0b0000))
		// ToDo: Implement IT handler
		ASSERT(false);
		return sizeof(USHORT);
	}

	// ADD/SUB, SXTH, SXTB, UXTH, UXTB, CBZ, CBNZ, PUSH, POP, REV, REV15, REVSH, NOP, YIELD, WFE, WFI, SEV, etc.
	return PureCopy16(pSource, pDest);
}

BYTE CXHookDis::CopyConditionalBranchOrOther16(BYTE* pSource, BYTE* pDest)
{
	USHORT instruction = *(PUSHORT)(pSource);

	// Could be a conditional branch, an Undefined instruction or a Service System Call
	// Only the former needs special logic
	if ((instruction & 0xE00) != 0xE00) { // 1101(!=111x)xxxxxxxx
		LONG oldDelta = DecodeBranch8(instruction);
		PBYTE pTarget = CalculateTarget(pSource, oldDelta);
		m_pbTarget = pTarget;

		LONG newDelta = CalculateNewDelta(pTarget, pDest);
		instruction = EncodeBranch8(instruction, newDelta);
		if (instruction) {
			// Copy the 16 bit instruction over
			*(PUSHORT)(pDest) = instruction;
			return sizeof(USHORT); // The source instruction was 16 bits
		}

		// If that fails, re-encode as a sequence of branches
		// For example, bne +0x6E (0x90452) becomes:
		//
		// 001df758 d100     bne         001df75c
		// 001df75a e005     b           001df768
		// 001df75c e002     b           001df764
		// 001df75e bf00     nop
		// 001df760 0452     dc.h        0452
		// 001df762 0009     dc.h        0009
		// 001df764 f85ff008 ldr         pc,=0x90452
		//

		// First, reuse the existing conditional branch to, if successful, branch down to a 'long branch' that we will emit below
		USHORT newInstruction = EncodeBranch8(*(PUSHORT)(pSource), 0); // Due to the size of c_PCAdjust a zero-length branch moves 4 bytes forward, past the following unconditional branch
		ASSERT(newInstruction);
		PUSHORT pDstInst = (PUSHORT)(pDest);
		*pDstInst++ = newInstruction;

		// Next, prepare to insert an unconditional branch that will be hit if the condition above is not met.  This branch will branch over the following 'long branch'
		// We can't actually encode this branch yet though, because 'long branches' can vary in size
		PUSHORT pUnconditionalBranchInstruction = pDstInst++;

		// Then, emit a 'long branch' that will be hit if the original condition is met
		BYTE longBranchSize = EmitLongBranch(pDstInst, pTarget);

		// Finally, encode and emit the unconditional branch that will be used to branch past the 'long branch' if the initial condition was not met
		Branch11 branch11 = { 0x00, 0x1C };
		newInstruction = EncodeBranch11(*(DWORD*)(&branch11), longBranchSize - c_PCAdjust + sizeof(USHORT));
		ASSERT(newInstruction);
		*pUnconditionalBranchInstruction = newInstruction;

		// Compute the extra space needed for the branch sequence
		m_lExtra = CalculateExtra(sizeof(USHORT), pDest, (BYTE*)(pDstInst));
		return sizeof(USHORT); // The source instruction was 16 bits
	}

	return PureCopy16(pSource, pDest);
}

BYTE CXHookDis::CopyUnConditionalBranch16(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = *(PUSHORT)(pSource);

	LONG oldDelta = DecodeBranch11(instruction);
	PBYTE pTarget = CalculateTarget(pSource, oldDelta);
	m_pbTarget = pTarget;

	LONG newDelta = CalculateNewDelta(pTarget, pDest);
	instruction = EncodeBranch11(instruction, newDelta);
	if (instruction) {
		// Copy the 16 bit instruction over
		*(PUSHORT)(pDest) = (USHORT)instruction;
		return sizeof(USHORT); // The source instruction was 16 bits
	}

	// If that fails, re-encode as 32-bit
	PUSHORT pDstInst = (PUSHORT)(pDest);
	instruction = EncodeBranch24(0xf0009000, newDelta, FALSE);
	if (instruction) {
		// Copy both bytes of the instruction
		EmitLongInstruction(pDstInst, instruction);

		m_lExtra = sizeof(DWORD) - sizeof(USHORT); // The destination instruction was 32 bits
		return sizeof(USHORT); // The source instruction was 16 bits
	}

	// If that fails, emit as a 'long branch'
	if (!instruction) {
		// For example, b +0x7FE (00090be6) becomes:
		// 003f6d02 e001     b           003f6d08
		// 003f6d04 0be6     dc.h        0be6
		// 003f6d06 0009     dc.h        0009
		// 003f6d08 f85ff008 ldr         pc,=0x90BE6
		EmitLongBranch(pDstInst, pTarget);

		// Compute the extra space needed for the branch sequence
		m_lExtra = CalculateExtra(sizeof(USHORT), pDest, (BYTE*)(pDstInst));
		return sizeof(USHORT); // The source instruction was 16 bits
	}

	return sizeof(USHORT); // The source instruction was 16 bits
}

BYTE CXHookDis::CopyLiteralLoad16(BYTE* pSource, BYTE* pDest)
{
	PBYTE pStart = pDest;
	USHORT instruction = *(PUSHORT)(pSource);

	LONG oldDelta = DecodeLiteralLoad8(instruction);
	PBYTE pTarget = CalculateTarget(Align4(pSource), oldDelta);

	// Re-encode as a 'long literal load'
	// For example, ldr r0, [PC + 1E0] (0x905B4) becomes:
	//
	// 001df72c f85f0008 ldr         r0,=0x905B4
	// 001df730 f8d00000 ldr.w       r0,[r0]
	LiteralLoad8& load8 = (LiteralLoad8&)(instruction);
	EmitLongLiteralLoad((PUSHORT&)pDest, load8.Register, pTarget);

	m_lExtra = pDest - pStart - sizeof(USHORT);
	return sizeof(USHORT); // The source instruction was 16 bits
}

BYTE CXHookDis::CopyBranchExchangeOrDataProcessing16(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = *(PUSHORT)(pSource);

	// BX
	if ((instruction & 0xff80) == 0x4700) {
		// The target is stored in a register
		m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	}

	// AND, LSR, TST, ADD, CMP, MOV
	return PureCopy16(pSource, pDest);
}

const CXHookDis::COPYENTRY CXHookDis::s_rceTable[32] =
{
	// Shift by immediate, move register
	// ToDo: Not handling moves from PC
	/* 0b00000 */{ 0x00, &CXHookDis::PureCopy16 },
	/* 0b00001 */{ 0x01, &CXHookDis::PureCopy16 },
	/* 0b00010 */{ 0x02, &CXHookDis::PureCopy16 },

	// Add/subtract register
	// Add/subtract immediate
	/* 0b00011 */{ 0x03, &CXHookDis::PureCopy16 },

	// Add/subtract/compare/move immediate
	/* 0b00100 */{ 0x04, &CXHookDis::PureCopy16 },
	/* 0b00101 */{ 0x05, &CXHookDis::PureCopy16 },
	/* 0b00110 */{ 0x06, &CXHookDis::PureCopy16 },
	/* 0b00111 */{ 0x07, &CXHookDis::PureCopy16 },

	// Data-processing register
	// Special data processing
	// Branch/exchange instruction set
	/* 0b01000 */{ 0x08, &CXHookDis::CopyBranchExchangeOrDataProcessing16 },

	// Load from literal pool
	/* 0b01001 */{ 0x09, &CXHookDis::CopyLiteralLoad16 },

	// Load/store register offset
	/* 0b01010 */{ 0x0a, &CXHookDis::PureCopy16 },
	/* 0b01011 */{ 0x0b, &CXHookDis::PureCopy16 },

	//  Load/store word/byte immediate offset.
	/* 0b01100 */{ 0x0c, &CXHookDis::PureCopy16 },
	/* 0b01101 */{ 0x0d, &CXHookDis::PureCopy16 },
	/* 0b01110 */{ 0x0e, &CXHookDis::PureCopy16 },
	/* 0b01111 */{ 0x0f, &CXHookDis::PureCopy16 },

	//  Load/store halfword immediate offset.
	/* 0b10000 */{ 0x10, &CXHookDis::PureCopy16 },
	/* 0b10001 */{ 0x11, &CXHookDis::PureCopy16 },

	// Load from or store to stack
	/* 0b10010 */{ 0x12, &CXHookDis::PureCopy16 },
	/* 0b10011 */{ 0x13, &CXHookDis::PureCopy16 },

	// Add to SP or PC
	/* 0b10100 */{ 0x14, &CXHookDis::PureCopy16 },
	//   ToDo: Is ADR (T1) blitt-able?
	//     It adds a value to PC and stores the result in a register.
	//     Does this count as a 'target' for xhooks?
	/* 0b10101 */{ 0x15, &CXHookDis::PureCopy16 },

	// Miscellaneous
	/* 0b10110 */{ 0x16, &CXHookDis::CopyMiscellaneous16 },
	/* 0b10111 */{ 0x17, &CXHookDis::CopyMiscellaneous16 },

	// Load/store multiple
	/* 0b11000 */{ 0x18, &CXHookDis::PureCopy16 },
	/* 0b11001 */{ 0x19, &CXHookDis::PureCopy16 },
	//   ToDo: Are we sure these are all safe?
	//     LDMIA, for example, can include an 'embedded' branch.
	//     Does this count as a 'target' for xhooks?

	// Conditional branch
	/* 0b11010 */{ 0x1a, &CXHookDis::CopyConditionalBranchOrOther16 },

	// Conditional branch
	// Undefined instruction
	// Service (system) call
	/* 0b11011 */{ 0x1b, &CXHookDis::CopyConditionalBranchOrOther16 },

	// Unconditional branch
	/* 0b11100 */{ 0x1c, &CXHookDis::CopyUnConditionalBranch16 },

	// 32-bit instruction
	/* 0b11101 */{ 0x1d, &CXHookDis::BeginCopy32 },
	/* 0b11110 */{ 0x1e, &CXHookDis::BeginCopy32 },
	/* 0b11111 */{ 0x1f, &CXHookDis::BeginCopy32 }
};

BYTE CXHookDis::CopyBranch24(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = GetLongInstruction(pSource);
	BOOL fLink;
	LONG oldDelta = DecodeBranch24(instruction, fLink);
	PBYTE pTarget = CalculateTarget(pSource, oldDelta);
	m_pbTarget = pTarget;

	// Re-encode as 32-bit
	PUSHORT pDstInst = (PUSHORT)(pDest);
	LONG newDelta = CalculateNewDelta(pTarget, pDest);
	instruction = EncodeBranch24(instruction, newDelta, fLink);
	if (instruction) {
		// Copy both bytes of the instruction
		EmitLongInstruction(pDstInst, instruction);
		return sizeof(DWORD);
	}

	// If that fails, re-encode as a 'long branch'
	EmitLongBranch(pDstInst, pTarget);

	// Compute the extra space needed for the instruction
	m_lExtra = CalculateExtra(sizeof(DWORD), pDest, (BYTE*)(pDstInst));
	return sizeof(DWORD); // The source instruction was 32 bits
}

BYTE CXHookDis::CopyBranchOrMiscellaneous32(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = GetLongInstruction(pSource);
	if ((instruction & 0xf800d000) == 0xf0008000) { // B<c>.W <label>
		LONG oldDelta = DecodeBranch20(instruction);
		PBYTE pTarget = CalculateTarget(pSource, oldDelta);
		m_pbTarget = pTarget;

		// Re-encode as 32-bit
		PUSHORT pDstInst = (PUSHORT)(pDest);
		LONG newDelta = CalculateNewDelta(pTarget, pDest);
		instruction = EncodeBranch20(instruction, newDelta);
		if (instruction) {
			// Copy both bytes of the instruction
			EmitLongInstruction(pDstInst, instruction);
			return sizeof(DWORD);
		}

		// If that fails, re-encode as a sequence of branches
		// For example, bls.w +0x86 (00090480)| becomes:
		//
		// 001df788 f2408001 bls.w       001df78e
		// 001df78c e004     b           001df798
		// 001df78e e001     b           001df794
		// 001df790 0480     dc.h        0480
		// 001df792 0009     dc.h        0009
		// 001df794 f85ff008 ldr         pc,=0x90480
		//

		// First, reuse the existing conditional branch to, if successful,
		// branch down to a 'long branch' that we will emit below
		instruction = EncodeBranch20(GetLongInstruction(pSource), 2);
		// Due to the size of c_PCAdjust a two-length branch moves 6 bytes forward,
		// past the following unconditional branch
		ASSERT(instruction);
		EmitLongInstruction(pDstInst, instruction);

		// Next, prepare to insert an unconditional branch that will be hit
		// if the condition above is not met.  This branch will branch over
		// the following 'long branch'
		// We can't actually encode this branch yet though, because
		// 'long branches' can vary in size
		PUSHORT pUnconditionalBranchInstruction = pDstInst++;

		// Then, emit a 'long branch' that will be hit if the original condition is met
		BYTE longBranchSize = EmitLongBranch(pDstInst, pTarget);

		// Finally, encode and emit the unconditional branch that will be used
		// to branch past the 'long branch' if the initial condition was not met
		Branch11 branch11 = { 0x00, 0x1C };
		instruction = EncodeBranch11(*(DWORD*)(&branch11), longBranchSize - c_PCAdjust + sizeof(USHORT));
		ASSERT(instruction);
		*pUnconditionalBranchInstruction = static_cast<USHORT>(instruction);

		// Compute the extra space needed for the instruction
		m_lExtra = CalculateExtra(sizeof(DWORD), pDest, (BYTE*)(pDstInst));
		return sizeof(DWORD); // The source instruction was 32 bits
	}

	if ((instruction & 0xf800d000) == 0xf0009000) { // B.W <label>
		return CopyBranch24(pSource, pDest);
	}

	if ((instruction & 0xf800d000) == 0xf000d000) { // BL.W <label>
		// BL <label> 11110xxxxxxxxxxx11xxxxxxxxxxxxxx

		PUSHORT pDstInst = (PUSHORT)(pDest);
		ULONG instruction = GetLongInstruction(pSource);
		BOOL fLink;
		LONG oldDelta = DecodeBranch24(instruction, fLink);
		PBYTE pTarget = CalculateTarget(pSource, oldDelta);
		m_pbTarget = pTarget;

		*--((PULONG&)m_pbPool) = (ULONG)XHOOKS_PBYTE_TO_PFUNC(pTarget);

		// ldr lr, target.
		EmitLiteralLoad12(pDstInst, c_LR, m_pbPool);
		// blx lr
		EmitShortInstruction(pDstInst, 0x47f0);

		// Compute the extra space needed for the instruction
		m_lExtra = CalculateExtra(sizeof(DWORD), pDest, (BYTE*)(pDstInst));
		return sizeof(DWORD); // The source instruction was 32 bits
	}

	if ((instruction & 0xFFF0FFFF) == 0xF3C08F00) {
		// BXJ 111100111100xxxx1000111100000000
		// BXJ switches to Jazelle mode, which is not supported
		ASSERT(false);
	}

	if ((instruction & 0xFFFFFF00) == 0xF3DE8F00) {
		// SUBS PC, LR 111100111101111010001111xxxxxxxx
		m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	}

	// Everything else should be blitt-able
	return PureCopy32(pSource, pDest);
}

BYTE CXHookDis::CopyLiteralLoad32(BYTE* pSource, BYTE* pDest)
{
	BYTE* pStart = pDest;
	ULONG instruction = GetLongInstruction(pSource);

	LONG oldDelta = DecodeLiteralLoad12(instruction);
	PBYTE pTarget = CalculateTarget(Align4(pSource), oldDelta);

	LiteralLoad12& load = (LiteralLoad12&)(instruction);

	EmitLongLiteralLoad((PUSHORT&)pDest, load.Register, pTarget);

	m_lExtra = pDest - pStart - sizeof(DWORD);

	return sizeof(DWORD); // The source instruction was 32 bits
}

BYTE CXHookDis::CopyLoadAndStoreSingle(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = GetLongInstruction(pSource);

	// Note: The following masks only look at the interesting bits
	// (not the opCode prefix, since that check was performed in
	// order to get to this function)
	if (!(instruction & 0x100000)) {
		// 1111 100x xxx0 xxxxxxxxxxxxxxxxxxxx : STR, STRB, STRH, etc.
		return PureCopy32(pSource, pDest);
	}

	if ((instruction & 0xF81F0000) == 0xF81F0000) {
		// 1111100xxxx11111xxxxxxxxxxxxxxxx : PC +/- Imm12
		return CopyLiteralLoad32(pSource, pDest);
	}

	if ((instruction & 0xFE70F000) == 0xF81FF000) {
		// 1111100xx001xxxx1111xxxxxxxxxxxx : PLD, PLI
		// Convert PC-Relative PLD/PLI instructions to noops (1111100Xx00111111111xxxxxxxxxxxx)
		if ((instruction & 0xFE7FF000) == 0xF81FF000) {
			PUSHORT pDstInst = (PUSHORT)(pDest);
			*pDstInst++ = c_NOP;
			*pDstInst++ = c_NOP;
			return sizeof(DWORD);  // The source instruction was 32 bits
		}

		// All other PLD/PLI instructions are blitt-able
		return PureCopy32(pSource, pDest);
	}

	// If the load is writing to PC
	if ((instruction & 0xF950F000) == 0xF850F000) {
		m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	}

	// All other loads LDR (immediate), etc.
	return PureCopy32(pSource, pDest);
}

BYTE CXHookDis::CopyLoadAndStoreMultipleAndSRS(BYTE* pSource, BYTE* pDest)
{
	// Probably all blitt-able, although not positive since some of these can result in a branch (LDMIA, POP, etc.)
	return PureCopy32(pSource, pDest);
}

BYTE CXHookDis::CopyTableBranch(BYTE* pSource, BYTE* pDest)
{
	m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
	ULONG instruction = GetLongInstruction(pSource);
	TableBranch& tableBranch = (TableBranch&)(instruction);

	// If the base register is anything other than PC, we can simply copy the instruction
	if (tableBranch.BaseRegister != c_PC) {
		return PureCopy32(pSource, pDest);
	}

	__debugbreak();

	// If the base register is PC, we need to manually perform the table lookup
	// For example, this:
	//
	//        7ef40000 e8dff002 tbb         [pc,r2]
	//
	// becomes this:
	//
	//        7ef40404 b401     push        {r0}            ; pushed as a placeholder for the target address
	//        7ef40406 e92d0005 push.w      {r0,r2}         ; scratch register and another register are pushed; there's a minimum of two registers in the list for push.w
	//        7ef40410 4820     ldr         r0,=0x7EF40004  ; load the table address from the literal pool
	//        7ef40414 eb000042 add         r0,r0,r2,lsl #1 ; add the index value to the address of the table to get the table entry; lsl only used if it's a TBH instruction
	//        7ef40418 f8d00000 ldr.w       r0,[r0]         ; dereference the table entry to get the value of the target
	//        7ef4041c ea4f0040 lsl         r0,r0,#1        ; multiply the offset by 2 (per the spec)
	//        7ef40420 eb00000f add.w       r0,r0,pc        ; Add the offset to pc to get the target address
	//        7ef40424 f8cd000c str.w       r0,[sp,#0xC]    ; store the target address on the stack (into the first push)
	//        7ef40428 e8bd0005 pop.w       {r0,r2}         ; scratch register and another register are popped; there's a minimum of two registers in the list for pop.w
	//        7ef4042c bd00     pop         {pc}            ; pop the address into pc
	//

	// Push r0 to make room for our jump address on the stack
	PUSHORT pDstInst = (PUSHORT)(pDest);
	*pDstInst++ = 0xb401;

	// Locate a scratch register
	BYTE scrReg = 0;
	while (scrReg == tableBranch.IndexRegister)
	{
		++scrReg;
	}

	// Push scrReg and tableBranch.IndexRegister (push.w doesn't support pushing just 1 register)
	DWORD pushInstruction = 0xe92d0000;
	pushInstruction |= 1 << scrReg;
	pushInstruction |= 1 << tableBranch.IndexRegister;
	EmitLongInstruction(pDstInst, pushInstruction);

	// Write the target address out to the 'literal pool';
	// when the base register of a TBB/TBH is PC,
	// the branch table immediately follows the instruction
	BYTE* pTarget = CalculateTarget(pSource, 0);
	*--((PUSHORT&)m_pbPool) = ((ULONG)pTarget & 0xffff);
	*--((PUSHORT&)m_pbPool) = ((ULONG)pTarget >> 16);

	// Load the literal pool value into our scratch register (this contains the address of the branch table)
	// ldr rn, target
	EmitLiteralLoad8(pDstInst, scrReg, m_pbPool);

	// Add the index offset to the address of the branch table; the result will be the value within the table that contains the branch offset
	// We need to multiply the index by two if we are using halfword indexing
	// Will shift tableBranch.IndexRegister by 1 (multiply by 2) if using a TBH
	EmitAdd32(pDstInst, scrReg, tableBranch.IndexRegister, scrReg, tableBranch.HalfWord);

	// Dereference rn into rn, to load the value within the table
	// ldr rn, [rn]
	if (scrReg < 0x7) {
		EmitImmediateRegisterLoad16(pDstInst, scrReg);
	}
	else {
		EmitImmediateRegisterLoad32(pDstInst, scrReg);
	}

	// Multiply the offset by two to get the true offset value (as per the spec)
	EmitLogicalShiftLeft32(pDstInst, scrReg, scrReg, 1);

	// Add the offset to PC to get the target
	EmitAdd32(pDstInst, scrReg, c_PC, scrReg, 0);

	// Now write the contents of scrReg to the stack, so we can pop it into PC
	// Write the address of the branch table entry to the stack, so we can pop it into PC
	EmitStoreImmediate12(pDstInst, scrReg, c_SP, sizeof(DWORD) * 3);

	// Pop scrReg and tableBranch.IndexRegister (pop.w doesn't support popping just 1 register)
	DWORD popInstruction = 0xe8bd0000;
	popInstruction |= 1 << scrReg;
	popInstruction |= 1 << tableBranch.IndexRegister;
	EmitLongInstruction(pDstInst, popInstruction);

	// Pop PC
	*pDstInst++ = 0xbd00;

	// Compute the extra space needed for the branch sequence
	m_lExtra = CalculateExtra(sizeof(USHORT), pDest, (BYTE*)(pDstInst));
	return sizeof(DWORD);
}

BYTE CXHookDis::BeginCopy32(BYTE* pSource, BYTE* pDest)
{
	ULONG instruction = GetLongInstruction(pSource);

	// Immediate data processing instructions; ADD, SUB, MOV, MOVN, ADR, MOVT, BFC, SSAT16, etc.
	if ((instruction & 0xF8008000) == 0xF0000000) { // 11110xxxxxxxxxxx0xxxxxxxxxxxxxxx
		// Should all be blitt-able
		// ToDo: What about ADR?  Is it safe to do a straight-copy?
		// ToDo: Not handling moves to or from PC
		return PureCopy32(pSource, pDest);
	}

	// Non-Immediate data processing instructions; ADD, EOR, TST, etc.
	if ((instruction & 0xEE000000) == 0xEA000000) { // 111x101xxxxxxxxxxxxxxxxxxxxxxx
		// Should all be blitt-able
		return PureCopy32(pSource, pDest);
	}

	// Load and store single data item, memory hints
	if ((instruction & 0xFE000000) == 0xF8000000) { // 1111100xxxxxxxxxxxxxxxxxxxxxxxxx
		return CopyLoadAndStoreSingle(pSource, pDest);
	}

	// Load and store, double and exclusive, and table branch
	if ((instruction & 0xFE400000) == 0xE8400000) { // 1110100xx1xxxxxxxxxxxxxxxxxxxxxx
		// Load and store double
		if (instruction & 0x1200000) {
			// LDRD, STRD (immediate) : xxxxxxxPxxWxxxxxxxxxxxxxxxxxxxxx where PW != 0b00
			// The source register is PC
			if ((instruction & 0xF0000) == 0xF0000) {
				// ToDo: If the source register is PC, what should we do?
				ASSERT(false);
			}

			// If either target registers are PC
			if (((instruction & 0xF000) == 0xF000) ||
				((instruction & 0xF00) == 0xF00)) {
				m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
			}

			return PureCopy32(pSource, pDest);
		}

		// Load and store exclusive
		if (!(instruction & 0x800000)) { // LDREX, STREX : xxxxxxxx0xxxxxxxxxxxxxxxxxxxxxxx
			if ((instruction & 0xF000) == 0xF000) { // xxxxxxxxxxxx1111xxxxxxxxxxxx
				m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_DYNAMIC;
			}
			return PureCopy32(pSource, pDest);
		}

		// Table branch
		if ((instruction & 0x1000F0) == 0x100000 ||  // TBB : xxxxxxxxxxx1xxxxxxxxxxxx0000xxxx
			(instruction & 0x1000F0) == 0x100010) { // TBH : xxxxxxxxxxx1xxxxxxxxxxxx0001xxxx
			return CopyTableBranch(pSource, pDest);
		}

		// Load and store exclusive byte, halfword, doubleword (LDREXB, LDREXH, LDREXD, STREXB, STREXH, STREXD, etc.)
		return PureCopy32(pSource, pDest);
	}

	// Load and store multiple, RFE and SRS
	if ((instruction & 0xFE400000) == 0xE8000000) { // 1110100xx0xxxxxxxxxxxxxxxxxxxxxx
		// Return from exception (RFE)
		if ((instruction & 0xE9900000) == 0xE9900000 || // 1110100110x1xxxxxxxxxxxxxxxxxxxx
			(instruction & 0xE8100000) == 0xE8100000) { // 1110100000x1xxxxxxxxxxxxxxxxxxxx
			return PureCopy32(pSource, pDest);
		}

		return CopyLoadAndStoreMultipleAndSRS(pSource, pDest);
	}

	// Branches, miscellaneous control
	if ((instruction & 0xF8008000) == 0xF0008000) { // 11110xxxxxxxxxxx0xxxxxxxxxxxxxxx
		// Branches, miscellaneous control
		return CopyBranchOrMiscellaneous32(pSource, pDest);
	}

	// Coprocessor instructions
	if ((instruction & 0xEC000000) == 0xEC000000) { // 111x11xxxxxxxxxxxxxxxxxxxxxxxxxx
		return PureCopy32(pSource, pDest);
	}

	// Unhandled instruction; should never make it this far
	ASSERT(false);
	return PureCopy32(pSource, pDest);
}

/////////////////////////////////////////////////////////// Disassembler Code.
//
CXHookDis::CXHookDis()
{
	m_pbTarget = (PBYTE)XHOOK_INSTRUCTION_TARGET_NONE;;
	m_pbPool = NULL;
	m_lExtra = 0;
}

PBYTE CXHookDis::CopyInstruction(PBYTE pDst,
	PBYTE *ppDstPool,
	PBYTE pSrc,
	PBYTE *ppTarget,
	LONG *plExtra)
{
	if (pDst && ppDstPool && ppDstPool != NULL) {
		m_pbPool = (PBYTE)*ppDstPool;
	}
	else {
		pDst = m_rbScratchDst;
		m_pbPool = m_rbScratchDst + sizeof(m_rbScratchDst);
	}
	// Make sure the constant pool is 32-bit aligned.
	m_pbPool -= ((ULONG_PTR)m_pbPool) & 3;

	REFCOPYENTRY pEntry = &s_rceTable[pSrc[1] >> 3];
	ULONG size = (this->*pEntry->pfCopy)(pSrc, pDst);

	pSrc += size;

	// If the target is needed, store our target
	if (ppTarget) {
		*ppTarget = m_pbTarget;
	}
	if (plExtra) {
		*plExtra = m_lExtra;
	}
	if (ppDstPool) {
		*ppDstPool = m_pbPool;
	}

	return pSrc;
}


PVOID WINAPI XHookCopyInstruction(PVOID pDst,
	PVOID *ppDstPool,
	PVOID pSrc,
	PVOID *ppTarget,
	LONG *plExtra)
{
	CXHookDis state;
	return (PVOID)state.CopyInstruction((PBYTE)pDst,
		(PBYTE*)ppDstPool,
		(PBYTE)pSrc,
		(PBYTE*)ppTarget,
		plExtra);
}

#endif // XHOOKS_ARM

#define IMPORT_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]
#define BOUND_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT]
#define CLR_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR]
#define IAT_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT]

//////////////////////////////////////////////////////////////////////////////
//
#ifndef _STRSAFE_H_INCLUDED_
static inline HRESULT StringCchLengthA(const char* psz, size_t cchMax, size_t* pcch)
{
	HRESULT hr = S_OK;
	size_t cchMaxPrev = cchMax;

	if (cchMax > 2147483647)
	{
		return ERROR_INVALID_PARAMETER;
	}

	while (cchMax && (*psz != '\0'))
	{
		psz++;
		cchMax--;
	}

	if (cchMax == 0)
	{
		// the string is longer than cchMax
		hr = ERROR_INVALID_PARAMETER;
	}

	if (SUCCEEDED(hr) && pcch)
	{
		*pcch = cchMaxPrev - cchMax;
	}

	return hr;
}

static inline HRESULT StringCchCopyA(char* pszDest, size_t cchDest, const char* pszSrc)
{
	HRESULT hr = S_OK;

	if (cchDest == 0)
	{
		// can not null terminate a zero-byte dest buffer
		hr = ERROR_INVALID_PARAMETER;
	}
	else
	{
		while (cchDest && (*pszSrc != '\0'))
		{
			*pszDest++ = *pszSrc++;
			cchDest--;
		}

		if (cchDest == 0)
		{
			// we are going to truncate pszDest
			pszDest--;
			hr = ERROR_INVALID_PARAMETER;
		}

		*pszDest = '\0';
	}

	return hr;
}

static inline HRESULT StringCchCatA(char* pszDest, size_t cchDest, const char* pszSrc)
{
	HRESULT hr;
	size_t cchDestCurrent;

	if (cchDest > 2147483647)
	{
		return ERROR_INVALID_PARAMETER;
	}

	hr = StringCchLengthA(pszDest, cchDest, &cchDestCurrent);

	if (SUCCEEDED(hr))
	{
		hr = StringCchCopyA(pszDest + cchDestCurrent,
			cchDest - cchDestCurrent,
			pszSrc);
	}

	return hr;
}

#endif

//////////////////////////////////////////////////////////////////////////////
//
#if IGNORE_CHECKSUMS
static WORD xhook_sum_minus(WORD wSum, WORD wMinus)
{
	wSum = (WORD)(wSum - ((wSum < wMinus) ? 1 : 0));
	wSum = (WORD)(wSum - wMinus);
	return wSum;
}

static WORD xhook_sum_done(DWORD PartialSum)
{
	// Fold final carry into a single word result and return the resultant value.
	return (WORD)(((PartialSum >> 16) + PartialSum) & 0xffff);
}

static WORD xhook_sum_data(DWORD dwSum, PBYTE pbData, DWORD cbData)
{
	while (cbData > 0) {
		dwSum += *((PWORD&)pbData)++;
		dwSum = (dwSum >> 16) + (dwSum & 0xffff);
		cbData -= sizeof(WORD);
	}
	return xhook_sum_done(dwSum);
}

static WORD xhook_sum_final(WORD wSum, PIMAGE_NT_HEADERS pinh)
{
	XHOOK_TRACE((".... : %08x (value: %08x)\n", wSum, pinh->OptionalHeader.CheckSum));

	// Subtract the two checksum words in the optional header from the computed.
	wSum = xhook_sum_minus(wSum, ((PWORD)(&pinh->OptionalHeader.CheckSum))[0]);
	wSum = xhook_sum_minus(wSum, ((PWORD)(&pinh->OptionalHeader.CheckSum))[1]);

	return wSum;
}

static WORD ChkSumRange(WORD wSum, HANDLE hProcess, PBYTE pbBeg, PBYTE pbEnd)
{
	BYTE rbPage[4096];

	while (pbBeg < pbEnd) {
		if (!ReadProcessMemory(hProcess, pbBeg, rbPage, sizeof(rbPage), NULL)) {
			XHOOK_TRACE(("ReadProcessMemory(chk@%p..%p) failed: %d\n",
				pbBeg, pbEnd, GetLastError()));
			break;
		}
		wSum = xhook_sum_data(wSum, rbPage, sizeof(rbPage));
		pbBeg += sizeof(rbPage);
	}
	return wSum;
}

static WORD ComputeChkSum(HANDLE hProcess, PBYTE pbModule, PIMAGE_NT_HEADERS pinh)
{
	// See LdrVerifyMappedImageMatchesChecksum.

	MEMORY_BASIC_INFORMATION mbi;
	ZeroMemory(&mbi, sizeof(mbi));
	WORD wSum = 0;

	PBYTE pbLast = pbModule;
	for (;; pbLast = (PBYTE)mbi.BaseAddress + mbi.RegionSize) {
		ZeroMemory(&mbi, sizeof(mbi));
		if (VirtualQueryEx(hProcess, (PVOID)pbLast, &mbi, sizeof(mbi)) == 0) {
			if (GetLastError() == ERROR_INVALID_PARAMETER) {
				break;
			}
			XHOOK_TRACE(("VirtualQueryEx(%p) failed: %d\n",
				pbLast, GetLastError()));
			break;
		}

		if (mbi.AllocationBase != pbModule) {
			break;
		}

		wSum = ChkSumRange(wSum,
			hProcess,
			(PBYTE)mbi.BaseAddress,
			(PBYTE)mbi.BaseAddress + mbi.RegionSize);

		XHOOK_TRACE(("[%p..%p] : %04x\n",
			(PBYTE)mbi.BaseAddress,
			(PBYTE)mbi.BaseAddress + mbi.RegionSize,
			wSum));
	}

	return xhook_sum_final(wSum, pinh);
}
#endif // IGNORE_CHECKSUMS

//////////////////////////////////////////////////////////////////////////////
//
// Enumate through modules in the target process.
//
static HMODULE WINAPI EnumerateModulesInProcess(HANDLE hProcess,
	HMODULE hModuleLast,
	PIMAGE_NT_HEADERS32 pNtHeader)
{
	PBYTE pbLast;

	if (hModuleLast == NULL) {
		pbLast = (PBYTE)0x10000;
	}
	else {
		pbLast = (PBYTE)hModuleLast + 0x10000;
	}

	MEMORY_BASIC_INFORMATION mbi;
	ZeroMemory(&mbi, sizeof(mbi));

	// Find the next memory region that contains a mapped PE image.
	//

	for (;; pbLast = (PBYTE)mbi.BaseAddress + mbi.RegionSize) {
		if (VirtualQueryEx(hProcess, (PVOID)pbLast, &mbi, sizeof(mbi)) == 0) {
			break;
		}
		if ((mbi.RegionSize & 0xfff) == 0xfff) {
			break;
		}
		if (((PBYTE)mbi.BaseAddress + mbi.RegionSize) < pbLast) {
			break;
		}

		// Skip uncommitted regions and guard pages.
		//
		if ((mbi.State != MEM_COMMIT) ||
			((mbi.Protect & 0xff) == PAGE_NOACCESS) ||
			(mbi.Protect & PAGE_GUARD)) {
			continue;
		}

		__try {
			IMAGE_DOS_HEADER idh;
			if (!ReadProcessMemory(hProcess, pbLast, &idh, sizeof(idh), NULL)) {
				XHOOK_TRACE(("ReadProcessMemory(idh@%p..%p) failed: %d\n",
					pbLast, pbLast + sizeof(idh), GetLastError()));
				continue;
			}

			if (idh.e_magic != IMAGE_DOS_SIGNATURE ||
				(DWORD)idh.e_lfanew > mbi.RegionSize ||
				(DWORD)idh.e_lfanew < sizeof(idh)) {
				continue;
			}

			if (!ReadProcessMemory(hProcess, pbLast + idh.e_lfanew,
				pNtHeader, sizeof(*pNtHeader), NULL)) {
				XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p:%p) failed: %d\n",
					pbLast + idh.e_lfanew,
					pbLast + idh.e_lfanew + sizeof(*pNtHeader),
					pbLast,
					GetLastError()));
				continue;
			}

			if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
				continue;
			}

			return (HMODULE)pbLast;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}
	}
	return NULL;
}

//////////////////////////////////////////////////////////////////////////////
//
// Find a region of memory in which we can create a replacement import table.
//
static PBYTE FindAndAllocateNearBase(HANDLE hProcess, PBYTE pbBase, DWORD cbAlloc)
{
	MEMORY_BASIC_INFORMATION mbi;
	ZeroMemory(&mbi, sizeof(mbi));

	PBYTE pbLast = pbBase;
	for (;; pbLast = (PBYTE)mbi.BaseAddress + mbi.RegionSize) {

		ZeroMemory(&mbi, sizeof(mbi));
		if (VirtualQueryEx(hProcess, (PVOID)pbLast, &mbi, sizeof(mbi)) == 0) {
			if (GetLastError() == ERROR_INVALID_PARAMETER) {
				break;
			}
			XHOOK_TRACE(("VirtualQueryEx(%p) failed: %d\n",
				pbLast, GetLastError()));
			break;
		}
		if ((mbi.RegionSize & 0xfff) == 0xfff) {
			break;
		}

		// Skip anything other than a pure free region.
		//
		if (mbi.State != MEM_FREE) {
			continue;
		}

		PBYTE pbAddress = (PBYTE)(((DWORD_PTR)mbi.BaseAddress + 0xffff) & ~(DWORD_PTR)0xffff);

		XHOOK_TRACE(("Free region %p..%p\n",
			mbi.BaseAddress,
			(PBYTE)mbi.BaseAddress + mbi.RegionSize));

		for (; pbAddress < (PBYTE)mbi.BaseAddress + mbi.RegionSize; pbAddress += 0x10000) {
			PBYTE pbAlloc = (PBYTE)VirtualAllocEx(hProcess, pbAddress, cbAlloc,
				MEM_RESERVE, PAGE_READWRITE);
			if (pbAlloc == NULL) {
				XHOOK_TRACE(("VirtualAllocEx(%p) failed: %d\n", pbAddress, GetLastError()));
				continue;
			}
			pbAlloc = (PBYTE)VirtualAllocEx(hProcess, pbAddress, cbAlloc,
				MEM_COMMIT, PAGE_READWRITE);
			if (pbAlloc == NULL) {
				XHOOK_TRACE(("VirtualAllocEx(%p) failed: %d\n", pbAddress, GetLastError()));
				continue;
			}
			XHOOK_TRACE(("[%p..%p] Allocated for import table.\n",
				pbAlloc, pbAlloc + cbAlloc));
			return pbAlloc;
		}
	}
	return NULL;
}

static inline DWORD PadToDword(DWORD dw)
{
	return (dw + 3) & ~3u;
}

static inline DWORD PadToDwordPtr(DWORD dw)
{
	return (dw + 7) & ~7u;
}

static inline HRESULT ReplaceOptionalSizeA(char* pszDest,
	size_t cchDest,
	const char* pszSize)
{
	if (cchDest == 0 || pszDest == NULL || pszSize == NULL ||
		pszSize[0] == '\0' || pszSize[1] == '\0' || pszSize[2] != '\0') {

		// can not write into empty buffer or with string other than two chars.
		return ERROR_INVALID_PARAMETER;
	}
	else {
		for (; cchDest >= 2; cchDest--, pszDest++) {
			if (pszDest[0] == '?' && pszDest[1] == '?') {
				pszDest[0] = pszSize[0];
				pszDest[1] = pszSize[1];
				break;
			}
		}
	}
	return S_OK;
}

//////////////////////////////////////////////////////////////////////////////
//
#if XHOOKS_32BIT
#define DWORD_XX                        DWORD32
#define IMAGE_NT_HEADERS_XX             IMAGE_NT_HEADERS32
#define IMAGE_NT_OPTIONAL_HDR_MAGIC_XX  IMAGE_NT_OPTIONAL_HDR32_MAGIC
#define IMAGE_ORDINAL_FLAG_XX           IMAGE_ORDINAL_FLAG32
#define UPDATE_IMPORTS_XX               UpdateImports32
#define XHOOKS_BITS_XX                 32
#include "uimports.cpp"
#undef XHOOK_EXE_RESTORE_FIELD_XX
#undef DWORD_XX
#undef IMAGE_NT_HEADERS_XX
#undef IMAGE_NT_OPTIONAL_HDR_MAGIC_XX
#undef IMAGE_ORDINAL_FLAG_XX
#undef UPDATE_IMPORTS_XX
#endif // XHOOKS_32BIT

#if XHOOKS_64BIT
#define DWORD_XX                        DWORD64
#define IMAGE_NT_HEADERS_XX             IMAGE_NT_HEADERS64
#define IMAGE_NT_OPTIONAL_HDR_MAGIC_XX  IMAGE_NT_OPTIONAL_HDR64_MAGIC
#define IMAGE_ORDINAL_FLAG_XX           IMAGE_ORDINAL_FLAG64
#define UPDATE_IMPORTS_XX               UpdateImports64
#define XHOOKS_BITS_XX                 64
#include "uimports.cpp"
#undef XHOOK_EXE_RESTORE_FIELD_XX
#undef DWORD_XX
#undef IMAGE_NT_HEADERS_XX
#undef IMAGE_NT_OPTIONAL_HDR_MAGIC_XX
#undef IMAGE_ORDINAL_FLAG_XX
#undef UPDATE_IMPORTS_XX
#endif // XHOOKS_64BIT

//////////////////////////////////////////////////////////////////////////////
//
#if XHOOKS_64BIT

C_ASSERT(sizeof(IMAGE_NT_HEADERS64) == sizeof(IMAGE_NT_HEADERS32) + 16);

static BOOL UpdateFrom32To64(HANDLE hProcess, HANDLE hModule, WORD machine)
{
	IMAGE_DOS_HEADER idh;
	IMAGE_NT_HEADERS32 inh32;
	IMAGE_NT_HEADERS64 inh64;
	IMAGE_SECTION_HEADER sects[32];
	PBYTE pbModule = (PBYTE)hModule;
	DWORD n;

	ZeroMemory(&inh32, sizeof(inh32));
	ZeroMemory(&inh64, sizeof(inh64));
	ZeroMemory(sects, sizeof(sects));

	XHOOK_TRACE(("UpdateFrom32To64(%04x)\n", machine));
	//////////////////////////////////////////////////////// Read old headers.
	//
	if (!ReadProcessMemory(hProcess, pbModule, &idh, sizeof(idh), NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(idh@%p..%p) failed: %d\n",
			pbModule, pbModule + sizeof(idh), GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("ReadProcessMemory(idh@%p..%p)\n",
		pbModule, pbModule + sizeof(idh)));

	PBYTE pnh = pbModule + idh.e_lfanew;
	if (!ReadProcessMemory(hProcess, pnh, &inh32, sizeof(inh32), NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p) failed: %d\n",
			pnh, pnh + sizeof(inh32), GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p)\n", pnh, pnh + sizeof(inh32)));

	if (inh32.FileHeader.NumberOfSections > (sizeof(sects) / sizeof(sects[0]))) {
		return FALSE;
	}

	PBYTE psects = pnh +
		FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) +
		inh32.FileHeader.SizeOfOptionalHeader;
	ULONG cb = inh32.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
	if (!ReadProcessMemory(hProcess, psects, &sects, cb, NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(ish@%p..%p) failed: %d\n",
			psects, psects + cb, GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("ReadProcessMemory(ish@%p..%p)\n", psects, psects + cb));

	////////////////////////////////////////////////////////// Convert header.
	//
	inh64.Signature = inh32.Signature;
	inh64.FileHeader = inh32.FileHeader;
	inh64.FileHeader.Machine = machine;
	inh64.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);

	inh64.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
	inh64.OptionalHeader.MajorLinkerVersion = inh32.OptionalHeader.MajorLinkerVersion;
	inh64.OptionalHeader.MinorLinkerVersion = inh32.OptionalHeader.MinorLinkerVersion;
	inh64.OptionalHeader.SizeOfCode = inh32.OptionalHeader.SizeOfCode;
	inh64.OptionalHeader.SizeOfInitializedData = inh32.OptionalHeader.SizeOfInitializedData;
	inh64.OptionalHeader.SizeOfUninitializedData = inh32.OptionalHeader.SizeOfUninitializedData;
	inh64.OptionalHeader.AddressOfEntryPoint = inh32.OptionalHeader.AddressOfEntryPoint;
	inh64.OptionalHeader.BaseOfCode = inh32.OptionalHeader.BaseOfCode;
	inh64.OptionalHeader.ImageBase = inh32.OptionalHeader.ImageBase;
	inh64.OptionalHeader.SectionAlignment = inh32.OptionalHeader.SectionAlignment;
	inh64.OptionalHeader.FileAlignment = inh32.OptionalHeader.FileAlignment;
	inh64.OptionalHeader.MajorOperatingSystemVersion
		= inh32.OptionalHeader.MajorOperatingSystemVersion;
	inh64.OptionalHeader.MinorOperatingSystemVersion
		= inh32.OptionalHeader.MinorOperatingSystemVersion;
	inh64.OptionalHeader.MajorImageVersion = inh32.OptionalHeader.MajorImageVersion;
	inh64.OptionalHeader.MinorImageVersion = inh32.OptionalHeader.MinorImageVersion;
	inh64.OptionalHeader.MajorSubsystemVersion = inh32.OptionalHeader.MajorSubsystemVersion;
	inh64.OptionalHeader.MinorSubsystemVersion = inh32.OptionalHeader.MinorSubsystemVersion;
	inh64.OptionalHeader.Win32VersionValue = inh32.OptionalHeader.Win32VersionValue;
	inh64.OptionalHeader.SizeOfImage = inh32.OptionalHeader.SizeOfImage;
	inh64.OptionalHeader.SizeOfHeaders = inh32.OptionalHeader.SizeOfHeaders;
	inh64.OptionalHeader.CheckSum = inh32.OptionalHeader.CheckSum;
	inh64.OptionalHeader.Subsystem = inh32.OptionalHeader.Subsystem;
	inh64.OptionalHeader.DllCharacteristics = inh32.OptionalHeader.DllCharacteristics;
	inh64.OptionalHeader.SizeOfStackReserve = inh32.OptionalHeader.SizeOfStackReserve;
	inh64.OptionalHeader.SizeOfStackCommit = inh32.OptionalHeader.SizeOfStackCommit;
	inh64.OptionalHeader.SizeOfHeapReserve = inh32.OptionalHeader.SizeOfHeapReserve;
	inh64.OptionalHeader.SizeOfHeapCommit = inh32.OptionalHeader.SizeOfHeapCommit;
	inh64.OptionalHeader.LoaderFlags = inh32.OptionalHeader.LoaderFlags;
	inh64.OptionalHeader.NumberOfRvaAndSizes = inh32.OptionalHeader.NumberOfRvaAndSizes;
	for (n = 0; n < IMAGE_NUMBEROF_DIRECTORY_ENTRIES; n++) {
		inh64.OptionalHeader.DataDirectory[n] = inh32.OptionalHeader.DataDirectory[n];
	}

	inh64.IMPORT_DIRECTORY.VirtualAddress = 0;
	inh64.IMPORT_DIRECTORY.Size = 0;

	/////////////////////////////////////////////////////// Write new headers.
	//
	DWORD dwProtect = 0;
	if (!VirtualProtectEx(hProcess, pbModule, inh64.OptionalHeader.SizeOfHeaders,
		PAGE_EXECUTE_READWRITE, &dwProtect)) {
		return FALSE;
	}

	if (!WriteProcessMemory(hProcess, pnh, &inh64, sizeof(inh64), NULL)) {
		XHOOK_TRACE(("WriteProcessMemory(inh@%p..%p) failed: %d\n",
			pnh, pnh + sizeof(inh64), GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("WriteProcessMemory(inh@%p..%p)\n", pnh, pnh + sizeof(inh64)));

	psects = pnh +
		FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) +
		inh64.FileHeader.SizeOfOptionalHeader;
	cb = inh64.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER);
	if (!WriteProcessMemory(hProcess, psects, &sects, cb, NULL)) {
		XHOOK_TRACE(("WriteProcessMemory(ish@%p..%p) failed: %d\n",
			psects, psects + cb, GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("WriteProcessMemory(ish@%p..%p)\n", psects, psects + cb));

	DWORD dwOld = 0;
	if (!VirtualProtectEx(hProcess, pbModule, inh64.OptionalHeader.SizeOfHeaders,
		dwProtect, &dwOld)) {
		return FALSE;
	}

	return TRUE;
}
#endif // XHOOKS_64BIT

//////////////////////////////////////////////////////////////////////////////
//
BOOL WINAPI XHookUpdateProcessWithDll(HANDLE hProcess, LPCSTR *plpDlls, DWORD nDlls)
{
	// Find the next memory region that contains a mapped PE image.
	//
	WORD mach32Bit = 0;
	WORD mach64Bit = 0;
	WORD exe32Bit = 0;
	HMODULE hModule = NULL;
	HMODULE hLast = NULL;

	for (;;) {
		IMAGE_NT_HEADERS32 inh;

		if ((hLast = EnumerateModulesInProcess(hProcess, hLast, &inh)) == NULL) {
			break;
		}

		XHOOK_TRACE(("%p  machine=%04x magic=%04x\n",
			hLast, inh.FileHeader.Machine, inh.OptionalHeader.Magic));

		if ((inh.FileHeader.Characteristics & IMAGE_FILE_DLL) == 0) {
			hModule = hLast;
			if (inh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
				exe32Bit = inh.FileHeader.Machine;
			}
			XHOOK_TRACE(("%p  Found EXE\n", hLast));
		}
		else {
			if (inh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
				mach32Bit = inh.FileHeader.Machine;
			}
			else if (inh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
				mach64Bit = inh.FileHeader.Machine;
			}
		}
	}
	XHOOK_TRACE(("    mach32Bit=%04x mach64Bit=%04x\n", mach32Bit, mach64Bit));

	if (hModule == NULL) {
		SetLastError(ERROR_INVALID_OPERATION);
		return FALSE;
	}

	// Save the various headers for XHookRestoreAfterWith.
	//
	XHOOK_EXE_RESTORE der;
	ZeroMemory(&der, sizeof(der));
	der.cb = sizeof(der);

	der.pidh = (PBYTE)hModule;
	der.cbidh = sizeof(der.idh);
	if (!ReadProcessMemory(hProcess, der.pidh, &der.idh, sizeof(der.idh), NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(idh@%p..%p) failed: %d\n",
			der.pidh, der.pidh + der.cbidh, GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("IDH: %p..%p\n", der.pidh, der.pidh + der.cbidh));

	// We read the NT header in two passes to get the full size.
	// First we read just the Signature and FileHeader.
	der.pinh = der.pidh + der.idh.e_lfanew;
	der.cbinh = FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader);
	if (!ReadProcessMemory(hProcess, der.pinh, &der.inh, der.cbinh, NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p) failed: %d\n",
			der.pinh, der.pinh + der.cbinh, GetLastError()));
		return FALSE;
	}

	// Second we read the OptionalHeader and Section headers.
	der.cbinh = (FIELD_OFFSET(IMAGE_NT_HEADERS, OptionalHeader) +
		der.inh.FileHeader.SizeOfOptionalHeader +
		der.inh.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER));
#if XHOOKS_64BIT
	if (exe32Bit && !mach32Bit) {
		// Include the Save the extra 16-bytes that will be overwritten with 64-bit header.
		der.cbinh += sizeof(IMAGE_NT_HEADERS64) - sizeof(IMAGE_NT_HEADERS32);
	}
#endif // XHOOKS_64BIT

	if (der.cbinh > sizeof(der.raw)) {
		return FALSE;
	}

	if (!ReadProcessMemory(hProcess, der.pinh, &der.inh, der.cbinh, NULL)) {
		XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p) failed: %d\n",
			der.pinh, der.pinh + der.cbinh, GetLastError()));
		return FALSE;
	}
	XHOOK_TRACE(("INH: %p..%p\n", der.pinh, der.pinh + der.cbinh));

	// Third, we read the CLR header

	if (der.inh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
		if (der.inh32.CLR_DIRECTORY.VirtualAddress != 0 &&
			der.inh32.CLR_DIRECTORY.Size != 0) {
		}
		XHOOK_TRACE(("CLR32.VirtAddr=%x, CLR.Size=%x\n",
			der.inh32.CLR_DIRECTORY.VirtualAddress,
			der.inh32.CLR_DIRECTORY.Size));

		der.pclr = ((PBYTE)hModule) + der.inh32.CLR_DIRECTORY.VirtualAddress;
	}
	else if (der.inh.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
		if (der.inh64.CLR_DIRECTORY.VirtualAddress != 0 &&
			der.inh64.CLR_DIRECTORY.Size != 0) {
		}

		XHOOK_TRACE(("CLR64.VirtAddr=%x, CLR.Size=%x\n",
			der.inh64.CLR_DIRECTORY.VirtualAddress,
			der.inh64.CLR_DIRECTORY.Size));

		der.pclr = ((PBYTE)hModule) + der.inh64.CLR_DIRECTORY.VirtualAddress;
	}

	if (der.pclr != 0) {
		der.cbclr = sizeof(der.clr);
		if (!ReadProcessMemory(hProcess, der.pclr, &der.clr, der.cbclr, NULL)) {
			XHOOK_TRACE(("ReadProcessMemory(clr@%p..%p) failed: %d\n",
				der.pclr, der.pclr + der.cbclr, GetLastError()));
			return FALSE;
		}
		XHOOK_TRACE(("CLR: %p..%p\n", der.pclr, der.pclr + der.cbclr));
	}

	// Fourth, adjust for a 32-bit WOW64 process.

	if (exe32Bit && mach64Bit) {
		if (!der.pclr                       // Native binary
			|| (der.clr.Flags & 1) == 0     // Or mixed-mode MSIL
			|| (der.clr.Flags & 2) != 0) {  // Or 32BIT Required MSIL

			mach64Bit = 0;
			if (mach32Bit == 0) {
				mach32Bit = exe32Bit;
			}
		}
	}

	// Now decide if we can insert the xhook.

#if XHOOKS_32BIT
	if (!mach32Bit && mach64Bit) {
		// 64-bit native or 64-bit managed process.
		//
		// Can't xhook a 64-bit process with 32-bit code.
		// Note: This happens for 32-bit PE binaries containing only
		// manage code that have been marked as 64-bit ready.
		//
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	else if (mach32Bit) {
		// 32-bit native or 32-bit managed process on any platform.
		if (!UpdateImports32(hProcess, hModule, plpDlls, nDlls)) {
			return FALSE;
		}
	}
	else {
		// Who knows!?
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
#endif // XHOOKS_32BIT

#if XHOOKS_64BIT
	if (mach32Bit) {
		// Can't xhook a 32-bit process with 64-bit code.
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
	else if (exe32Bit && !mach32Bit) {
		// Try to convert the 32-bit managed binary to a 64-bit managed binary.
		if (!UpdateFrom32To64(hProcess, hModule, mach64Bit)) {
			return FALSE;
		}

		// 64-bit process from 32-bit managed binary.
		if (!UpdateImports64(hProcess, hModule, plpDlls, nDlls)) {
			return FALSE;
		}
	}
	else if (mach64Bit) {
		// 64-bit native or 64-bit managed process on any platform.
		if (!UpdateImports64(hProcess, hModule, plpDlls, nDlls)) {
			return FALSE;
		}
	}
	else {
		// Who knows!?
		SetLastError(ERROR_INVALID_HANDLE);
		return FALSE;
	}
#endif // XHOOKS_64BIT

	/////////////////////////////////////////////////// Update the CLR header.
	//
	if (der.pclr != NULL) {
		XHOOK_CLR_HEADER clr;
		CopyMemory(&clr, &der.clr, sizeof(clr));
		clr.Flags &= 0xfffffffe;    // Clear the IL_ONLY flag.

		DWORD dwProtect;
		if (!VirtualProtectEx(hProcess, der.pclr, sizeof(clr), PAGE_READWRITE, &dwProtect)) {
			XHOOK_TRACE(("VirtualProtectEx(clr) write failed: %d\n", GetLastError()));
			return FALSE;
		}

		if (!WriteProcessMemory(hProcess, der.pclr, &clr, sizeof(clr), NULL)) {
			XHOOK_TRACE(("WriteProcessMemory(clr) failed: %d\n", GetLastError()));
			return FALSE;
		}

		if (!VirtualProtectEx(hProcess, der.pclr, sizeof(clr), dwProtect, &dwProtect)) {
			XHOOK_TRACE(("VirtualProtectEx(clr) restore failed: %d\n", GetLastError()));
			return FALSE;
		}
		XHOOK_TRACE(("CLR: %p..%p\n", der.pclr, der.pclr + der.cbclr));

#if XHOOKS_64BIT
		if (der.clr.Flags & 0x2) { // Is the 32BIT Required Flag set?
			// X64 never gets here because the process appears as a WOW64 process.
			// However, on IA64, it doesn't appear to be a WOW process.
			XHOOK_TRACE(("CLR Requires 32-bit\n", der.pclr, der.pclr + der.cbclr));
			SetLastError(ERROR_INVALID_HANDLE);
			return FALSE;
		}
#endif // XHOOKS_64BIT
	}

	//////////////////////////////// Save the undo data to the target process.
	//
	if (!XHookCopyPayloadToProcess(hProcess, XHOOK_EXE_RESTORE_GUID, &der, sizeof(der))) {
		XHOOK_TRACE(("XHookCopyPayloadToProcess failed: %d\n", GetLastError()));
		return FALSE;
	}
	return TRUE;
}

//////////////////////////////////////////////////////////////////////////////
//
BOOL WINAPI XHookCreateProcessWithDllA(LPCSTR lpApplicationName,
	__in_z LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEA pfCreateProcessA)
{
	DWORD dwMyCreationFlags = (dwCreationFlags | CREATE_SUSPENDED);
	PROCESS_INFORMATION pi;

	if (pfCreateProcessA == NULL) {
		pfCreateProcessA = CreateProcessA;
	}

	if (!pfCreateProcessA(lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		dwMyCreationFlags,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi)) {
		return FALSE;
	}

	LPCSTR rlpDlls[2];
	DWORD nDlls = 0;
	if (lpDllName != NULL) {
		rlpDlls[nDlls++] = lpDllName;
	}

	if (!XHookUpdateProcessWithDll(pi.hProcess, rlpDlls, nDlls)) {
		TerminateProcess(pi.hProcess, ~0u);
		return FALSE;
	}

	if (lpProcessInformation) {
		CopyMemory(lpProcessInformation, &pi, sizeof(pi));
	}

	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	return TRUE;
}


BOOL WINAPI XHookCreateProcessWithDllW(LPCWSTR lpApplicationName,
	__in_z LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEW pfCreateProcessW)
{
	DWORD dwMyCreationFlags = (dwCreationFlags | CREATE_SUSPENDED);
	PROCESS_INFORMATION pi;

	if (pfCreateProcessW == NULL) {
		pfCreateProcessW = CreateProcessW;
	}

	if (!pfCreateProcessW(lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		dwMyCreationFlags,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		&pi)) {
		return FALSE;
	}

	LPCSTR rlpDlls[2];
	DWORD nDlls = 0;
	if (lpDllName != NULL) {
		rlpDlls[nDlls++] = lpDllName;
	}

	if (!XHookUpdateProcessWithDll(pi.hProcess, rlpDlls, nDlls)) {
		TerminateProcess(pi.hProcess, ~0u);
		return FALSE;
	}

	if (lpProcessInformation) {
		CopyMemory(lpProcessInformation, &pi, sizeof(pi));
	}

	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(pi.hThread);
	}
	return TRUE;
}

BOOL WINAPI XHookCopyPayloadToProcess(HANDLE hProcess,
	REFGUID rguid,
	PVOID pData,
	DWORD cbData)
{
	DWORD cbTotal = (sizeof(IMAGE_DOS_HEADER) +
		sizeof(IMAGE_NT_HEADERS) +
		sizeof(IMAGE_SECTION_HEADER) +
		sizeof(XHOOK_SECTION_HEADER) +
		sizeof(XHOOK_SECTION_RECORD) +
		cbData);

	PBYTE pbBase = (PBYTE)VirtualAllocEx(hProcess, NULL, cbTotal,
		MEM_COMMIT, PAGE_READWRITE);
	if (pbBase == NULL) {
		XHOOK_TRACE(("VirtualAllocEx(%d) failed: %d\n", cbTotal, GetLastError()));
		return FALSE;
	}

	PBYTE pbTarget = pbBase;
	IMAGE_DOS_HEADER idh;
	IMAGE_NT_HEADERS inh;
	IMAGE_SECTION_HEADER ish;
	XHOOK_SECTION_HEADER dsh;
	XHOOK_SECTION_RECORD dsr;
	SIZE_T cbWrote = 0;

	ZeroMemory(&idh, sizeof(idh));
	idh.e_magic = IMAGE_DOS_SIGNATURE;
	idh.e_lfanew = sizeof(idh);
	if (!WriteProcessMemory(hProcess, pbTarget, &idh, sizeof(idh), &cbWrote) ||
		cbWrote != sizeof(idh)) {
		XHOOK_TRACE(("WriteProcessMemory(idh) failed: %d\n", GetLastError()));
		return FALSE;
	}
	pbTarget += sizeof(idh);

	ZeroMemory(&inh, sizeof(inh));
	inh.Signature = IMAGE_NT_SIGNATURE;
	inh.FileHeader.SizeOfOptionalHeader = sizeof(inh.OptionalHeader);
	inh.FileHeader.Characteristics = IMAGE_FILE_DLL;
	inh.FileHeader.NumberOfSections = 1;
	inh.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR_MAGIC;
	if (!WriteProcessMemory(hProcess, pbTarget, &inh, sizeof(inh), &cbWrote) ||
		cbWrote != sizeof(inh)) {
		return FALSE;
	}
	pbTarget += sizeof(inh);

	ZeroMemory(&ish, sizeof(ish));
	memcpy(ish.Name, ".xhook", sizeof(ish.Name));
	ish.VirtualAddress = (DWORD)((pbTarget + sizeof(ish)) - pbBase);
	ish.SizeOfRawData = (sizeof(XHOOK_SECTION_HEADER) +
		sizeof(XHOOK_SECTION_RECORD) +
		cbData);
	if (!WriteProcessMemory(hProcess, pbTarget, &ish, sizeof(ish), &cbWrote) ||
		cbWrote != sizeof(ish)) {
		return FALSE;
	}
	pbTarget += sizeof(ish);

	ZeroMemory(&dsh, sizeof(dsh));
	dsh.cbHeaderSize = sizeof(dsh);
	dsh.nSignature = XHOOK_SECTION_HEADER_SIGNATURE;
	dsh.nDataOffset = sizeof(XHOOK_SECTION_HEADER);
	dsh.cbDataSize = (sizeof(XHOOK_SECTION_HEADER) +
		sizeof(XHOOK_SECTION_RECORD) +
		cbData);
	if (!WriteProcessMemory(hProcess, pbTarget, &dsh, sizeof(dsh), &cbWrote) ||
		cbWrote != sizeof(dsh)) {
		return FALSE;
	}
	pbTarget += sizeof(dsh);

	ZeroMemory(&dsr, sizeof(dsr));
	dsr.cbBytes = cbData + sizeof(XHOOK_SECTION_RECORD);
	dsr.nReserved = 0;
	dsr.guid = rguid;
	if (!WriteProcessMemory(hProcess, pbTarget, &dsr, sizeof(dsr), &cbWrote) ||
		cbWrote != sizeof(dsr)) {
		return FALSE;
	}
	pbTarget += sizeof(dsr);

	if (!WriteProcessMemory(hProcess, pbTarget, pData, cbData, &cbWrote) ||
		cbWrote != cbData) {
		return FALSE;
	}
	pbTarget += cbData;

	XHOOK_TRACE(("Copied %d byte payload into target process at %p\n",
		cbTotal, pbTarget - cbTotal));
	return TRUE;
}

static BOOL fSearchedForHelper = FALSE;
static PXHOOK_EXE_HELPER pHelper = NULL;

VOID CALLBACK XHookFinishHelperProcess(HWND, HINSTANCE, LPSTR, INT)
{
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pHelper->pid);
	if (hProcess == NULL) {
		XHOOK_TRACE(("OpenProcess(pid=%d) failed: %d\n",
			pHelper->pid, GetLastError()));
		ExitProcess(9901);
	}

	PCSTR pszModule = pHelper->DllName;
	if (!XHookUpdateProcessWithDll(hProcess, &pszModule, 1)) {
		XHOOK_TRACE(("XHookUpdateProcessWithDll(pid=%d) failed: %d\n",
			pHelper->pid, GetLastError()));
		ExitProcess(9902);
	}
}

BOOL WINAPI XHookIsHelperProcess(VOID)
{
	PVOID pvData;
	DWORD cbData;

	if (fSearchedForHelper) {
		return (pHelper != NULL);
	}

	fSearchedForHelper = TRUE;
	pvData = XHookFindPayloadEx(XHOOK_EXE_HELPER_GUID, &cbData);

	if (pvData == NULL || cbData < sizeof(XHOOK_EXE_HELPER)) {
		return FALSE;
	}

	pHelper = (PXHOOK_EXE_HELPER)pvData;
	if (pHelper->cb < sizeof(*pHelper)) {
		pHelper = NULL;
		return FALSE;
	}

	return TRUE;
}

BOOL WINAPI XHookProcessViaHelperA(DWORD dwTargetPid,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEA pfCreateProcessA)
{
	PROCESS_INFORMATION pi;
	STARTUPINFOA si;
	CHAR szExe[MAX_PATH];
	CHAR szCommand[MAX_PATH];
	XHOOK_EXE_HELPER helper;

	ZeroMemory(&helper, sizeof(helper));
	helper.cb = sizeof(helper);
	helper.pid = dwTargetPid;
	strcpy_s(helper.DllName, ARRAYSIZE(helper.DllName), lpDllName);

	DWORD nLen = GetEnvironmentVariableA("WINDIR", szExe, ARRAYSIZE(szExe));
	if (nLen == 0 || nLen >= ARRAYSIZE(szExe)) {
		return FALSE;
	}

#if XHOOKS_OPTION_BITS
#if XHOOKS_32BIT
	strcat_s(szExe, ARRAYSIZE(szExe), "\\sysnative\\rundll32.exe");
#else // !XHOOKS_32BIT
	strcat_s(szExe, ARRAYSIZE(szExe), "\\syswow64\\rundll32.exe");
#endif // !XHOOKS_32BIT

	PCHAR pszDll;
	if ((pszDll = strrchr(helper.DllName, '\\')) != NULL) {
		pszDll++;
	}
	else if ((pszDll = strrchr(helper.DllName, ':')) != NULL) {
		pszDll++;
	}
	else {
		pszDll = helper.DllName;
	}

	// Replace "32." with "64." or "64." with "32."
	for (; *pszDll; pszDll++) {
#if XHOOKS_32BIT
		if (pszDll[0] == '3' && pszDll[1] == '2' && pszDll[2] == '.') {
			pszDll[0] = '6'; pszDll[1] = '4';
			break;
		}
#else
		if (pszDll[0] == '6' && pszDll[1] == '4' && pszDll[2] == '.') {
			pszDll[0] = '3'; pszDll[1] = '2';
			break;
		}
#endif
	}

#else // XHOOKS_OPTIONS_BITS
	strcat_s(szExe, ARRAYSIZE(szExe), "\\system32\\rundll32.exe");
#endif // XHOOKS_OPTIONS_BITS

	sprintf_s(szCommand, ARRAYSIZE(szCommand),
		"rundll32.exe \"%hs\",#1", helper.DllName);

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	if (pfCreateProcessA(szExe, szCommand, NULL, NULL, FALSE, CREATE_SUSPENDED,
		NULL, NULL, &si, &pi)) {

		if (!XHookCopyPayloadToProcess(pi.hProcess,
			XHOOK_EXE_HELPER_GUID,
			&helper, sizeof(helper))) {
			XHOOK_TRACE(("XHookCopyPayloadToProcess failed: %d\n", GetLastError()));
			TerminateProcess(pi.hProcess, ~0u);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			return FALSE;
		}

		ResumeThread(pi.hThread);
		WaitForSingleObject(pi.hProcess, INFINITE);

		DWORD dwResult = 500;
		GetExitCodeProcess(pi.hProcess, &dwResult);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (dwResult != 0) {
			XHOOK_TRACE(("Rundll32.exe failed: result=%d\n", dwResult));
			return FALSE;
		}
		return TRUE;
	}
	else {
		XHOOK_TRACE(("CreateProcess failed: %d\n", GetLastError()));
		return FALSE;
	}
}

BOOL WINAPI XHookProcessViaHelperW(DWORD dwTargetPid,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEW pfCreateProcessW)
{
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	WCHAR szExe[MAX_PATH];
	WCHAR szCommand[MAX_PATH];
	XHOOK_EXE_HELPER helper;

	ZeroMemory(&helper, sizeof(helper));
	helper.cb = sizeof(helper);
	helper.pid = dwTargetPid;
	strcpy_s(helper.DllName, ARRAYSIZE(helper.DllName), lpDllName);

	DWORD nLen = GetEnvironmentVariableW(L"WINDIR", szExe, ARRAYSIZE(szExe));
	if (nLen == 0 || nLen >= ARRAYSIZE(szExe)) {
		return FALSE;
	}

#if XHOOKS_OPTION_BITS
#if XHOOKS_32BIT
	wcscat_s(szExe, ARRAYSIZE(szExe), L"\\sysnative\\rundll32.exe");
#else // !XHOOKS_32BIT
	wcscat_s(szExe, ARRAYSIZE(szExe), L"\\syswow64\\rundll32.exe");
#endif // !XHOOKS_32BIT

	PCHAR pszDll;
	if ((pszDll = strrchr(helper.DllName, '\\')) != NULL) {
		pszDll++;
	}
	else if ((pszDll = strrchr(helper.DllName, ':')) != NULL) {
		pszDll++;
	}
	else {
		pszDll = helper.DllName;
	}

	// Replace "32." with "64." or "64." with "32."
	for (; *pszDll; pszDll++) {
#if XHOOKS_32BIT
		if (pszDll[0] == '3' && pszDll[1] == '2' && pszDll[2] == '.') {
			pszDll[0] = '6'; pszDll[1] = '4';
			break;
		}
#else
		if (pszDll[0] == '6' && pszDll[1] == '4' && pszDll[2] == '.') {
			pszDll[0] = '3'; pszDll[1] = '2';
			break;
		}
#endif
	}

#else // XHOOKS_OPTIONS_BITS
	wcscat_s(szExe, ARRAYSIZE(szExe), L"\\system32\\rundll32.exe");
#endif // XHOOKS_OPTIONS_BITS

	swprintf_s(szCommand, ARRAYSIZE(szCommand),
		L"rundll32.exe \"%hs\",#1", helper.DllName);

	ZeroMemory(&pi, sizeof(pi));
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);

	if (pfCreateProcessW(szExe, szCommand, NULL, NULL, FALSE, CREATE_SUSPENDED,
		NULL, NULL, &si, &pi)) {

		if (!XHookCopyPayloadToProcess(pi.hProcess,
			XHOOK_EXE_HELPER_GUID,
			&helper, sizeof(helper))) {
			XHOOK_TRACE(("XHookCopyPayloadToProcess failed: %d\n", GetLastError()));
			TerminateProcess(pi.hProcess, ~0u);
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);
			return FALSE;
		}

		ResumeThread(pi.hThread);

		ResumeThread(pi.hThread);
		WaitForSingleObject(pi.hProcess, INFINITE);

		DWORD dwResult = 500;
		GetExitCodeProcess(pi.hProcess, &dwResult);

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);

		if (dwResult != 0) {
			XHOOK_TRACE(("Rundll32.exe failed: result=%d\n", dwResult));
			return FALSE;
		}
		return TRUE;
	}
	else {
		XHOOK_TRACE(("CreateProcess failed: %d\n", GetLastError()));
		return FALSE;
	}
}

BOOL WINAPI XHookCreateProcessWithDllExA(LPCSTR lpApplicationName,
	__in_z LPSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCSTR lpCurrentDirectory,
	LPSTARTUPINFOA lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEA
	pfCreateProcessA)
{
	if (pfCreateProcessA == NULL) {
		pfCreateProcessA = CreateProcessA;
	}

	PROCESS_INFORMATION backup;
	if (lpProcessInformation == NULL) {
		lpProcessInformation = &backup;
		ZeroMemory(&backup, sizeof(backup));
	}

	if (!pfCreateProcessA(lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		dwCreationFlags | CREATE_SUSPENDED,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		lpProcessInformation)) {
		return FALSE;
	}

	LPCSTR szDll = lpDllName;

	if (!XHookUpdateProcessWithDll(lpProcessInformation->hProcess, &szDll, 1) &&
		!XHookProcessViaHelperA(lpProcessInformation->dwProcessId,
		lpDllName,
		pfCreateProcessA)) {

		TerminateProcess(lpProcessInformation->hProcess, ~0u);
		CloseHandle(lpProcessInformation->hProcess);
		CloseHandle(lpProcessInformation->hThread);
		return FALSE;
	}

	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(lpProcessInformation->hThread);
	}

	if (lpProcessInformation == &backup) {
		CloseHandle(lpProcessInformation->hProcess);
		CloseHandle(lpProcessInformation->hThread);
	}

	return TRUE;
}

BOOL WINAPI XHookCreateProcessWithDllExW(LPCWSTR lpApplicationName,
	__in_z LPWSTR lpCommandLine,
	LPSECURITY_ATTRIBUTES lpProcessAttributes,
	LPSECURITY_ATTRIBUTES lpThreadAttributes,
	BOOL bInheritHandles,
	DWORD dwCreationFlags,
	LPVOID lpEnvironment,
	LPCWSTR lpCurrentDirectory,
	LPSTARTUPINFOW lpStartupInfo,
	LPPROCESS_INFORMATION lpProcessInformation,
	LPCSTR lpDllName,
	PXHOOK_CREATE_PROCESS_ROUTINEW
	pfCreateProcessW)
{
	if (pfCreateProcessW == NULL) {
		pfCreateProcessW = CreateProcessW;
	}

	PROCESS_INFORMATION backup;
	if (lpProcessInformation == NULL) {
		lpProcessInformation = &backup;
		ZeroMemory(&backup, sizeof(backup));
	}

	if (!pfCreateProcessW(lpApplicationName,
		lpCommandLine,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		dwCreationFlags | CREATE_SUSPENDED,
		lpEnvironment,
		lpCurrentDirectory,
		lpStartupInfo,
		lpProcessInformation)) {
		return FALSE;
	}


	LPCSTR sz = lpDllName;

	if (!XHookUpdateProcessWithDll(lpProcessInformation->hProcess, &sz, 1) &&
		!XHookProcessViaHelperW(lpProcessInformation->dwProcessId,
		lpDllName,
		pfCreateProcessW)) {

		TerminateProcess(lpProcessInformation->hProcess, ~0u);
		CloseHandle(lpProcessInformation->hProcess);
		CloseHandle(lpProcessInformation->hThread);
		return FALSE;
	}

	if (!(dwCreationFlags & CREATE_SUSPENDED)) {
		ResumeThread(lpProcessInformation->hThread);
	}

	if (lpProcessInformation == &backup) {
		CloseHandle(lpProcessInformation->hProcess);
		CloseHandle(lpProcessInformation->hThread);
	}
	return TRUE;
}

//
///////////////////////////////////////////////////////////////// End of creatwth.

//////////////////////////////////////////////////////////////////////////////
//
struct _XHOOK_ALIGN
{
	BYTE    obTarget : 3;
	BYTE    obTrampoline : 5;
};

C_ASSERT(sizeof(_XHOOK_ALIGN) == 1);

//////////////////////////////////////////////////////////////////////////////
//
static bool xhook_is_imported(PBYTE pbCode, PBYTE pbAddress)
{
	MEMORY_BASIC_INFORMATION mbi;
	VirtualQuery((PVOID)pbCode, &mbi, sizeof(mbi));
	__try {
		PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)mbi.AllocationBase;
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			return false;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			return false;
		}

		if (pbAddress >= ((PBYTE)pDosHeader +
			pNtHeader->OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress) &&
			pbAddress < ((PBYTE)pDosHeader +
			pNtHeader->OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress +
			pNtHeader->OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size)) {
			return true;
		}
		return false;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return false;
	}
}

///////////////////////////////////////////////////////////////////////// X86.
//
#ifdef XHOOKS_X86

struct _XHOOK_TRAMPOLINE
{
	BYTE            rbCode[30];     // target code + jmp to pbRemain
	BYTE            cbCode;         // size of moved target code.
	BYTE            cbCodeBreak;    // padding to make debugging easier.
	BYTE            rbRestore[22];  // original target code.
	BYTE            cbRestore;      // size of original target code.
	BYTE            cbRestoreBreak; // padding to make debugging easier.
	_XHOOK_ALIGN   rAlign[8];      // instruction alignment array.
	PBYTE           pbRemain;       // first instruction after moved code. [free list]
	PBYTE           pbXHook;       // first instruction of xhook function.
};

C_ASSERT(sizeof(_XHOOK_TRAMPOLINE) == 72);

enum {
	SIZE_OF_JMP = 5
};

inline PBYTE xhook_gen_jmp_immediate(PBYTE pbCode, PBYTE pbJmpVal)
{
	PBYTE pbJmpSrc = pbCode + 5;
	*pbCode++ = 0xE9;   // jmp +imm32
	*((INT32*&)pbCode)++ = (INT32)(pbJmpVal - pbJmpSrc);
	return pbCode;
}

inline PBYTE xhook_gen_jmp_indirect(PBYTE pbCode, PBYTE *ppbJmpVal)
{
	PBYTE pbJmpSrc = pbCode + 6;
	*pbCode++ = 0xff;   // jmp [+imm32]
	*pbCode++ = 0x25;
	*((INT32*&)pbCode)++ = (INT32)((PBYTE)ppbJmpVal - pbJmpSrc);
	return pbCode;
}

inline PBYTE xhook_gen_brk(PBYTE pbCode, PBYTE pbLimit)
{
	while (pbCode < pbLimit) {
		*pbCode++ = 0xcc;   // brk;
	}
	return pbCode;
}

inline PBYTE xhook_skip_jmp(PBYTE pbCode, PVOID *ppGlobals)
{
	if (pbCode == NULL) {
		return NULL;
	}
	if (ppGlobals != NULL) {
		*ppGlobals = NULL;
	}

	// First, skip over the import vector if there is one.
	if (pbCode[0] == 0xff && pbCode[1] == 0x25) {   // jmp [imm32]
		// Looks like an import alias jump, then get the code it points to.
		PBYTE pbTarget = *(PBYTE *)&pbCode[2];
		if (xhook_is_imported(pbCode, pbTarget)) {
			PBYTE pbNew = *(PBYTE *)pbTarget;
			XHOOK_TRACE(("%p->%p: skipped over import table.\n", pbCode, pbNew));
			pbCode = pbNew;
		}
	}

	// Then, skip over a patch jump
	if (pbCode[0] == 0xeb) {   // jmp +imm8
		PBYTE pbNew = pbCode + 2 + *(CHAR *)&pbCode[1];
		XHOOK_TRACE(("%p->%p: skipped over short jump.\n", pbCode, pbNew));
		pbCode = pbNew;

		// First, skip over the import vector if there is one.
		if (pbCode[0] == 0xff && pbCode[1] == 0x25) {   // jmp [imm32]
			// Looks like an import alias jump, then get the code it points to.
			PBYTE pbTarget = *(PBYTE *)&pbCode[2];
			if (xhook_is_imported(pbCode, pbTarget)) {
				PBYTE pbNew = *(PBYTE *)pbTarget;
				XHOOK_TRACE(("%p->%p: skipped over import table.\n", pbCode, pbNew));
				pbCode = pbNew;
			}
		}
		// Finally, skip over a long jump if it is the target of the patch jump.
		else if (pbCode[0] == 0xe9) {   // jmp +imm32
			PBYTE pbNew = pbCode + 5 + *(INT32 *)&pbCode[1];
			XHOOK_TRACE(("%p->%p: skipped over long jump.\n", pbCode, pbNew));
			pbCode = pbNew;
		}
	}
	return pbCode;
}

inline BOOL xhook_does_code_end_function(PBYTE pbCode)
{
	if (pbCode[0] == 0xeb ||    // jmp +imm8
		pbCode[0] == 0xe9 ||    // jmp +imm32
		pbCode[0] == 0xe0 ||    // jmp eax
		pbCode[0] == 0xc2 ||    // ret +imm8
		pbCode[0] == 0xc3 ||    // ret
		pbCode[0] == 0xcc) {    // brk
		return TRUE;
	}
	else if (pbCode[0] == 0xff && pbCode[1] == 0x25) {  // jmp [+imm32]
		return TRUE;
	}
	else if ((pbCode[0] == 0x26 ||      // jmp es:
		pbCode[0] == 0x2e ||      // jmp cs:
		pbCode[0] == 0x36 ||      // jmp ss:
		pbCode[0] == 0xe3 ||      // jmp ds:
		pbCode[0] == 0x64 ||      // jmp fs:
		pbCode[0] == 0x65) &&     // jmp gs:
		pbCode[1] == 0xff &&       // jmp [+imm32]
		pbCode[2] == 0x25) {
		return TRUE;
	}
	return FALSE;
}

inline ULONG xhook_is_code_filler(PBYTE pbCode)
{
	if (pbCode[0] == 0x90) { // nop
		return 1;
	}
	return 0;
}

#endif // XHOOKS_X86

///////////////////////////////////////////////////////////////////////// X64.
//
#ifdef XHOOKS_X64

struct _XHOOK_TRAMPOLINE
{
	// An X64 instuction can be 15 bytes long.
	// In practice 11 seems to be the limit.
	BYTE            rbCode[30];     // target code + jmp to pbRemain.
	BYTE            cbCode;         // size of moved target code.
	BYTE            cbCodeBreak;    // padding to make debugging easier.
	BYTE            rbRestore[30];  // original target code.
	BYTE            cbRestore;      // size of original target code.
	BYTE            cbRestoreBreak; // padding to make debugging easier.
	_XHOOK_ALIGN   rAlign[8];      // instruction alignment array.
	PBYTE           pbRemain;       // first instruction after moved code. [free list]
	PBYTE           pbXHook;       // first instruction of xhook function.
	BYTE            rbCodeIn[8];    // jmp [pbXHook]
};

C_ASSERT(sizeof(_XHOOK_TRAMPOLINE) == 96);

enum {
	SIZE_OF_JMP = 5
};

inline PBYTE xhook_gen_jmp_immediate(PBYTE pbCode, PBYTE pbJmpVal)
{
	PBYTE pbJmpSrc = pbCode + 5;
	*pbCode++ = 0xE9;   // jmp +imm32
	*((INT32*&)pbCode)++ = (INT32)(pbJmpVal - pbJmpSrc);
	return pbCode;
}

inline PBYTE xhook_gen_jmp_indirect(PBYTE pbCode, PBYTE *ppbJmpVal)
{
	PBYTE pbJmpSrc = pbCode + 6;
	*pbCode++ = 0xff;   // jmp [+imm32]
	*pbCode++ = 0x25;
	*((INT32*&)pbCode)++ = (INT32)((PBYTE)ppbJmpVal - pbJmpSrc);
	return pbCode;
}

inline PBYTE xhook_gen_brk(PBYTE pbCode, PBYTE pbLimit)
{
	while (pbCode < pbLimit) {
		*pbCode++ = 0xcc;   // brk;
	}
	return pbCode;
}

inline PBYTE xhook_skip_jmp(PBYTE pbCode, PVOID *ppGlobals)
{
	if (pbCode == NULL) {
		return NULL;
	}
	if (ppGlobals != NULL) {
		*ppGlobals = NULL;
	}

	// First, skip over the import vector if there is one.
	if (pbCode[0] == 0xff && pbCode[1] == 0x25) {   // jmp [+imm32]
		// Looks like an import alias jump, then get the code it points to.
		PBYTE pbTarget = pbCode + 6 + *(INT32 *)&pbCode[2];
		if (xhook_is_imported(pbCode, pbTarget)) {
			PBYTE pbNew = *(PBYTE *)pbTarget;
			XHOOK_TRACE(("%p->%p: skipped over import table.\n", pbCode, pbNew));
			pbCode = pbNew;
		}
	}

	// Then, skip over a patch jump
	if (pbCode[0] == 0xeb) {   // jmp +imm8
		PBYTE pbNew = pbCode + 2 + *(CHAR *)&pbCode[1];
		XHOOK_TRACE(("%p->%p: skipped over short jump.\n", pbCode, pbNew));
		pbCode = pbNew;

		// First, skip over the import vector if there is one.
		if (pbCode[0] == 0xff && pbCode[1] == 0x25) {   // jmp [imm32]
			// Looks like an import alias jump, then get the code it points to.
			PBYTE pbTarget = pbCode + 6 + *(INT32 *)&pbCode[2];
			if (xhook_is_imported(pbCode, pbTarget)) {
				PBYTE pbNew = *(PBYTE *)pbTarget;
				XHOOK_TRACE(("%p->%p: skipped over import table.\n", pbCode, pbNew));
				pbCode = pbNew;
			}
		}
		// Finally, skip over a long jump if it is the target of the patch jump.
		else if (pbCode[0] == 0xe9) {   // jmp +imm32
			PBYTE pbNew = pbCode + 5 + *(INT32 *)&pbCode[1];
			XHOOK_TRACE(("%p->%p: skipped over long jump.\n", pbCode, pbNew));
			pbCode = pbNew;
		}
	}
	return pbCode;
}

inline BOOL xhook_does_code_end_function(PBYTE pbCode)
{
	if (pbCode[0] == 0xeb ||    // jmp +imm8
		pbCode[0] == 0xe9 ||    // jmp +imm32
		pbCode[0] == 0xe0 ||    // jmp eax
		pbCode[0] == 0xc2 ||    // ret +imm8
		pbCode[0] == 0xc3 ||    // ret
		pbCode[0] == 0xcc) {    // brk
		return TRUE;
	}
	else if (pbCode[0] == 0xff && pbCode[1] == 0x25) {  // jmp [+imm32]
		return TRUE;
	}
	else if ((pbCode[0] == 0x26 ||      // jmp es:
		pbCode[0] == 0x2e ||      // jmp cs:
		pbCode[0] == 0x36 ||      // jmp ss:
		pbCode[0] == 0xe3 ||      // jmp ds:
		pbCode[0] == 0x64 ||      // jmp fs:
		pbCode[0] == 0x65) &&     // jmp gs:
		pbCode[1] == 0xff &&       // jmp [+imm32]
		pbCode[2] == 0x25) {
		return TRUE;
	}
	return FALSE;
}

inline ULONG xhook_is_code_filler(PBYTE pbCode)
{
	if (pbCode[0] == 0x90) { // nop
		return 1;
	}
	return 0;
}

#endif // XHOOKS_X64

//////////////////////////////////////////////////////////////////////// IA64.
//
#ifdef XHOOKS_IA64

struct _XHOOK_TRAMPOLINE
{
	// On the IA64, a trampoline is used for both incoming and outgoing calls.
	//
	// The trampoline contains the following bundles for the outgoing call:
	//      movl gp=target_gp;
	//      <relocated target bundle>
	//      brl  target_code;
	//
	// The trampoline contains the following bundles for the incoming call:
	//      alloc  r41=ar.pfs, b, 0, 8, 0
	//      mov    r40=rp
	//
	//      adds   r50=0, r39
	//      adds   r49=0, r38
	//      adds   r48=0, r37 ;;
	//
	//      adds   r47=0, r36
	//      adds   r46=0, r35
	//      adds   r45=0, r34
	//
	//      adds   r44=0, r33
	//      adds   r43=0, r32
	//      adds   r42=0, gp ;;
	//
	//      movl   gp=ffffffff`ffffffff ;;
	//
	//      brl.call.sptk.few rp=disas!TestCodes+20e0 (00000000`00404ea0) ;;
	//
	//      adds   gp=0, r42
	//      mov    rp=r40, +0 ;;
	//      mov.i  ar.pfs=r41
	//
	//      br.ret.sptk.many rp ;;
	//
	// This way, we only have to relocate a single bundle.
	//
	// The complicated incoming trampoline is required because we have to
	// create an additional stack frame so that we save and restore the gp.
	// We must do this because gp is a caller-saved register, but not saved
	// if the caller thinks the target is in the same DLL, which changes
	// when we insert a xhook.
	//
	XHOOK_IA64_BUNDLE  bMovlTargetGp;  // Bundle which sets target GP
	BYTE                rbCode[sizeof(XHOOK_IA64_BUNDLE)]; // moved bundle.
	XHOOK_IA64_BUNDLE  bBrlRemainEip;  // Brl to pbRemain

	// Target of brl inserted in target function
	XHOOK_IA64_BUNDLE  bAllocFrame;    // alloc frame
	XHOOK_IA64_BUNDLE  bSave37to39;    // save r37, r38, r39.
	XHOOK_IA64_BUNDLE  bSave34to36;    // save r34, r35, r36.
	XHOOK_IA64_BUNDLE  bSaveGPto33;    // save gp, r32, r33.
	XHOOK_IA64_BUNDLE  bMovlXHookGp;  // set xhook GP.
	XHOOK_IA64_BUNDLE  bCallXHook;    // call xhook.
	XHOOK_IA64_BUNDLE  bPopFrameGp;    // pop frame and restore gp.
	XHOOK_IA64_BUNDLE  bReturn;        // return to caller.

	PLABEL_DESCRIPTOR   pldTrampoline;

	BYTE                rbRestore[sizeof(XHOOK_IA64_BUNDLE)]; // original target bundle.
	BYTE                cbRestore;      // size of original target code.
	BYTE                cbCode;         // size of moved target code.
	_XHOOK_ALIGN       rAlign[14];     // instruction alignment array.
	PBYTE               pbRemain;       // first instruction after moved code. [free list]
	PBYTE               pbXHook;       // first instruction of xhook function.
	PPLABEL_DESCRIPTOR  ppldXHook;     // [pbXHook,gpXHook]
	PPLABEL_DESCRIPTOR  ppldTarget;     // [pbTarget,gpXHook]
};

C_ASSERT(sizeof(XHOOK_IA64_BUNDLE) == 16);
C_ASSERT(sizeof(_XHOOK_TRAMPOLINE) == 256);

enum {
	SIZE_OF_JMP = sizeof(XHOOK_IA64_BUNDLE)
};

inline PBYTE xhook_skip_jmp(PBYTE pPointer, PVOID *ppGlobals)
{
	PBYTE pGlobals = NULL;
	PBYTE pbCode = NULL;

	if (pPointer != NULL) {
		PPLABEL_DESCRIPTOR ppld = (PPLABEL_DESCRIPTOR)pPointer;
		pbCode = (PBYTE)ppld->EntryPoint;
		pGlobals = (PBYTE)ppld->GlobalPointer;
	}
	if (ppGlobals != NULL) {
		*ppGlobals = pGlobals;
	}
	if (pbCode == NULL) {
		return NULL;
	}

	XHOOK_IA64_BUNDLE *pb = (XHOOK_IA64_BUNDLE *)pbCode;

	// IA64 Local Import Jumps look like:
	//      addl   r2=ffffffff`ffe021c0, gp ;;
	//      ld8    r2=[r2]
	//      nop.i  0 ;;
	//
	//      ld8    r3=[r2], 8 ;;
	//      ld8    gp=[r2]
	//      mov    b6=r3, +0
	//
	//      nop.m  0
	//      nop.i  0
	//      br.cond.sptk.few b6
	//

	//                     002024000200100b
	if ((pb[0].wide[0] & 0xfffffc000603ffff) == 0x002024000200100b &&
		pb[0].wide[1] == 0x0004000000203008 &&
		pb[1].wide[0] == 0x001014180420180a &&
		pb[1].wide[1] == 0x07000830c0203008 &&
		pb[2].wide[0] == 0x0000000100000010 &&
		pb[2].wide[1] == 0x0080006000000200) {

		ULONG64 offset =
			((pb[0].wide[0] & 0x0000000001fc0000) >> 18) |  // imm7b
			((pb[0].wide[0] & 0x000001ff00000000) >> 25) |  // imm9d
			((pb[0].wide[0] & 0x00000000f8000000) >> 11);   // imm5c
		if (pb[0].wide[0] & 0x0000020000000000) {           // sign
			offset |= 0xffffffffffe00000;
		}
		PBYTE pbTarget = pGlobals + offset;
		XHOOK_TRACE(("%p: potential import jump, target=%p\n", pb, pbTarget));

		if (xhook_is_imported(pbCode, pbTarget) && *(PBYTE*)pbTarget != NULL) {
			XHOOK_TRACE(("%p: is import jump, label=%p\n", pb, *(PBYTE *)pbTarget));

			PPLABEL_DESCRIPTOR ppld = (PPLABEL_DESCRIPTOR)*(PBYTE *)pbTarget;
			pbCode = (PBYTE)ppld->EntryPoint;
			pGlobals = (PBYTE)ppld->GlobalPointer;
			if (ppGlobals != NULL) {
				*ppGlobals = pGlobals;
			}
		}
	}
	return pbCode;
}

inline BOOL xhook_does_code_end_function(PBYTE pbCode)
{
	(void)pbCode;
	return FALSE;
}

inline ULONG xhook_is_code_filler(PBYTE pbCode)
{
	(void)pbCode;
	return 0;//FALSE;
}



#endif // XHOOKS_IA64

#ifdef XHOOKS_ARM
#define XHOOKS_PFUNC_TO_PBYTE(p)  ((PBYTE)(((ULONG_PTR)(p)) & ~(ULONG_PTR)1))
#define XHOOKS_PBYTE_TO_PFUNC(p)  ((PBYTE)(((ULONG_PTR)(p)) | (ULONG_PTR)1))

struct _XHOOK_TRAMPOLINE
{
	// A Thumb-2 instruction can be 2 or 4 bytes long.
	BYTE            rbCode[62];     // target code + jmp to pbRemain
	BYTE            cbCode;         // size of moved target code.
	BYTE            cbCodeBreak;    // padding to make debugging easier.
	BYTE            rbRestore[22];  // original target code.
	BYTE            cbRestore;      // size of original target code.
	BYTE            cbRestoreBreak; // padding to make debugging easier.
	_XHOOK_ALIGN   rAlign[8];      // instruction alignment array.
	PBYTE           pbRemain;       // first instruction after moved code. [free list]
	PBYTE           pbXHook;       // first instruction of xhook function.
};

C_ASSERT(sizeof(_XHOOK_TRAMPOLINE) == 104);

enum {
	SIZE_OF_JMP = 8
};

inline PBYTE align4(PBYTE pValue)
{
	return (PBYTE)(((ULONG)pValue) & ~(ULONG)3u);
}

inline ULONG fetch_thumb_opcode(PBYTE pbCode)
{
	ULONG Opcode = *(UINT16 *)&pbCode[0];
	if (Opcode >= 0xe800) {
		Opcode = (Opcode << 16) | *(UINT16 *)&pbCode[2];
	}
	return Opcode;
}

inline void write_thumb_opcode(PBYTE &pbCode, ULONG Opcode)
{
	if (Opcode >= 0x10000) {
		*((UINT16*&)pbCode)++ = Opcode >> 16;
	}
	*((UINT16*&)pbCode)++ = (UINT16)Opcode;
}

PBYTE xhook_gen_jmp_immediate(PBYTE pbCode, PBYTE *ppPool, PBYTE pbJmpVal)
{
	PBYTE pbLiteral;
	if (ppPool != NULL) {
		*ppPool = *ppPool - 4;
		pbLiteral = *ppPool;
	}
	else {
		pbLiteral = align4(pbCode + 6);
	}

	*((PBYTE*&)pbLiteral) = XHOOKS_PBYTE_TO_PFUNC(pbJmpVal);
	LONG delta = pbLiteral - align4(pbCode + 4);

	write_thumb_opcode(pbCode, 0xf8dff000 | delta);     // LDR PC,[PC+n]

	if (ppPool == NULL) {
		if (((ULONG)pbCode & 2) != 0) {
			write_thumb_opcode(pbCode, 0xdefe);         // BREAK
		}
		pbCode += 4;
	}
	return pbCode;
}

inline PBYTE xhook_gen_brk(PBYTE pbCode, PBYTE pbLimit)
{
	while (pbCode < pbLimit) {
		write_thumb_opcode(pbCode, 0xdefe);
	}
	return pbCode;
}

inline PBYTE xhook_skip_jmp(PBYTE pbCode, PVOID *ppGlobals)
{
	if (pbCode == NULL) {
		return NULL;
	}
	if (ppGlobals != NULL) {
		*ppGlobals = NULL;
	}
	// Skip over the import jump if there is one.
	pbCode = (PBYTE)XHOOKS_PFUNC_TO_PBYTE(pbCode);
	ULONG Opcode = fetch_thumb_opcode(pbCode);

	if ((Opcode & 0xfbf08f00) == 0xf2400c00) {          // movw r12,#xxxx
		ULONG Opcode2 = fetch_thumb_opcode(pbCode + 4);

		if ((Opcode2 & 0xfbf08f00) == 0xf2c00c00) {      // movt r12,#xxxx
			ULONG Opcode3 = fetch_thumb_opcode(pbCode + 8);
			if (Opcode3 == 0xf8dcf000) {                 // ldr  pc,[r12]
				PBYTE pbTarget = (PBYTE)(((Opcode2 << 12) & 0xf7000000) |
					((Opcode2 << 1) & 0x08000000) |
					((Opcode2 << 16) & 0x00ff0000) |
					((Opcode >> 4) & 0x0000f700) |
					((Opcode >> 15) & 0x00000800) |
					((Opcode >> 0) & 0x000000ff));
				if (xhook_is_imported(pbCode, pbTarget)) {
					PBYTE pbNew = *(PBYTE *)pbTarget;
					// [GalenH]                    __debugbreak();
					pbNew = XHOOKS_PFUNC_TO_PBYTE(pbNew);
					XHOOK_TRACE(("%p->%p: skipped over import table.\n", pbCode, pbNew));
					return pbNew;
				}
			}
		}
	}
	return pbCode;
}

inline BOOL xhook_does_code_end_function(PBYTE pbCode)
{
	ULONG Opcode = fetch_thumb_opcode(pbCode);
	if ((Opcode & 0xffffff87) == 0x4700 ||          // bx <reg>
		(Opcode & 0xf800d000) == 0xf0009000) {      // b <imm20>
		return TRUE;
	}
	if ((Opcode & 0xffff8000) == 0xe8bd8000) {      // pop {...,pc}
		__debugbreak();
		return TRUE;
	}
	if ((Opcode & 0xffffff00) == 0x0000bd00) {      // pop {...,pc}
		__debugbreak();
		return TRUE;
	}
	return FALSE;
}

inline ULONG xhook_is_code_filler(PBYTE pbCode)
{
	if (pbCode[0] == 0x00 && pbCode[1] == 0xbf) {
		return 1; //nop
	}
	if (pbCode[0] == 0x00 && pbCode[1] == 0x00) {
		return 1; //nop
	}
	return 0;
}
#endif // XHOOKS_ARM

//////////////////////////////////////////////// Trampoline Memory Management.
//
struct XHOOK_REGION
{
	ULONG               dwSignature;
	XHOOK_REGION *     pNext;  // Next region in list of regions.
	XHOOK_TRAMPOLINE * pFree;  // List of free trampolines in this region.
};
typedef XHOOK_REGION * PXHOOK_REGION;

const ULONG XHOOK_REGION_SIGNATURE = 'Rrtd';
const ULONG XHOOK_REGION_SIZE = 0x10000;
const ULONG XHOOK_TRAMPOLINES_PER_REGION = (XHOOK_REGION_SIZE
	/ sizeof(XHOOK_TRAMPOLINE)) - 1;
static PXHOOK_REGION s_pRegions = NULL;            // List of all regions.
static PXHOOK_REGION s_pRegion = NULL;             // Default region.

static void xhook_writable_trampoline_regions()
{
	// Mark all of the regions as writable.
	for (PXHOOK_REGION pRegion = s_pRegions; pRegion != NULL; pRegion = pRegion->pNext) {
		DWORD dwOld;
		VirtualProtect(pRegion, XHOOK_REGION_SIZE, PAGE_EXECUTE_READWRITE, &dwOld);
	}
}

static void xhook_runnable_trampoline_regions()
{
	HANDLE hProcess = GetCurrentProcess();

	// Mark all of the regions as executable.
	for (PXHOOK_REGION pRegion = s_pRegions; pRegion != NULL; pRegion = pRegion->pNext) {
		DWORD dwOld;
		VirtualProtect(pRegion, XHOOK_REGION_SIZE, PAGE_EXECUTE_READ, &dwOld);
		FlushInstructionCache(hProcess, pRegion, XHOOK_REGION_SIZE);
	}
}

static PBYTE xhook_alloc_round_down_to_region(PBYTE pbTry)
{
	// WinXP64 returns free areas that aren't REGION aligned to 32-bit applications.
	ULONG_PTR extra = ((ULONG_PTR)pbTry) & (XHOOK_REGION_SIZE - 1);
	if (extra != 0) {
		pbTry -= extra;
	}
	return pbTry;
}

static PBYTE xhook_alloc_round_up_to_region(PBYTE pbTry)
{
	// WinXP64 returns free areas that aren't REGION aligned to 32-bit applications.
	ULONG_PTR extra = ((ULONG_PTR)pbTry) & (XHOOK_REGION_SIZE - 1);
	if (extra != 0) {
		ULONG_PTR adjust = XHOOK_REGION_SIZE - extra;
		pbTry += adjust;
	}
	return pbTry;
}

// Starting at pbLo, try to allocate a memory region, continue until pbHi.

static PVOID xhook_alloc_region_from_lo(PBYTE pbLo, PBYTE pbHi)
{
	PBYTE pbTry = xhook_alloc_round_up_to_region(pbLo);

	XHOOK_TRACE((" Looking for free region in %p..%p from %p:\n", pbLo, pbHi, pbTry));

	for (; pbTry < pbHi;) {
		MEMORY_BASIC_INFORMATION mbi;

		if (pbTry >= (PBYTE)(ULONG_PTR)0x50000000 &&
			pbTry <= (PBYTE)(ULONG_PTR)0x80000000) {
			// Skip region reserved for system DLLs.
			pbTry = (PBYTE)(ULONG_PTR)(0x80000000 + XHOOK_REGION_SIZE);
			continue;
		}

		ZeroMemory(&mbi, sizeof(mbi));
		if (!VirtualQuery(pbTry, &mbi, sizeof(mbi))) {
			break;
		}

		XHOOK_TRACE(("  Try %p => %p..%p %6x\n",
			pbTry,
			mbi.BaseAddress,
			(PBYTE)mbi.BaseAddress + mbi.RegionSize - 1,
			mbi.State));

		if (mbi.State == MEM_FREE && mbi.RegionSize >= XHOOK_REGION_SIZE) {

			PVOID pv = VirtualAlloc(pbTry,
				XHOOK_REGION_SIZE,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_EXECUTE_READWRITE);
			if (pv != NULL) {
				return pv;
			}
			pbTry += XHOOK_REGION_SIZE;
		}
		else {
			pbTry = xhook_alloc_round_up_to_region((PBYTE)mbi.BaseAddress + mbi.RegionSize);
		}
	}
	return NULL;
}

// Starting at pbHi, try to allocate a memory region, continue until pbLo.

static PVOID xhook_alloc_region_from_hi(PBYTE pbLo, PBYTE pbHi)
{
	PBYTE pbTry = xhook_alloc_round_down_to_region(pbHi - XHOOK_REGION_SIZE);

	XHOOK_TRACE((" Looking for free region in %p..%p from %p:\n", pbLo, pbHi, pbTry));

	for (; pbTry > pbLo;) {
		MEMORY_BASIC_INFORMATION mbi;

		XHOOK_TRACE(("  Try %p\n", pbTry));
		if (pbTry >= (PBYTE)(ULONG_PTR)0x50000000 &&
			pbTry <= (PBYTE)(ULONG_PTR)0x80000000) {
			// Skip region reserved for system DLLs.
			pbTry = (PBYTE)(ULONG_PTR)(0x50000000 - XHOOK_REGION_SIZE);
			continue;
		}

		ZeroMemory(&mbi, sizeof(mbi));
		if (!VirtualQuery(pbTry, &mbi, sizeof(mbi))) {
			break;
		}

		XHOOK_TRACE(("  Try %p => %p..%p %6x\n",
			pbTry,
			mbi.BaseAddress,
			(PBYTE)mbi.BaseAddress + mbi.RegionSize - 1,
			mbi.State));

		if (mbi.State == MEM_FREE && mbi.RegionSize >= XHOOK_REGION_SIZE) {

			PVOID pv = VirtualAlloc(pbTry,
				XHOOK_REGION_SIZE,
				MEM_COMMIT | MEM_RESERVE,
				PAGE_EXECUTE_READWRITE);
			if (pv != NULL) {
				return pv;
			}
			pbTry -= XHOOK_REGION_SIZE;
		}
		else {
			pbTry = xhook_alloc_round_down_to_region((PBYTE)mbi.AllocationBase
				- XHOOK_REGION_SIZE);
		}
	}
	return NULL;
}

static PXHOOK_TRAMPOLINE xhook_alloc_trampoline(PBYTE pbTarget)
{
	// We have to place trampolines within +/- 2GB of target.

	PXHOOK_TRAMPOLINE pLo = (PXHOOK_TRAMPOLINE)
		((pbTarget > (PBYTE)0x7ff80000)
		? pbTarget - 0x7ff80000 : (PBYTE)(ULONG_PTR)XHOOK_REGION_SIZE);
	PXHOOK_TRAMPOLINE pHi = (PXHOOK_TRAMPOLINE)
		((pbTarget < (PBYTE)0xffffffff80000000)
		? pbTarget + 0x7ff80000 : (PBYTE)0xfffffffffff80000);
	XHOOK_TRACE(("[%p..%p..%p]\n", pLo, pbTarget, pHi));

	PXHOOK_TRAMPOLINE pTrampoline = NULL;

	// Insure that there is a default region.
	if (s_pRegion == NULL && s_pRegions != NULL) {
		s_pRegion = s_pRegions;
	}

	// First check the default region for an valid free block.
	if (s_pRegion != NULL && s_pRegion->pFree != NULL &&
		s_pRegion->pFree >= pLo && s_pRegion->pFree <= pHi) {

	found_region:
		pTrampoline = s_pRegion->pFree;
		// do a last sanity check on region.
		if (pTrampoline < pLo || pTrampoline > pHi) {
			return NULL;
		}
		s_pRegion->pFree = (PXHOOK_TRAMPOLINE)pTrampoline->pbRemain;
		memset(pTrampoline, 0xcc, sizeof(*pTrampoline));
		return pTrampoline;
	}

	// Then check the existing regions for a valid free block.
	for (s_pRegion = s_pRegions; s_pRegion != NULL; s_pRegion = s_pRegion->pNext) {
		if (s_pRegion != NULL && s_pRegion->pFree != NULL &&
			s_pRegion->pFree >= pLo && s_pRegion->pFree <= pHi) {
			goto found_region;
		}
	}

	// We need to allocate a new region.

	// Round pbTarget down to 64KB block.
	pbTarget = pbTarget - (PtrToUlong(pbTarget) & 0xffff);

	PVOID pbTry = NULL;

	// Try looking 1GB below or lower.
	if (pbTry == NULL && pbTarget > (PBYTE)0x40000000) {
		pbTry = xhook_alloc_region_from_hi((PBYTE)pLo, pbTarget - 0x40000000);
	}
	// Try looking 1GB above or higher.
	if (pbTry == NULL && pbTarget < (PBYTE)0xffffffff40000000) {
		pbTry = xhook_alloc_region_from_lo(pbTarget + 0x40000000, (PBYTE)pHi);
	}
	// Try looking 1GB below or higher.
	if (pbTry == NULL && pbTarget >(PBYTE)0x40000000) {
		pbTry = xhook_alloc_region_from_lo(pbTarget - 0x40000000, pbTarget);
	}
	// Try looking 1GB above or lower.
	if (pbTry == NULL && pbTarget < (PBYTE)0xffffffff40000000) {
		pbTry = xhook_alloc_region_from_hi(pbTarget, pbTarget + 0x40000000);
	}
	// Try anything below.
	if (pbTry == NULL) {
		pbTry = xhook_alloc_region_from_hi((PBYTE)pLo, pbTarget);
	}
	// try anything above.
	if (pbTry == NULL) {
		pbTry = xhook_alloc_region_from_lo(pbTarget, (PBYTE)pHi);
	}

	if (pbTry != NULL) {
		s_pRegion = (XHOOK_REGION*)pbTry;
		s_pRegion->dwSignature = XHOOK_REGION_SIGNATURE;
		s_pRegion->pFree = NULL;
		s_pRegion->pNext = s_pRegions;
		s_pRegions = s_pRegion;
		XHOOK_TRACE(("  Allocated region %p..%p\n\n",
			s_pRegion, ((PBYTE)s_pRegion) + XHOOK_REGION_SIZE - 1));

		// Put everything but the first trampoline on the free list.
		PBYTE pFree = NULL;
		pTrampoline = ((PXHOOK_TRAMPOLINE)s_pRegion) + 1;
		for (int i = XHOOK_TRAMPOLINES_PER_REGION - 1; i > 1; i--) {
			pTrampoline[i].pbRemain = pFree;
			pFree = (PBYTE)&pTrampoline[i];
		}
		s_pRegion->pFree = (PXHOOK_TRAMPOLINE)pFree;
		goto found_region;
	}

	XHOOK_TRACE(("Couldn't find available memory region!\n"));
	return NULL;
}

static void xhook_free_trampoline(PXHOOK_TRAMPOLINE pTrampoline)
{
	PXHOOK_REGION pRegion = (PXHOOK_REGION)
		((ULONG_PTR)pTrampoline & ~(ULONG_PTR)0xffff);

	memset(pTrampoline, 0, sizeof(*pTrampoline));
	pTrampoline->pbRemain = (PBYTE)pRegion->pFree;
	pRegion->pFree = pTrampoline;
}

static BOOL xhook_is_region_empty(PXHOOK_REGION pRegion)
{
	// Stop if the region isn't a region (this would be bad).
	if (pRegion->dwSignature != XHOOK_REGION_SIGNATURE) {
		return FALSE;
	}

	PBYTE pbRegionBeg = (PBYTE)pRegion;
	PBYTE pbRegionLim = pbRegionBeg + XHOOK_REGION_SIZE;

	// Stop if any of the trampolines aren't free.
	PXHOOK_TRAMPOLINE pTrampoline = ((PXHOOK_TRAMPOLINE)pRegion) + 1;
	for (int i = 0; i < XHOOK_TRAMPOLINES_PER_REGION; i++) {
		if (pTrampoline[i].pbRemain != NULL &&
			(pTrampoline[i].pbRemain < pbRegionBeg ||
			pTrampoline[i].pbRemain >= pbRegionLim)) {
			return FALSE;
		}
	}

	// OK, the region is empty.
	return TRUE;
}

static void xhook_free_unused_trampoline_regions()
{
	PXHOOK_REGION *ppRegionBase = &s_pRegions;
	PXHOOK_REGION pRegion = s_pRegions;

	while (pRegion != NULL) {
		if (xhook_is_region_empty(pRegion)) {
			*ppRegionBase = pRegion->pNext;

			VirtualFree(pRegion, 0, MEM_RELEASE);
			s_pRegion = NULL;
		}
		else {
			ppRegionBase = &pRegion->pNext;
		}
		pRegion = *ppRegionBase;
	}
}

///////////////////////////////////////////////////////// Transaction Structs.
//
struct XHookThread
{
	XHookThread *      pNext;
	HANDLE              hThread;
};

struct XHookOperation
{
	XHookOperation *   pNext;
	BOOL                fIsRemove;
	PBYTE *             ppbPointer;
	PBYTE               pbTarget;
	PXHOOK_TRAMPOLINE  pTrampoline;
	ULONG               dwPerm;
};

static BOOL                 s_fIgnoreTooSmall = FALSE;
static BOOL                 s_fRetainRegions = FALSE;

static LONG                 s_nPendingThreadId = 0; // Thread owning pending transaction.
static LONG                 s_nPendingError = NO_ERROR;
static PVOID *              s_ppPendingError = NULL;
static XHookThread *       s_pPendingThreads = NULL;
static XHookOperation *    s_pPendingOperations = NULL;

//////////////////////////////////////////////////////////////////////////////
//
PVOID WINAPI XHookCodeFromPointer(PVOID pPointer, PVOID *ppGlobals)
{
	return xhook_skip_jmp((PBYTE)pPointer, ppGlobals);
}

//////////////////////////////////////////////////////////// Transaction APIs.
//
BOOL WINAPI XHookSetIgnoreTooSmall(BOOL fIgnore)
{
	BOOL fPrevious = s_fIgnoreTooSmall;
	s_fIgnoreTooSmall = fIgnore;
	return fPrevious;
}

BOOL WINAPI XHookSetRetainRegions(BOOL fRetain)
{
	BOOL fPrevious = s_fRetainRegions;
	s_fRetainRegions = fRetain;
	return fPrevious;
}

LONG WINAPI XHookTransactionBegin()
{
	// Only one transaction is allowed at a time.
	if (s_nPendingThreadId != 0) {
		return ERROR_INVALID_OPERATION;
	}
	// Make sure only one thread can start a transaction.
	if (InterlockedCompareExchange(&s_nPendingThreadId, (LONG)GetCurrentThreadId(), 0) != 0) {
		return ERROR_INVALID_OPERATION;
	}

	s_pPendingOperations = NULL;
	s_pPendingThreads = NULL;
	s_nPendingError = NO_ERROR;
	s_ppPendingError = NULL;

	// Make sure the trampoline pages are writable.
	xhook_writable_trampoline_regions();

	return NO_ERROR;
}

LONG WINAPI XHookTransactionAbort()
{
	if (s_nPendingThreadId != (LONG)GetCurrentThreadId()) {
		return ERROR_INVALID_OPERATION;
	}

	// Restore all of the page permissions.
	for (XHookOperation *o = s_pPendingOperations; o != NULL;) {
		// We don't care if this fails, because the code is still accessible.
		DWORD dwOld;
		VirtualProtect(o->pbTarget, o->pTrampoline->cbRestore,
			o->dwPerm, &dwOld);

		if (!o->fIsRemove) {
			if (o->pTrampoline) {
				xhook_free_trampoline(o->pTrampoline);
				o->pTrampoline = NULL;
			}
		}

		XHookOperation *n = o->pNext;
		delete o;
		o = n;
	}
	s_pPendingOperations = NULL;

	// Make sure the trampoline pages are no longer writable.
	xhook_runnable_trampoline_regions();

	// Resume any suspended threads.
	for (XHookThread *t = s_pPendingThreads; t != NULL;) {
		// There is nothing we can do if this fails.
		ResumeThread(t->hThread);

		XHookThread *n = t->pNext;
		delete t;
		t = n;
	}
	s_pPendingThreads = NULL;
	s_nPendingThreadId = 0;

	return NO_ERROR;
}

LONG WINAPI XHookTransactionCommit()
{
	return XHookTransactionCommitEx(NULL);
}

static BYTE xhook_align_from_trampoline(PXHOOK_TRAMPOLINE pTrampoline, BYTE obTrampoline)
{
	for (LONG n = 0; n < ARRAYSIZE(pTrampoline->rAlign); n++) {
		if (pTrampoline->rAlign[n].obTrampoline == obTrampoline) {
			return pTrampoline->rAlign[n].obTarget;
		}
	}
	return 0;
}

static LONG xhook_align_from_target(PXHOOK_TRAMPOLINE pTrampoline, LONG obTarget)
{
	for (LONG n = 0; n < ARRAYSIZE(pTrampoline->rAlign); n++) {
		if (pTrampoline->rAlign[n].obTarget == obTarget) {
			return pTrampoline->rAlign[n].obTrampoline;
		}
	}
	return 0;
}

LONG WINAPI XHookTransactionCommitEx(PVOID **pppFailedPointer)
{
	if (pppFailedPointer != NULL) {
		// Used to get the last error.
		*pppFailedPointer = s_ppPendingError;
	}
	if (s_nPendingThreadId != (LONG)GetCurrentThreadId()) {
		return ERROR_INVALID_OPERATION;
	}

	// If any of the pending operations failed, then we abort the whole transaction.
	if (s_nPendingError != NO_ERROR) {
		XHOOK_BREAK();
		XHookTransactionAbort();
		return s_nPendingError;
	}

	// Common variables.
	XHookOperation *o;
	XHookThread *t;
	BOOL freed = FALSE;

	// Insert or remove each of the xhooks.
	for (o = s_pPendingOperations; o != NULL; o = o->pNext) {
		if (o->fIsRemove) {
			CopyMemory(o->pbTarget,
				o->pTrampoline->rbRestore,
				o->pTrampoline->cbRestore);
#ifdef XHOOKS_IA64
			*o->ppbPointer = (PBYTE)o->pTrampoline->ppldTarget;
#endif // XHOOKS_IA64

#ifdef XHOOKS_X86
			*o->ppbPointer = o->pbTarget;
#endif // XHOOKS_X86

#ifdef XHOOKS_X64
			*o->ppbPointer = o->pbTarget;
#endif // XHOOKS_X64

#ifdef XHOOKS_ARM
			*o->ppbPointer = XHOOKS_PBYTE_TO_PFUNC(o->pbTarget);
#endif // XHOOKS_ARM
		}
		else {
			XHOOK_TRACE(("xhooks: pbTramp =%p, pbRemain=%p, pbXHook=%p, cbRestore=%d\n",
				o->pTrampoline,
				o->pTrampoline->pbRemain,
				o->pTrampoline->pbXHook,
				o->pTrampoline->cbRestore));

			XHOOK_TRACE(("xhooks: pbTarget=%p: "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x [before]\n",
				o->pbTarget,
				o->pbTarget[0], o->pbTarget[1], o->pbTarget[2], o->pbTarget[3],
				o->pbTarget[4], o->pbTarget[5], o->pbTarget[6], o->pbTarget[7],
				o->pbTarget[8], o->pbTarget[9], o->pbTarget[10], o->pbTarget[11]));

#ifdef XHOOKS_IA64
			((XHOOK_IA64_BUNDLE*)o->pbTarget)
				->SetBrl((UINT64)&o->pTrampoline->bAllocFrame);
			*o->ppbPointer = (PBYTE)&o->pTrampoline->pldTrampoline;
#endif // XHOOKS_IA64

#ifdef XHOOKS_X64
			xhook_gen_jmp_indirect(o->pTrampoline->rbCodeIn, &o->pTrampoline->pbXHook);
			PBYTE pbCode = xhook_gen_jmp_immediate(o->pbTarget, o->pTrampoline->rbCodeIn);
			pbCode = xhook_gen_brk(pbCode, o->pTrampoline->pbRemain);
			*o->ppbPointer = o->pTrampoline->rbCode;
#endif // XHOOKS_X64

#ifdef XHOOKS_X86
			PBYTE pbCode = xhook_gen_jmp_immediate(o->pbTarget, o->pTrampoline->pbXHook);
			pbCode = xhook_gen_brk(pbCode, o->pTrampoline->pbRemain);
			*o->ppbPointer = o->pTrampoline->rbCode;
#endif // XHOOKS_X86

#ifdef XHOOKS_ARM
			PBYTE pbCode = xhook_gen_jmp_immediate(o->pbTarget, NULL, o->pTrampoline->pbXHook);
			pbCode = xhook_gen_brk(pbCode, o->pTrampoline->pbRemain);
			*o->ppbPointer = XHOOKS_PBYTE_TO_PFUNC(o->pTrampoline->rbCode);
#endif // XHOOKS_ARM

			XHOOK_TRACE(("xhooks: pbTarget=%p: "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x [after]\n",
				o->pbTarget,
				o->pbTarget[0], o->pbTarget[1], o->pbTarget[2], o->pbTarget[3],
				o->pbTarget[4], o->pbTarget[5], o->pbTarget[6], o->pbTarget[7],
				o->pbTarget[8], o->pbTarget[9], o->pbTarget[10], o->pbTarget[11]));

			XHOOK_TRACE(("xhooks: pbTramp =%p: "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x "
				"%02x %02x %02x %02x\n",
				o->pTrampoline,
				o->pTrampoline->rbCode[0], o->pTrampoline->rbCode[1],
				o->pTrampoline->rbCode[2], o->pTrampoline->rbCode[3],
				o->pTrampoline->rbCode[4], o->pTrampoline->rbCode[5],
				o->pTrampoline->rbCode[6], o->pTrampoline->rbCode[7],
				o->pTrampoline->rbCode[8], o->pTrampoline->rbCode[9],
				o->pTrampoline->rbCode[10], o->pTrampoline->rbCode[11]));

#ifdef XHOOKS_IA64
			XHOOK_TRACE(("\n"));
			XHOOK_TRACE(("xhooks:  &pldTrampoline  =%p\n",
				&o->pTrampoline->pldTrampoline));
			XHOOK_TRACE(("xhooks:  &bMovlTargetGp  =%p [%p]\n",
				&o->pTrampoline->bMovlTargetGp,
				o->pTrampoline->bMovlTargetGp.GetMovlGp()));
			XHOOK_TRACE(("xhooks:  &rbCode         =%p [%p]\n",
				&o->pTrampoline->rbCode,
				((XHOOK_IA64_BUNDLE&)o->pTrampoline->rbCode).GetBrlTarget()));
			XHOOK_TRACE(("xhooks:  &bBrlRemainEip  =%p [%p]\n",
				&o->pTrampoline->bBrlRemainEip,
				o->pTrampoline->bBrlRemainEip.GetBrlTarget()));
			XHOOK_TRACE(("xhooks:  &bMovlXHookGp  =%p [%p]\n",
				&o->pTrampoline->bMovlXHookGp,
				o->pTrampoline->bMovlXHookGp.GetMovlGp()));
			XHOOK_TRACE(("xhooks:  &bBrlXHookEip  =%p [%p]\n",
				&o->pTrampoline->bCallXHook,
				o->pTrampoline->bCallXHook.GetBrlTarget()));
			XHOOK_TRACE(("xhooks:  pldXHook       =%p [%p]\n",
				o->pTrampoline->ppldXHook->EntryPoint,
				o->pTrampoline->ppldXHook->GlobalPointer));
			XHOOK_TRACE(("xhooks:  pldTarget       =%p [%p]\n",
				o->pTrampoline->ppldTarget->EntryPoint,
				o->pTrampoline->ppldTarget->GlobalPointer));
			XHOOK_TRACE(("xhooks:  pbRemain        =%p\n",
				o->pTrampoline->pbRemain));
			XHOOK_TRACE(("xhooks:  pbXHook        =%p\n",
				o->pTrampoline->pbXHook));
			XHOOK_TRACE(("\n"));
#endif // XHOOKS_IA64
		}
	}

	// Update any suspended threads.
	for (t = s_pPendingThreads; t != NULL; t = t->pNext) {
		CONTEXT cxt;
		cxt.ContextFlags = CONTEXT_CONTROL;

#undef XHOOKS_EIP
#undef XHOOKS_EIP_TYPE

#ifdef XHOOKS_X86
#define XHOOKS_EIP         Eip
#define XHOOKS_EIP_TYPE    DWORD
#endif // XHOOKS_X86

#ifdef XHOOKS_X64
#define XHOOKS_EIP         Rip
#define XHOOKS_EIP_TYPE    DWORD64
#endif // XHOOKS_X64

#ifdef XHOOKS_IA64
#define XHOOKS_EIP         StIIP
#define XHOOKS_EIP_TYPE    ULONGLONG
#endif // XHOOKS_IA64

#ifdef XHOOKS_ARM
#define XHOOKS_EIP         Pc
#define XHOOKS_EIP_TYPE    DWORD
#endif // XHOOKS_ARM

		if (GetThreadContext(t->hThread, &cxt)) {
			for (XHookOperation *o = s_pPendingOperations; o != NULL; o = o->pNext) {
				if (o->fIsRemove) {
					if (cxt.XHOOKS_EIP >= (XHOOKS_EIP_TYPE)(ULONG_PTR)o->pTrampoline &&
						cxt.XHOOKS_EIP < (XHOOKS_EIP_TYPE)((ULONG_PTR)o->pTrampoline
						+ sizeof(o->pTrampoline))
						) {

						cxt.XHOOKS_EIP = (XHOOKS_EIP_TYPE)
							((ULONG_PTR)o->pbTarget
							+ xhook_align_from_trampoline(o->pTrampoline,
							(BYTE)(cxt.XHOOKS_EIP
							- (XHOOKS_EIP_TYPE)(ULONG_PTR)
							o->pTrampoline)));

						SetThreadContext(t->hThread, &cxt);
					}
				}
				else {
					if (cxt.XHOOKS_EIP >= (XHOOKS_EIP_TYPE)(ULONG_PTR)o->pbTarget &&
						cxt.XHOOKS_EIP < (XHOOKS_EIP_TYPE)((ULONG_PTR)o->pbTarget
						+ o->pTrampoline->cbRestore)
						) {

						cxt.XHOOKS_EIP = (XHOOKS_EIP_TYPE)
							((ULONG_PTR)o->pTrampoline
							+ xhook_align_from_target(o->pTrampoline,
							(BYTE)(cxt.XHOOKS_EIP
							- (XHOOKS_EIP_TYPE)(ULONG_PTR)
							o->pbTarget)));

						SetThreadContext(t->hThread, &cxt);
					}
				}
			}
		}
#undef XHOOKS_EIP
	}

	// Restore all of the page permissions and flush the icache.
	HANDLE hProcess = GetCurrentProcess();
	for (o = s_pPendingOperations; o != NULL;) {
		// We don't care if this fails, because the code is still accessible.
		DWORD dwOld;
		VirtualProtect(o->pbTarget, o->pTrampoline->cbRestore, o->dwPerm, &dwOld);
		FlushInstructionCache(hProcess, o->pbTarget, o->pTrampoline->cbRestore);

		if (o->fIsRemove && o->pTrampoline) {
			xhook_free_trampoline(o->pTrampoline);
			o->pTrampoline = NULL;
			freed = true;
		}

		XHookOperation *n = o->pNext;
		delete o;
		o = n;
	}
	s_pPendingOperations = NULL;

	// Free any trampoline regions that are now unused.
	if (freed && !s_fRetainRegions) {
		xhook_free_unused_trampoline_regions();
	}

	// Make sure the trampoline pages are no longer writable.
	xhook_runnable_trampoline_regions();

	// Resume any suspended threads.
	for (t = s_pPendingThreads; t != NULL;) {
		// There is nothing we can do if this fails.
		ResumeThread(t->hThread);

		XHookThread *n = t->pNext;
		delete t;
		t = n;
	}
	s_pPendingThreads = NULL;
	s_nPendingThreadId = 0;

	if (pppFailedPointer != NULL) {
		*pppFailedPointer = s_ppPendingError;
	}

	return s_nPendingError;
}

LONG WINAPI XHookUpdateThread(HANDLE hThread)
{
	LONG error;

	// If any of the pending operations failed, then we don't need to do this.
	if (s_nPendingError != NO_ERROR) {
		return s_nPendingError;
	}

	// Silently (and safely) drop any attempt to suspend our own thread.
	if (hThread == GetCurrentThread()) {
		return NO_ERROR;
	}

	XHookThread *t = new XHookThread;
	if (t == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
	fail:
		if (t != NULL) {
			delete t;
			t = NULL;
		}
		s_nPendingError = error;
		s_ppPendingError = NULL;
		XHOOK_BREAK();
		return error;
	}

	if (SuspendThread(hThread) == (DWORD)-1) {
		error = GetLastError();
		XHOOK_BREAK();
		goto fail;
	}

	t->hThread = hThread;
	t->pNext = s_pPendingThreads;
	s_pPendingThreads = t;

	return NO_ERROR;
}

///////////////////////////////////////////////////////////// Transacted APIs.
//
LONG WINAPI XHookAttach(PVOID *ppPointer,
	PVOID pXHook)
{
	return XHookAttachEx(ppPointer, pXHook, NULL, NULL, NULL);
}

LONG WINAPI XHookAttachEx(PVOID *ppPointer,
	PVOID pXHook,
	PXHOOK_TRAMPOLINE *ppRealTrampoline,
	PVOID *ppRealTarget,
	PVOID *ppRealXHook)
{
	LONG error = NO_ERROR;

	if (ppRealTrampoline != NULL) {
		*ppRealTrampoline = NULL;
	}
	if (ppRealTarget != NULL) {
		*ppRealTarget = NULL;
	}
	if (ppRealXHook != NULL) {
		*ppRealXHook = NULL;
	}

	if (s_nPendingThreadId != (LONG)GetCurrentThreadId()) {
		XHOOK_TRACE(("transaction conflict with thread id=%d\n", s_nPendingThreadId));
		return ERROR_INVALID_OPERATION;
	}

	// If any of the pending operations failed, then we don't need to do this.
	if (s_nPendingError != NO_ERROR) {
		XHOOK_TRACE(("pending transaction error=%d\n", s_nPendingError));
		return s_nPendingError;
	}

	if (ppPointer == NULL) {
		XHOOK_TRACE(("ppPointer is null\n"));
		return ERROR_INVALID_HANDLE;
	}
	if (*ppPointer == NULL) {
		error = ERROR_INVALID_HANDLE;
		s_nPendingError = error;
		s_ppPendingError = ppPointer;
		XHOOK_TRACE(("*ppPointer is null (ppPointer=%p)\n", ppPointer));
		XHOOK_BREAK();
		return error;
	}

	PBYTE pbTarget = (PBYTE)*ppPointer;
	PXHOOK_TRAMPOLINE pTrampoline = NULL;
	XHookOperation *o = NULL;

#ifdef XHOOKS_IA64
	PPLABEL_DESCRIPTOR ppldXHook = (PPLABEL_DESCRIPTOR)pXHook;
	PPLABEL_DESCRIPTOR ppldTarget = (PPLABEL_DESCRIPTOR)pbTarget;
	PVOID pXHookGlobals = NULL;
	PVOID pTargetGlobals = NULL;

	pXHook = (PBYTE)XHookCodeFromPointer(ppldXHook, &pXHookGlobals);
	pbTarget = (PBYTE)XHookCodeFromPointer(ppldTarget, &pTargetGlobals);
	XHOOK_TRACE(("  ppldXHook=%p, code=%p [gp=%p]\n",
		ppldXHook, pXHook, pXHookGlobals));
	XHOOK_TRACE(("  ppldTarget=%p, code=%p [gp=%p]\n",
		ppldTarget, pbTarget, pTargetGlobals));
#else // XHOOKS_IA64
	pbTarget = (PBYTE)XHookCodeFromPointer(pbTarget, NULL);
	pXHook = XHookCodeFromPointer(pXHook, NULL);
#endif // !XHOOKS_IA64

	// Don't follow a jump if its destination is the target function.
	// This happens when the xhook does nothing other than call the target.
	if (pXHook == (PVOID)pbTarget) {
		if (s_fIgnoreTooSmall) {
			goto stop;
		}
		else {
			XHOOK_BREAK();
			goto fail;
		}
	}

	if (ppRealTarget != NULL) {
		*ppRealTarget = pbTarget;
	}
	if (ppRealXHook != NULL) {
		*ppRealXHook = pXHook;
	}

	o = new XHookOperation;
	if (o == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
	fail:
		s_nPendingError = error;
		XHOOK_BREAK();
	stop:
		if (pTrampoline != NULL) {
			xhook_free_trampoline(pTrampoline);
			pTrampoline = NULL;
			if (ppRealTrampoline != NULL) {
				*ppRealTrampoline = NULL;
			}
		}
		if (o != NULL) {
			delete o;
			o = NULL;
		}
		s_ppPendingError = ppPointer;
		return error;
	}

	pTrampoline = xhook_alloc_trampoline(pbTarget);
	if (pTrampoline == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
		XHOOK_BREAK();
		goto fail;
	}

	if (ppRealTrampoline != NULL) {
		*ppRealTrampoline = pTrampoline;
	}

	XHOOK_TRACE(("xhooks: pbTramp=%p, pXHook=%p\n", pTrampoline, pXHook));

	memset(pTrampoline->rAlign, 0, sizeof(pTrampoline->rAlign));

	// Determine the number of movable target instructions.
	PBYTE pbSrc = pbTarget;
	PBYTE pbTrampoline = pTrampoline->rbCode;
	PBYTE pbPool = pbTrampoline + sizeof(pTrampoline->rbCode);
	ULONG cbTarget = 0;
	ULONG cbJump = SIZE_OF_JMP;
	ULONG nAlign = 0;

#ifdef XHOOKS_ARM
	// On ARM, we need an extra instruction when the function isn't 32-bit aligned.
	// Check if the existing code is another xhook (or at least a similar
	// "ldr pc, [PC+0]" jump.
	if ((ULONG)pbTarget & 2) {
		cbJump += 2;

		ULONG op = fetch_thumb_opcode(pbSrc);
		if (op == 0xbf00) {
			op = fetch_thumb_opcode(pbSrc + 2);
			if (op == 0xf8dff000) { // LDR PC,[PC]
				*((PUSHORT&)pbTrampoline)++ = *((PUSHORT&)pbSrc)++;
				*((PULONG&)pbTrampoline)++ = *((PULONG&)pbSrc)++;
				*((PULONG&)pbTrampoline)++ = *((PULONG&)pbSrc)++;
				cbTarget = (LONG)(pbSrc - pbTarget);
				// We will fall through the "while" because cbTarget is now >= cbJump.
			}
		}
	}
	else {
		ULONG op = fetch_thumb_opcode(pbSrc);
		if (op == 0xf8dff000) { // LDR PC,[PC]
			*((PULONG&)pbTrampoline)++ = *((PULONG&)pbSrc)++;
			*((PULONG&)pbTrampoline)++ = *((PULONG&)pbSrc)++;
			cbTarget = (LONG)(pbSrc - pbTarget);
			// We will fall through the "while" because cbTarget is now >= cbJump.
		}
	}
#endif

	while (cbTarget < cbJump) {
		PBYTE pbOp = pbSrc;
		LONG lExtra = 0;

		XHOOK_TRACE((" XHookCopyInstruction(%p,%p)\n",
			pbTrampoline, pbSrc));
		pbSrc = (PBYTE)
			XHookCopyInstruction(pbTrampoline, (PVOID*)&pbPool, pbSrc, NULL, &lExtra);
		XHOOK_TRACE((" XHookCopyInstruction() = %p (%d bytes)\n",
			pbSrc, (int)(pbSrc - pbOp)));
		pbTrampoline += (pbSrc - pbOp) + lExtra;
		cbTarget = (LONG)(pbSrc - pbTarget);
		pTrampoline->rAlign[nAlign].obTarget = cbTarget;
		pTrampoline->rAlign[nAlign].obTrampoline = pbTrampoline - pTrampoline->rbCode;

		if (xhook_does_code_end_function(pbOp)) {
			break;
		}
	}

	// Consume, but don't duplicate padding if it is needed and available.
	while (cbTarget < cbJump) {
		LONG cFiller = xhook_is_code_filler(pbSrc);
		if (cFiller == 0) {
			break;
		}

		pbSrc += cFiller;
		cbTarget = (LONG)(pbSrc - pbTarget);
	}

#if XHOOK_DEBUG
	{
		XHOOK_TRACE((" xhooks: rAlign ["));
		LONG n = 0;
		for (n = 0; n < ARRAYSIZE(pTrampoline->rAlign); n++) {
			if (pTrampoline->rAlign[n].obTarget == 0 &&
				pTrampoline->rAlign[n].obTrampoline == 0) {
				break;
			}
			XHOOK_TRACE((" %d/%d",
				pTrampoline->rAlign[n].obTarget,
				pTrampoline->rAlign[n].obTrampoline
				));

		}
		XHOOK_TRACE((" ]\n"));
	}
#endif

	if (cbTarget < cbJump || nAlign > ARRAYSIZE(pTrampoline->rAlign)) {
		// Too few instructions.

		error = ERROR_INVALID_BLOCK;
		if (s_fIgnoreTooSmall) {
			goto stop;
		}
		else {
			XHOOK_BREAK();
			goto fail;
		}
	}

	if (pbTrampoline > pbPool) {
		__debugbreak();
	}

#if 0 // [GalenH]
	if (cbTarget < pbTrampoline - pTrampoline->rbCode) {
		__debugbreak();
	}
#endif

	pTrampoline->cbCode = (BYTE)(pbTrampoline - pTrampoline->rbCode);
	pTrampoline->cbRestore = (BYTE)cbTarget;
	CopyMemory(pTrampoline->rbRestore, pbTarget, cbTarget);

#if !defined(XHOOKS_IA64)
	if (cbTarget > sizeof(pTrampoline->rbCode) - cbJump) {
		// Too many instructions.
		error = ERROR_INVALID_HANDLE;
		XHOOK_BREAK();
		goto fail;
	}
#endif // !XHOOKS_IA64

	pTrampoline->pbRemain = pbTarget + cbTarget;
	pTrampoline->pbXHook = (PBYTE)pXHook;

#ifdef XHOOKS_IA64
	pTrampoline->ppldXHook = ppldXHook;
	pTrampoline->ppldTarget = ppldTarget;
	pTrampoline->pldTrampoline.EntryPoint = (UINT64)&pTrampoline->bMovlTargetGp;
	pTrampoline->pldTrampoline.GlobalPointer = (UINT64)pXHookGlobals;

	((XHOOK_IA64_BUNDLE *)pTrampoline->rbCode)->SetStop();

	pTrampoline->bMovlTargetGp.SetMovlGp((UINT64)pTargetGlobals);
	pTrampoline->bBrlRemainEip.SetBrl((UINT64)pTrampoline->pbRemain);

	// Alloc frame:      alloc r41=ar.pfs,11,0,8,0; mov r40=rp
	pTrampoline->bAllocFrame.wide[0] = 0x00000580164d480c;
	pTrampoline->bAllocFrame.wide[1] = 0x00c4000500000200;
	// save r36, r37, r38.
	pTrampoline->bSave37to39.wide[0] = 0x031021004e019001;
	pTrampoline->bSave37to39.wide[1] = 0x8401280600420098;
	// save r34,r35,r36: adds r47=0,r36; adds r46=0,r35; adds r45=0,r34
	pTrampoline->bSave34to36.wide[0] = 0x02e0210048017800;
	pTrampoline->bSave34to36.wide[1] = 0x84011005a042008c;
	// save gp,r32,r33"  adds r44=0,r33; adds r43=0,r32; adds r42=0,gp ;;
	pTrampoline->bSaveGPto33.wide[0] = 0x02b0210042016001;
	pTrampoline->bSaveGPto33.wide[1] = 0x8400080540420080;
	// set xhook GP.
	pTrampoline->bMovlXHookGp.SetMovlGp((UINT64)pXHookGlobals);
	// call xhook:      brl.call.sptk.few rp=xhook ;;
	pTrampoline->bCallXHook.wide[0] = 0x0000000100000005;
	pTrampoline->bCallXHook.wide[1] = 0xd000001000000000;
	pTrampoline->bCallXHook.SetBrlTarget((UINT64)pXHook);
	// pop frame & gp:   adds gp=0,r42; mov rp=r40,+0;; mov.i ar.pfs=r41
	pTrampoline->bPopFrameGp.wide[0] = 0x4000210054000802;
	pTrampoline->bPopFrameGp.wide[1] = 0x00aa029000038005;
	// return to caller: br.ret.sptk.many rp ;;
	pTrampoline->bReturn.wide[0] = 0x0000000100000019;
	pTrampoline->bReturn.wide[1] = 0x0084000880000200;

	XHOOK_TRACE(("xhooks: &bMovlTargetGp=%p\n", &pTrampoline->bMovlTargetGp));
	XHOOK_TRACE(("xhooks: &bMovlXHookGp=%p\n", &pTrampoline->bMovlXHookGp));
#endif // XHOOKS_IA64

	pbTrampoline = pTrampoline->rbCode + pTrampoline->cbCode;
#ifdef XHOOKS_X64
	pbTrampoline = xhook_gen_jmp_indirect(pbTrampoline, &pTrampoline->pbRemain);
	pbTrampoline = xhook_gen_brk(pbTrampoline, pbPool);
#endif // XHOOKS_X64

#ifdef XHOOKS_X86
	pbTrampoline = xhook_gen_jmp_immediate(pbTrampoline, pTrampoline->pbRemain);
	pbTrampoline = xhook_gen_brk(pbTrampoline, pbPool);
#endif // XHOOKS_X86

#ifdef XHOOKS_ARM
	pbTrampoline = xhook_gen_jmp_immediate(pbTrampoline, &pbPool, pTrampoline->pbRemain);
	pbTrampoline = xhook_gen_brk(pbTrampoline, pbPool);
#endif // XHOOKS_ARM

	DWORD dwOld = 0;
	if (!VirtualProtect(pbTarget, cbTarget, PAGE_EXECUTE_READWRITE, &dwOld)) {
		error = GetLastError();
		XHOOK_BREAK();
		goto fail;
	}

	XHOOK_TRACE(("xhooks: pbTarget=%p: "
		"%02x %02x %02x %02x "
		"%02x %02x %02x %02x "
		"%02x %02x %02x %02x\n",
		pbTarget,
		pbTarget[0], pbTarget[1], pbTarget[2], pbTarget[3],
		pbTarget[4], pbTarget[5], pbTarget[6], pbTarget[7],
		pbTarget[8], pbTarget[9], pbTarget[10], pbTarget[11]));
	XHOOK_TRACE(("xhooks: pbTramp =%p: "
		"%02x %02x %02x %02x "
		"%02x %02x %02x %02x "
		"%02x %02x %02x %02x\n",
		pTrampoline,
		pTrampoline->rbCode[0], pTrampoline->rbCode[1],
		pTrampoline->rbCode[2], pTrampoline->rbCode[3],
		pTrampoline->rbCode[4], pTrampoline->rbCode[5],
		pTrampoline->rbCode[6], pTrampoline->rbCode[7],
		pTrampoline->rbCode[8], pTrampoline->rbCode[9],
		pTrampoline->rbCode[10], pTrampoline->rbCode[11]));

	o->fIsRemove = FALSE;
	o->ppbPointer = (PBYTE*)ppPointer;
	o->pTrampoline = pTrampoline;
	o->pbTarget = pbTarget;
	o->dwPerm = dwOld;
	o->pNext = s_pPendingOperations;
	s_pPendingOperations = o;

	return NO_ERROR;
}

LONG WINAPI XHookDetach(PVOID *ppPointer,
	PVOID pXHook)
{
	LONG error = NO_ERROR;

	if (s_nPendingThreadId != (LONG)GetCurrentThreadId()) {
		return ERROR_INVALID_OPERATION;
	}

	// If any of the pending operations failed, then we don't need to do this.
	if (s_nPendingError != NO_ERROR) {
		return s_nPendingError;
	}

	if (ppPointer == NULL) {
		return ERROR_INVALID_HANDLE;
	}
	if (*ppPointer == NULL) {
		error = ERROR_INVALID_HANDLE;
		s_nPendingError = error;
		s_ppPendingError = ppPointer;
		XHOOK_BREAK();
		return error;
	}

	XHookOperation *o = new XHookOperation;
	if (o == NULL) {
		error = ERROR_NOT_ENOUGH_MEMORY;
	fail:
		s_nPendingError = error;
		XHOOK_BREAK();
	stop:
		if (o != NULL) {
			delete o;
			o = NULL;
		}
		s_ppPendingError = ppPointer;
		return error;
	}


#ifdef XHOOKS_IA64
	PPLABEL_DESCRIPTOR ppldTrampo = (PPLABEL_DESCRIPTOR)*ppPointer;
	PPLABEL_DESCRIPTOR ppldXHook = (PPLABEL_DESCRIPTOR)pXHook;
	PVOID pXHookGlobals = NULL;
	PVOID pTrampoGlobals = NULL;

	pXHook = (PBYTE)XHookCodeFromPointer(ppldXHook, &pXHookGlobals);
	PXHOOK_TRAMPOLINE pTrampoline = (PXHOOK_TRAMPOLINE)
		XHookCodeFromPointer(ppldTrampo, &pTrampoGlobals);
	XHOOK_TRACE(("  ppldXHook=%p, code=%p [gp=%p]\n",
		ppldXHook, pXHook, pXHookGlobals));
	XHOOK_TRACE(("  ppldTrampo=%p, code=%p [gp=%p]\n",
		ppldTrampo, pTrampoline, pTrampoGlobals));


	XHOOK_TRACE(("\n"));
	XHOOK_TRACE(("xhooks:  &pldTrampoline  =%p\n",
		&pTrampoline->pldTrampoline));
	XHOOK_TRACE(("xhooks:  &bMovlTargetGp  =%p [%p]\n",
		&pTrampoline->bMovlTargetGp,
		pTrampoline->bMovlTargetGp.GetMovlGp()));
	XHOOK_TRACE(("xhooks:  &rbCode         =%p [%p]\n",
		&pTrampoline->rbCode,
		((XHOOK_IA64_BUNDLE&)pTrampoline->rbCode).GetBrlTarget()));
	XHOOK_TRACE(("xhooks:  &bBrlRemainEip  =%p [%p]\n",
		&pTrampoline->bBrlRemainEip,
		pTrampoline->bBrlRemainEip.GetBrlTarget()));
	XHOOK_TRACE(("xhooks:  &bMovlXHookGp  =%p [%p]\n",
		&pTrampoline->bMovlXHookGp,
		pTrampoline->bMovlXHookGp.GetMovlGp()));
	XHOOK_TRACE(("xhooks:  &bBrlXHookEip  =%p [%p]\n",
		&pTrampoline->bCallXHook,
		pTrampoline->bCallXHook.GetBrlTarget()));
	XHOOK_TRACE(("xhooks:  pldXHook       =%p [%p]\n",
		pTrampoline->ppldXHook->EntryPoint,
		pTrampoline->ppldXHook->GlobalPointer));
	XHOOK_TRACE(("xhooks:  pldTarget       =%p [%p]\n",
		pTrampoline->ppldTarget->EntryPoint,
		pTrampoline->ppldTarget->GlobalPointer));
	XHOOK_TRACE(("xhooks:  pbRemain        =%p\n",
		pTrampoline->pbRemain));
	XHOOK_TRACE(("xhooks:  pbXHook        =%p\n",
		pTrampoline->pbXHook));
	XHOOK_TRACE(("\n"));
#else // !XHOOKS_IA64
	PXHOOK_TRAMPOLINE pTrampoline =
		(PXHOOK_TRAMPOLINE)XHookCodeFromPointer(*ppPointer, NULL);
	pXHook = XHookCodeFromPointer(pXHook, NULL);
#endif // !XHOOKS_IA64

	////////////////////////////////////// Verify that Trampoline is in place.
	//
	LONG cbTarget = pTrampoline->cbRestore;
	PBYTE pbTarget = pTrampoline->pbRemain - cbTarget;
	if (cbTarget == 0 || cbTarget > sizeof(pTrampoline->rbCode)) {
		error = ERROR_INVALID_BLOCK;
		if (s_fIgnoreTooSmall) {
			goto stop;
		}
		else {
			XHOOK_BREAK();
			goto fail;
		}
	}

	if (pTrampoline->pbXHook != pXHook) {
		error = ERROR_INVALID_BLOCK;
		if (s_fIgnoreTooSmall) {
			goto stop;
		}
		else {
			XHOOK_BREAK();
			goto fail;
		}
	}

	DWORD dwOld = 0;
	if (!VirtualProtect(pbTarget, cbTarget,
		PAGE_EXECUTE_READWRITE, &dwOld)) {
		error = GetLastError();
		XHOOK_BREAK();
		goto fail;
	}

	o->fIsRemove = TRUE;
	o->ppbPointer = (PBYTE*)ppPointer;
	o->pTrampoline = pTrampoline;
	o->pbTarget = pbTarget;
	o->dwPerm = dwOld;
	o->pNext = s_pPendingOperations;
	s_pPendingOperations = o;

	return NO_ERROR;
}

//  End of File

namespace XHook
{
	//////////////////////////////////////////////////////////////////////////////
	//
#ifndef _STRSAFE_H_INCLUDED_
	static inline HRESULT StringCchLengthA(const char* psz, size_t cchMax, size_t* pcch)
	{
		HRESULT hr = S_OK;
		size_t cchMaxPrev = cchMax;

		if (cchMax > 2147483647)
		{
			return ERROR_INVALID_PARAMETER;
		}

		while (cchMax && (*psz != '\0'))
		{
			psz++;
			cchMax--;
		}

		if (cchMax == 0)
		{
			// the string is longer than cchMax
			hr = ERROR_INVALID_PARAMETER;
		}

		if (SUCCEEDED(hr) && pcch)
		{
			*pcch = cchMaxPrev - cchMax;
		}

		return hr;
	}


	static inline HRESULT StringCchCopyA(char* pszDest, size_t cchDest, const char* pszSrc)
	{
		HRESULT hr = S_OK;

		if (cchDest == 0)
		{
			// can not null terminate a zero-byte dest buffer
			hr = ERROR_INVALID_PARAMETER;
		}
		else
		{
			while (cchDest && (*pszSrc != '\0'))
			{
				*pszDest++ = *pszSrc++;
				cchDest--;
			}

			if (cchDest == 0)
			{
				// we are going to truncate pszDest
				pszDest--;
				hr = ERROR_INVALID_PARAMETER;
			}

			*pszDest = '\0';
		}

		return hr;
	}

	static inline HRESULT StringCchCatA(char* pszDest, size_t cchDest, const char* pszSrc)
	{
		HRESULT hr;
		size_t cchDestCurrent;

		if (cchDest > 2147483647)
		{
			return ERROR_INVALID_PARAMETER;
		}

		hr = StringCchLengthA(pszDest, cchDest, &cchDestCurrent);

		if (SUCCEEDED(hr))
		{
			hr = StringCchCopyA(pszDest + cchDestCurrent,
				cchDest - cchDestCurrent,
				pszSrc);
		}

		return hr;
	}

#endif

	///////////////////////////////////////////////////////////////////////////////
	//
	class CImageData
	{
		friend class CImage;

	public:
		CImageData(PBYTE pbData, DWORD cbData);
		~CImageData();

		PBYTE                   Enumerate(GUID *pGuid, DWORD *pcbData, DWORD *pnIterator);
		PBYTE                   Find(REFGUID rguid, DWORD *pcbData);
		PBYTE                   Set(REFGUID rguid, PBYTE pbData, DWORD cbData);

		BOOL                    Delete(REFGUID rguid);
		BOOL                    Purge();

		BOOL                    IsEmpty()           { return m_cbData == 0; }
		BOOL                    IsValid();

	protected:
		BOOL                    SizeTo(DWORD cbData);

	protected:
		PBYTE                   m_pbData;
		DWORD                   m_cbData;
		DWORD                   m_cbAlloc;
	};

	class CImageImportFile
	{
		friend class CImage;
		friend class CImageImportName;

	public:
		CImageImportFile();
		~CImageImportFile();

	public:
		CImageImportFile *      m_pNextFile;
		BOOL                    m_fByway;

		CImageImportName *      m_pImportNames;
		DWORD                   m_nImportNames;

		DWORD                   m_rvaOriginalFirstThunk;
		DWORD                   m_rvaFirstThunk;

		DWORD                   m_nForwarderChain;
		PCHAR                   m_pszOrig;
		PCHAR                   m_pszName;
	};

	class CImageImportName
	{
		friend class CImage;
		friend class CImageImportFile;

	public:
		CImageImportName();
		~CImageImportName();

	public:
		WORD        m_nHint;
		ULONG       m_nOrig;
		ULONG       m_nOrdinal;
		PCHAR       m_pszOrig;
		PCHAR       m_pszName;
	};

	class CImage
	{
		friend class CImageThunks;
		friend class CImageChars;
		friend class CImageImportFile;
		friend class CImageImportName;

	public:
		CImage();
		~CImage();

		static CImage *         IsValid(PXHOOK_BINARY pBinary);

	public:                                                 // File Functions
		BOOL                    Read(HANDLE hFile);
		BOOL                    Write(HANDLE hFile);
		BOOL                    Close();

	public:                                                 // Manipulation Functions
		PBYTE                   DataEnum(GUID *pGuid, DWORD *pcbData, DWORD *pnIterator);
		PBYTE                   DataFind(REFGUID rguid, DWORD *pcbData);
		PBYTE                   DataSet(REFGUID rguid, PBYTE pbData, DWORD cbData);
		BOOL                    DataDelete(REFGUID rguid);
		BOOL                    DataPurge();

		BOOL                    EditImports(PVOID pContext,
			PF_XHOOK_BINARY_BYWAY_CALLBACK pfBywayCallback,
			PF_XHOOK_BINARY_FILE_CALLBACK pfFileCallback,
			PF_XHOOK_BINARY_SYMBOL_CALLBACK pfSymbolCallback,
			PF_XHOOK_BINARY_COMMIT_CALLBACK pfCommitCallback);

	protected:
		BOOL                    WriteFile(HANDLE hFile,
			LPCVOID lpBuffer,
			DWORD nNumberOfBytesToWrite,
			LPDWORD lpNumberOfBytesWritten);
		BOOL                    CopyFileData(HANDLE hFile, DWORD nOldPos, DWORD cbData);
		BOOL                    ZeroFileData(HANDLE hFile, DWORD cbData);
		BOOL                    AlignFileData(HANDLE hFile);

		BOOL                    SizeOutputBuffer(DWORD cbData);
		PBYTE                   AllocateOutput(DWORD cbData, DWORD *pnVirtAddr);

		PVOID                   RvaToVa(ULONG_PTR nRva);
		DWORD                   RvaToFileOffset(DWORD nRva);

		DWORD                   FileAlign(DWORD nAddr);
		DWORD                   SectionAlign(DWORD nAddr);

		BOOL                    CheckImportsNeeded(DWORD *pnTables,
			DWORD *pnThunks,
			DWORD *pnChars);

		CImageImportFile *      NewByway(__in_z PCHAR pszName);

	private:
		DWORD                   m_dwValidSignature;
		CImageData *            m_pImageData;               // Read & Write

		HANDLE                  m_hMap;                     // Read & Write
		PBYTE                   m_pMap;                     // Read & Write

		DWORD                   m_nNextFileAddr;            // Write
		DWORD                   m_nNextVirtAddr;            // Write

		IMAGE_DOS_HEADER        m_DosHeader;                // Read & Write
		IMAGE_NT_HEADERS        m_NtHeader;                 // Read & Write
		IMAGE_SECTION_HEADER    m_SectionHeaders[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];

		DWORD                   m_nPrePE;
		DWORD                   m_cbPrePE;
		DWORD                   m_cbPostPE;

		DWORD                   m_nPeOffset;
		DWORD                   m_nSectionsOffset;
		DWORD                   m_nExtraOffset;
		DWORD                   m_nFileSize;

		DWORD                   m_nOutputVirtAddr;
		DWORD                   m_nOutputVirtSize;
		DWORD                   m_nOutputFileAddr;

		PBYTE                   m_pbOutputBuffer;
		DWORD                   m_cbOutputBuffer;

		CImageImportFile *      m_pImportFiles;
		DWORD                   m_nImportFiles;

		BOOL                    m_fHadXHookSection;

	private:
		enum {
			XHOOK_IMAGE_VALID_SIGNATURE = 0xfedcba01,      // "Dtr\0"
		};
	};

	//////////////////////////////////////////////////////////////////////////////
	//
	static BYTE s_rbDosCode[0x10] = {
		0x0E, 0x1F, 0xBA, 0x0E, 0x00, 0xB4, 0x09, 0xCD,
		0x21, 0xB8, 0x01, 0x4C, 0xCD, 0x21, '*', '*'
	};

	static inline DWORD Max(DWORD a, DWORD b)
	{
		return a > b ? a : b;
	}

	static inline DWORD Align(DWORD a, DWORD size)
	{
		size--;
		return (a + size) & ~size;
	}

	static inline DWORD QuadAlign(DWORD a)
	{
		return Align(a, 8);
	}

	static PCHAR DuplicateString(__in_z PCHAR pszIn)
	{
		if (pszIn) {
			UINT nIn = (UINT)strlen(pszIn);
			PCHAR pszOut = new CHAR[nIn + 1];
			if (pszOut == NULL) {
				SetLastError(ERROR_OUTOFMEMORY);
			}
			else {
				CopyMemory(pszOut, pszIn, nIn + 1);
			}
			return pszOut;
		}
		return NULL;
	}

	static PCHAR ReplaceString(__deref_out PCHAR *ppsz, __in_z PCHAR pszIn)
	{
		if (ppsz == NULL) {
			return NULL;
		}

		UINT nIn;
		if (*ppsz != NULL) {
			if (strcmp(*ppsz, pszIn) == 0) {
				return *ppsz;
			}
			nIn = (UINT)strlen(pszIn);

			if (strlen(*ppsz) == nIn) {
				CopyMemory(*ppsz, pszIn, nIn + 1);
				return *ppsz;
			}
			else {
				delete[] * ppsz;
				*ppsz = NULL;
			}
		}
		else {
			nIn = (UINT)strlen(pszIn);
		}

		*ppsz = new CHAR[nIn + 1];
		if (*ppsz == NULL) {
			SetLastError(ERROR_OUTOFMEMORY);
		}
		else {
			CopyMemory(*ppsz, pszIn, nIn + 1);
		}
		return *ppsz;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	CImageImportFile::CImageImportFile()
	{
		m_pNextFile = NULL;
		m_fByway = FALSE;

		m_pImportNames = NULL;
		m_nImportNames = 0;

		m_rvaOriginalFirstThunk = 0;
		m_rvaFirstThunk = 0;

		m_nForwarderChain = (UINT)0;
		m_pszName = NULL;
		m_pszOrig = NULL;
	}

	CImageImportFile::~CImageImportFile()
	{
		if (m_pNextFile) {
			delete m_pNextFile;
			m_pNextFile = NULL;
		}
		if (m_pImportNames) {
			delete[] m_pImportNames;
			m_pImportNames = NULL;
			m_nImportNames = 0;
		}
		if (m_pszName) {
			delete[] m_pszName;
			m_pszName = NULL;
		}
		if (m_pszOrig) {
			delete[] m_pszOrig;
			m_pszOrig = NULL;
		}
	}

	CImageImportName::CImageImportName()
	{
		m_nOrig = 0;
		m_nOrdinal = 0;
		m_nHint = 0;
		m_pszName = NULL;
		m_pszOrig = NULL;
	}

	CImageImportName::~CImageImportName()
	{
		if (m_pszName) {
			delete[] m_pszName;
			m_pszName = NULL;
		}
		if (m_pszOrig) {
			delete[] m_pszOrig;
			m_pszOrig = NULL;
		}
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	CImageData::CImageData(PBYTE pbData, DWORD cbData)
	{
		m_pbData = pbData;
		m_cbData = cbData;
		m_cbAlloc = 0;
	}

	CImageData::~CImageData()
	{
		IsValid();

		if (m_cbAlloc == 0) {
			m_pbData = NULL;
		}
		if (m_pbData) {
			delete[] m_pbData;
			m_pbData = NULL;
		}
		m_cbData = 0;
		m_cbAlloc = 0;
	}

	BOOL CImageData::SizeTo(DWORD cbData)
	{
		IsValid();

		if (cbData <= m_cbAlloc) {
			return TRUE;
		}

		PBYTE pbNew = new BYTE[cbData];
		if (pbNew == NULL) {
			SetLastError(ERROR_OUTOFMEMORY);
			return FALSE;
		}

		if (m_pbData) {
			CopyMemory(pbNew, m_pbData, m_cbData);
			if (m_cbAlloc > 0) {
				delete[] m_pbData;
			}
			m_pbData = NULL;
		}
		m_pbData = pbNew;
		m_cbAlloc = cbData;

		IsValid();

		return TRUE;
	}

	BOOL CImageData::Purge()
	{
		m_cbData = 0;

		IsValid();

		return TRUE;
	}

	BOOL CImageData::IsValid()
	{
		if (m_pbData == NULL) {
			return TRUE;
		}

		PBYTE pbBeg = m_pbData;
		PBYTE pbEnd = m_pbData + m_cbData;

		for (PBYTE pbIter = pbBeg; pbIter < pbEnd;) {
			PXHOOK_SECTION_RECORD pRecord = (PXHOOK_SECTION_RECORD)pbIter;

			if (pRecord->cbBytes < sizeof(XHOOK_SECTION_RECORD)) {
				return FALSE;
			}
			if (pRecord->nReserved != 0) {
				return FALSE;
			}

			pbIter += pRecord->cbBytes;
		}
		return TRUE;
	}

	PBYTE CImageData::Enumerate(GUID *pGuid, DWORD *pcbData, DWORD *pnIterator)
	{
		IsValid();

		if (pnIterator == NULL ||
			m_cbData < *pnIterator + sizeof(XHOOK_SECTION_RECORD)) {

			if (pcbData) {
				*pcbData = 0;
			}
			if (pGuid) {
				ZeroMemory(pGuid, sizeof(*pGuid));
			}
			return NULL;
		}

		PXHOOK_SECTION_RECORD pRecord = (PXHOOK_SECTION_RECORD)(m_pbData + *pnIterator);

		if (pGuid) {
			*pGuid = pRecord->guid;
		}
		if (pcbData) {
			*pcbData = pRecord->cbBytes - sizeof(XHOOK_SECTION_RECORD);
		}
		*pnIterator = (LONG)(((PBYTE)pRecord - m_pbData) + pRecord->cbBytes);

		return (PBYTE)(pRecord + 1);
	}

	PBYTE CImageData::Find(REFGUID rguid, DWORD *pcbData)
	{
		IsValid();

		DWORD cbBytes = sizeof(XHOOK_SECTION_RECORD);
		for (DWORD nOffset = 0; nOffset < m_cbData; nOffset += cbBytes) {
			PXHOOK_SECTION_RECORD pRecord = (PXHOOK_SECTION_RECORD)(m_pbData + nOffset);

			cbBytes = pRecord->cbBytes;
			if (cbBytes > m_cbData) {
				break;
			}
			if (cbBytes < sizeof(XHOOK_SECTION_RECORD)) {
				continue;
			}

			if (pRecord->guid.Data1 == rguid.Data1 &&
				pRecord->guid.Data2 == rguid.Data2 &&
				pRecord->guid.Data3 == rguid.Data3 &&
				pRecord->guid.Data4[0] == rguid.Data4[0] &&
				pRecord->guid.Data4[1] == rguid.Data4[1] &&
				pRecord->guid.Data4[2] == rguid.Data4[2] &&
				pRecord->guid.Data4[3] == rguid.Data4[3] &&
				pRecord->guid.Data4[4] == rguid.Data4[4] &&
				pRecord->guid.Data4[5] == rguid.Data4[5] &&
				pRecord->guid.Data4[6] == rguid.Data4[6] &&
				pRecord->guid.Data4[7] == rguid.Data4[7]) {

				*pcbData = cbBytes - sizeof(XHOOK_SECTION_RECORD);
				return (PBYTE)(pRecord + 1);
			}
		}

		if (pcbData) {
			*pcbData = 0;
		}
		return NULL;
	}

	BOOL CImageData::Delete(REFGUID rguid)
	{
		IsValid();

		PBYTE pbFound = NULL;
		DWORD cbFound = 0;

		pbFound = Find(rguid, &cbFound);
		if (pbFound == NULL) {
			SetLastError(ERROR_MOD_NOT_FOUND);
			return FALSE;
		}

		pbFound -= sizeof(XHOOK_SECTION_RECORD);
		cbFound += sizeof(XHOOK_SECTION_RECORD);

		PBYTE pbRestData = pbFound + cbFound;
		DWORD cbRestData = m_cbData - (LONG)(pbRestData - m_pbData);

		if (cbRestData) {
			MoveMemory(pbFound, pbRestData, cbRestData);
		}
		m_cbData -= cbFound;

		IsValid();
		return TRUE;
	}

	PBYTE CImageData::Set(REFGUID rguid, PBYTE pbData, DWORD cbData)
	{
		IsValid();
		Delete(rguid);

		DWORD cbAlloc = QuadAlign(cbData);

		if (!SizeTo(m_cbData + cbAlloc + sizeof(XHOOK_SECTION_RECORD))) {
			return NULL;
		}

		PXHOOK_SECTION_RECORD pRecord = (PXHOOK_SECTION_RECORD)(m_pbData + m_cbData);
		pRecord->cbBytes = cbAlloc + sizeof(XHOOK_SECTION_RECORD);
		pRecord->nReserved = 0;
		pRecord->guid = rguid;

		PBYTE pbDest = (PBYTE)(pRecord + 1);
		if (pbData) {
			CopyMemory(pbDest, pbData, cbData);
			if (cbData < cbAlloc) {
				ZeroMemory(pbDest + cbData, cbAlloc - cbData);
			}
		}
		else {
			if (cbAlloc > 0) {
				ZeroMemory(pbDest, cbAlloc);
			}
		}

		m_cbData += cbAlloc + sizeof(XHOOK_SECTION_RECORD);

		IsValid();
		return pbDest;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	class CImageThunks
	{
	private:
		CImage *            m_pImage;
		PIMAGE_THUNK_DATA   m_pThunks;
		DWORD               m_nThunks;
		DWORD               m_nThunksMax;
		DWORD               m_nThunkVirtAddr;

	public:
		CImageThunks(CImage *pImage, DWORD nThunksMax, DWORD *pnAddr)
		{
			m_pImage = pImage;
			m_nThunks = 0;
			m_nThunksMax = nThunksMax;
			m_pThunks = (PIMAGE_THUNK_DATA)
				m_pImage->AllocateOutput(sizeof(IMAGE_THUNK_DATA) * nThunksMax,
				&m_nThunkVirtAddr);
			*pnAddr = m_nThunkVirtAddr;
		}

		PIMAGE_THUNK_DATA Current(DWORD *pnVirtAddr)
		{
			if (m_nThunksMax > 1) {
				*pnVirtAddr = m_nThunkVirtAddr;
				return m_pThunks;
			}
			*pnVirtAddr = 0;
			return NULL;
		}

		PIMAGE_THUNK_DATA Allocate(ULONG_PTR nData, DWORD *pnVirtAddr)
		{
			if (m_nThunks < m_nThunksMax) {
				*pnVirtAddr = m_nThunkVirtAddr;

				m_nThunks++;
				m_nThunkVirtAddr += sizeof(IMAGE_THUNK_DATA);
				m_pThunks->u1.Ordinal = nData;
				return m_pThunks++;
			}
			*pnVirtAddr = 0;
			return NULL;
		}

		DWORD   Size()
		{
			return m_nThunksMax * sizeof(IMAGE_THUNK_DATA);
		}
	};

	//////////////////////////////////////////////////////////////////////////////
	//
	class CImageChars
	{
	private:
		CImage *        m_pImage;
		PCHAR           m_pChars;
		DWORD           m_nChars;
		DWORD           m_nCharsMax;
		DWORD           m_nCharVirtAddr;

	public:
		CImageChars(CImage *pImage, DWORD nCharsMax, DWORD *pnAddr)
		{
			m_pImage = pImage;
			m_nChars = 0;
			m_nCharsMax = nCharsMax;
			m_pChars = (PCHAR)m_pImage->AllocateOutput(nCharsMax, &m_nCharVirtAddr);
			*pnAddr = m_nCharVirtAddr;
		}

		PCHAR Allocate(__in_z PCHAR pszString, DWORD *pnVirtAddr)
		{
			DWORD nLen = (DWORD)strlen(pszString) + 1;
			nLen += (nLen & 1);

			if (m_nChars + nLen > m_nCharsMax) {
				*pnVirtAddr = 0;
				return NULL;
			}

			*pnVirtAddr = m_nCharVirtAddr;
			HRESULT hrRet = StringCchCopyA(m_pChars, m_nCharsMax, pszString);

			if (FAILED(hrRet)) {
				return NULL;
			}

			pszString = m_pChars;

			m_pChars += nLen;
			m_nChars += nLen;
			m_nCharVirtAddr += nLen;

			return pszString;
		}

		PCHAR Allocate(PCHAR pszString, DWORD nHint, DWORD *pnVirtAddr)
		{
			DWORD nLen = (DWORD)strlen(pszString) + 1 + sizeof(USHORT);
			nLen += (nLen & 1);

			if (m_nChars + nLen > m_nCharsMax) {
				*pnVirtAddr = 0;
				return NULL;
			}

			*pnVirtAddr = m_nCharVirtAddr;
			*(USHORT *)m_pChars = (USHORT)nHint;

			HRESULT hrRet = StringCchCopyA(m_pChars + sizeof(USHORT), m_nCharsMax, pszString);
			if (FAILED(hrRet)) {
				return NULL;
			}

			pszString = m_pChars + sizeof(USHORT);

			m_pChars += nLen;
			m_nChars += nLen;
			m_nCharVirtAddr += nLen;

			return pszString;
		}

		DWORD Size()
		{
			return m_nChars;
		}
	};

	//////////////////////////////////////////////////////////////////////////////
	//
	CImage * CImage::IsValid(PXHOOK_BINARY pBinary)
	{
		if (pBinary) {
			CImage *pImage = (CImage *)pBinary;

			if (pImage->m_dwValidSignature == XHOOK_IMAGE_VALID_SIGNATURE) {
				return pImage;
			}
		}
		SetLastError(ERROR_INVALID_HANDLE);
		return NULL;
	}

	CImage::CImage()
	{
		m_dwValidSignature = (DWORD)XHOOK_IMAGE_VALID_SIGNATURE;

		m_hMap = NULL;
		m_pMap = NULL;

		m_nPeOffset = 0;
		m_nSectionsOffset = 0;

		m_pbOutputBuffer = NULL;
		m_cbOutputBuffer = 0;

		m_pImageData = NULL;

		m_pImportFiles = NULL;
		m_nImportFiles = 0;

		m_fHadXHookSection = FALSE;
	}

	CImage::~CImage()
	{
		Close();
		m_dwValidSignature = 0;
	}

	BOOL CImage::Close()
	{
		if (m_pImportFiles) {
			delete m_pImportFiles;
			m_pImportFiles = NULL;
			m_nImportFiles = 0;
		}

		if (m_pImageData) {
			delete m_pImageData;
			m_pImageData = NULL;
		}

		if (m_pMap != NULL) {
			UnmapViewOfFile(m_pMap);
			m_pMap = NULL;
		}

		if (m_hMap) {
			CloseHandle(m_hMap);
			m_hMap = NULL;
		}

		if (m_pbOutputBuffer) {
			delete[] m_pbOutputBuffer;
			m_pbOutputBuffer = NULL;
			m_cbOutputBuffer = 0;
		}
		return TRUE;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	PBYTE CImage::DataEnum(GUID *pGuid, DWORD *pcbData, DWORD *pnIterator)
	{
		if (m_pImageData == NULL) {
			return NULL;
		}
		return m_pImageData->Enumerate(pGuid, pcbData, pnIterator);
	}

	PBYTE CImage::DataFind(REFGUID rguid, DWORD *pcbData)
	{
		if (m_pImageData == NULL) {
			return NULL;
		}
		return m_pImageData->Find(rguid, pcbData);
	}

	PBYTE CImage::DataSet(REFGUID rguid, PBYTE pbData, DWORD cbData)
	{
		if (m_pImageData == NULL) {
			return NULL;
		}
		return m_pImageData->Set(rguid, pbData, cbData);
	}

	BOOL CImage::DataDelete(REFGUID rguid)
	{
		if (m_pImageData == NULL) {
			return FALSE;
		}
		return m_pImageData->Delete(rguid);
	}

	BOOL CImage::DataPurge()
	{
		if (m_pImageData == NULL) {
			return TRUE;
		}
		return m_pImageData->Purge();
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	BOOL CImage::SizeOutputBuffer(DWORD cbData)
	{
		if (m_cbOutputBuffer < cbData) {
			if (cbData < 1024) {//65536
				cbData = 1024;
			}
			cbData = FileAlign(cbData);

			PBYTE pOutput = new BYTE[cbData];
			if (pOutput == NULL) {
				SetLastError(ERROR_OUTOFMEMORY);
				return FALSE;
			}

			if (m_pbOutputBuffer) {
				CopyMemory(pOutput, m_pbOutputBuffer, m_cbOutputBuffer);

				delete[] m_pbOutputBuffer;
				m_pbOutputBuffer = NULL;
			}

			ZeroMemory(pOutput + m_cbOutputBuffer, cbData - m_cbOutputBuffer),

				m_pbOutputBuffer = pOutput;
			m_cbOutputBuffer = cbData;
		}
		return TRUE;
	}

	PBYTE CImage::AllocateOutput(DWORD cbData, DWORD *pnVirtAddr)
	{
		cbData = QuadAlign(cbData);

		PBYTE pbData = m_pbOutputBuffer + m_nOutputVirtSize;

		*pnVirtAddr = m_nOutputVirtAddr + m_nOutputVirtSize;
		m_nOutputVirtSize += cbData;

		if (m_nOutputVirtSize > m_cbOutputBuffer) {
			SetLastError(ERROR_OUTOFMEMORY);
			return NULL;
		}

		ZeroMemory(pbData, cbData);

		return pbData;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	DWORD CImage::FileAlign(DWORD nAddr)
	{
		return Align(nAddr, m_NtHeader.OptionalHeader.FileAlignment);
	}

	DWORD CImage::SectionAlign(DWORD nAddr)
	{
		return Align(nAddr, m_NtHeader.OptionalHeader.SectionAlignment);
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	PVOID CImage::RvaToVa(ULONG_PTR nRva)
	{
		if (nRva == 0) {
			return NULL;
		}

		for (DWORD n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			DWORD vaStart = m_SectionHeaders[n].VirtualAddress;
			DWORD vaEnd = vaStart + m_SectionHeaders[n].SizeOfRawData;

			if (nRva >= vaStart && nRva < vaEnd) {
				return (PBYTE)m_pMap
					+ m_SectionHeaders[n].PointerToRawData
					+ nRva - m_SectionHeaders[n].VirtualAddress;
			}
		}
		return NULL;
	}

	DWORD CImage::RvaToFileOffset(DWORD nRva)
	{
		DWORD n;
		for (n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			DWORD vaStart = m_SectionHeaders[n].VirtualAddress;
			DWORD vaEnd = vaStart + m_SectionHeaders[n].SizeOfRawData;

			if (nRva >= vaStart && nRva < vaEnd) {
				return m_SectionHeaders[n].PointerToRawData
					+ nRva - m_SectionHeaders[n].VirtualAddress;
			}
		}
		return 0;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	BOOL CImage::WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
		LPDWORD lpNumberOfBytesWritten)
	{
		return ::WriteFile(hFile,
			lpBuffer,
			nNumberOfBytesToWrite,
			lpNumberOfBytesWritten,
			NULL);
	}


	BOOL CImage::CopyFileData(HANDLE hFile, DWORD nOldPos, DWORD cbData)
	{
		DWORD cbDone = 0;
		return WriteFile(hFile, m_pMap + nOldPos, cbData, &cbDone);
	}

	BOOL CImage::ZeroFileData(HANDLE hFile, DWORD cbData)
	{
		if (!SizeOutputBuffer(4096)) {
			return FALSE;
		}

		ZeroMemory(m_pbOutputBuffer, 4096);

		for (DWORD cbLeft = cbData; cbLeft > 0;) {
			DWORD cbStep = cbLeft > sizeof(m_pbOutputBuffer)
				? sizeof(m_pbOutputBuffer) : cbLeft;
			DWORD cbDone = 0;

			if (!WriteFile(hFile, m_pbOutputBuffer, cbStep, &cbDone)) {
				return FALSE;
			}
			if (cbDone == 0) {
				break;
			}

			cbLeft -= cbDone;
		}
		return TRUE;
	}

	BOOL CImage::AlignFileData(HANDLE hFile)
	{
		DWORD nLastFileAddr = m_nNextFileAddr;

		m_nNextFileAddr = FileAlign(m_nNextFileAddr);
		m_nNextVirtAddr = SectionAlign(m_nNextVirtAddr);

		if (hFile != INVALID_HANDLE_VALUE) {
			if (m_nNextFileAddr > nLastFileAddr) {
				if (SetFilePointer(hFile, nLastFileAddr, NULL, FILE_BEGIN) == ~0u) {
					return FALSE;
				}
				return ZeroFileData(hFile, m_nNextFileAddr - nLastFileAddr);
			}
		}
		return TRUE;
	}

	BOOL CImage::Read(HANDLE hFile)
	{
		DWORD n;
		PBYTE pbData = NULL;
		DWORD cbData = 0;

		if (hFile == INVALID_HANDLE_VALUE) {
			SetLastError(ERROR_INVALID_HANDLE);
			return FALSE;
		}

		///////////////////////////////////////////////////////// Create mapping.
		//
		m_nFileSize = GetFileSize(hFile, NULL);
		if (m_nFileSize == (DWORD)-1) {
			return FALSE;
		}

		m_hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
		if (m_hMap == NULL) {
			return FALSE;
		}

		m_pMap = (PBYTE)MapViewOfFileEx(m_hMap, FILE_MAP_READ, 0, 0, 0, NULL);
		if (m_pMap == NULL) {
			return FALSE;
		}

		////////////////////////////////////////////////////// Process DOS Header.
		//
		PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)m_pMap;
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return FALSE;
		}
		m_nPeOffset = pDosHeader->e_lfanew;
		m_nPrePE = 0;
		m_cbPrePE = QuadAlign(pDosHeader->e_lfanew);

		CopyMemory(&m_DosHeader, m_pMap + m_nPrePE, sizeof(m_DosHeader));

		/////////////////////////////////////////////////////// Process PE Header.
		//
		CopyMemory(&m_NtHeader, m_pMap + m_nPeOffset, sizeof(m_NtHeader));
		if (m_NtHeader.Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return FALSE;
		}
		if (m_NtHeader.FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}
		m_nSectionsOffset = m_nPeOffset
			+ sizeof(m_NtHeader.Signature)
			+ sizeof(m_NtHeader.FileHeader)
			+ m_NtHeader.FileHeader.SizeOfOptionalHeader;

		///////////////////////////////////////////////// Process Section Headers.
		//
		if (m_NtHeader.FileHeader.NumberOfSections > (sizeof(m_SectionHeaders) /
			sizeof(m_SectionHeaders[0]))) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}
		CopyMemory(&m_SectionHeaders,
			m_pMap + m_nSectionsOffset,
			sizeof(m_SectionHeaders[0]) * m_NtHeader.FileHeader.NumberOfSections);

		/////////////////////////////////////////////////// Parse .xhook Section.
		//
		DWORD rvaOriginalImageDirectory = 0;
		DWORD rvaXHookBeg = 0;
		DWORD rvaXHookEnd = 0;

		for (n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			if (strcmp((PCHAR)m_SectionHeaders[n].Name, ".xhook") == 0) {
				XHOOK_SECTION_HEADER dh;
				CopyMemory(&dh,
					m_pMap + m_SectionHeaders[n].PointerToRawData,
					sizeof(dh));

				rvaOriginalImageDirectory = dh.nOriginalImportVirtualAddress;
				if (dh.cbPrePE != 0) {
					m_nPrePE = m_SectionHeaders[n].PointerToRawData + sizeof(dh);
					m_cbPrePE = dh.cbPrePE;
				}
				rvaXHookBeg = m_SectionHeaders[n].VirtualAddress;
				rvaXHookEnd = rvaXHookBeg + m_SectionHeaders[n].SizeOfRawData;
			}
		}

		//////////////////////////////////////////////////////// Get Import Table.
		//
		DWORD rvaImageDirectory = m_NtHeader.OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
		PIMAGE_IMPORT_DESCRIPTOR iidp
			= (PIMAGE_IMPORT_DESCRIPTOR)RvaToVa(rvaImageDirectory);
		PIMAGE_IMPORT_DESCRIPTOR oidp
			= (PIMAGE_IMPORT_DESCRIPTOR)RvaToVa(rvaOriginalImageDirectory);

		if (oidp == NULL) {
			oidp = iidp;
		}
		if (iidp == NULL || oidp == NULL) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}

		DWORD nFiles = 0;
		for (; iidp[nFiles].OriginalFirstThunk != 0 || iidp[nFiles].FirstThunk != 0; nFiles++) {
		}

		CImageImportFile **ppLastFile = &m_pImportFiles;
		m_pImportFiles = NULL;

		for (n = 0; n < nFiles; n++, iidp++) {
			ULONG_PTR rvaName = iidp->Name;
			PCHAR pszName = (PCHAR)RvaToVa(rvaName);
			if (pszName == NULL) {
				SetLastError(ERROR_EXE_MARKED_INVALID);
				goto fail;
			}

			CImageImportFile *pImportFile = new CImageImportFile;
			if (pImportFile == NULL) {
				SetLastError(ERROR_OUTOFMEMORY);
				goto fail;
			}

			*ppLastFile = pImportFile;
			ppLastFile = &pImportFile->m_pNextFile;
			m_nImportFiles++;

			pImportFile->m_pszName = DuplicateString(pszName);
			if (pImportFile->m_pszName == NULL) {
				goto fail;
			}

			pImportFile->m_rvaOriginalFirstThunk = iidp->OriginalFirstThunk;
			pImportFile->m_rvaFirstThunk = iidp->FirstThunk;
			pImportFile->m_nForwarderChain = iidp->ForwarderChain;
			pImportFile->m_pImportNames = NULL;
			pImportFile->m_nImportNames = 0;
			pImportFile->m_fByway = FALSE;

			if ((ULONG)iidp->FirstThunk >= rvaXHookBeg &&
				(ULONG)iidp->FirstThunk < rvaXHookEnd) {

				pImportFile->m_pszOrig = NULL;
				pImportFile->m_fByway = TRUE;
				continue;
			}

			rvaName = oidp->Name;
			pszName = (PCHAR)RvaToVa(rvaName);
			if (pszName == NULL) {
				SetLastError(ERROR_EXE_MARKED_INVALID);
				goto fail;
			}
			pImportFile->m_pszOrig = DuplicateString(pszName);
			if (pImportFile->m_pszOrig == NULL) {
				goto fail;
			}

			DWORD rvaThunk = iidp->OriginalFirstThunk;
			if (!rvaThunk) {
				rvaThunk = iidp->FirstThunk;
			}
			PIMAGE_THUNK_DATA pAddrThunk = (PIMAGE_THUNK_DATA)RvaToVa(rvaThunk);
			rvaThunk = oidp->OriginalFirstThunk;
			if (!rvaThunk) {
				rvaThunk = oidp->FirstThunk;
			}
			PIMAGE_THUNK_DATA pLookThunk = (PIMAGE_THUNK_DATA)RvaToVa(rvaThunk);

			DWORD nNames = 0;
			if (pAddrThunk) {
				for (; pAddrThunk[nNames].u1.Ordinal; nNames++) {
				}
			}

			if (pAddrThunk && nNames) {
				pImportFile->m_nImportNames = nNames;
				pImportFile->m_pImportNames = new CImageImportName[nNames];
				if (pImportFile->m_pImportNames == NULL) {
					SetLastError(ERROR_OUTOFMEMORY);
					goto fail;
				}

				CImageImportName *pImportName = &pImportFile->m_pImportNames[0];

				for (DWORD f = 0; f < nNames; f++, pImportName++) {
					pImportName->m_nOrig = 0;
					pImportName->m_nOrdinal = 0;
					pImportName->m_nHint = 0;
					pImportName->m_pszName = NULL;
					pImportName->m_pszOrig = NULL;

					rvaName = pAddrThunk[f].u1.Ordinal;
					if (rvaName & IMAGE_ORDINAL_FLAG) {
						pImportName->m_nOrig = (ULONG)IMAGE_ORDINAL(rvaName);
						pImportName->m_nOrdinal = pImportName->m_nOrig;
					}
					else {
						PIMAGE_IMPORT_BY_NAME pName
							= (PIMAGE_IMPORT_BY_NAME)RvaToVa(rvaName);
						if (pName) {
							pImportName->m_nHint = pName->Hint;
							pImportName->m_pszName = DuplicateString((PCHAR)pName->Name);
							if (pImportName->m_pszName == NULL) {
								goto fail;
							}
						}

						rvaName = pLookThunk[f].u1.Ordinal;
						if (rvaName & IMAGE_ORDINAL_FLAG) {
							pImportName->m_nOrig = (ULONG)IMAGE_ORDINAL(rvaName);
							pImportName->m_nOrdinal = (ULONG)IMAGE_ORDINAL(rvaName);
						}
						else {
							pName = (PIMAGE_IMPORT_BY_NAME)RvaToVa(rvaName);
							if (pName) {
								pImportName->m_pszOrig
									= DuplicateString((PCHAR)pName->Name);
								if (pImportName->m_pszOrig == NULL) {
									goto fail;
								}
							}
						}
					}
				}
			}
			oidp++;
		}

		////////////////////////////////////////////////////////// Parse Sections.
		//
		m_nExtraOffset = 0;
		for (n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			m_nExtraOffset = Max(m_SectionHeaders[n].PointerToRawData +
				m_SectionHeaders[n].SizeOfRawData,
				m_nExtraOffset);

			if (strcmp((PCHAR)m_SectionHeaders[n].Name, ".xhook") == 0) {
				XHOOK_SECTION_HEADER dh;
				CopyMemory(&dh,
					m_pMap + m_SectionHeaders[n].PointerToRawData,
					sizeof(dh));

				if (dh.nDataOffset == 0) {
					dh.nDataOffset = dh.cbHeaderSize;
				}

				cbData = dh.cbDataSize - dh.nDataOffset;
				pbData = (m_pMap +
					m_SectionHeaders[n].PointerToRawData +
					dh.nDataOffset);

				m_nExtraOffset = Max(m_SectionHeaders[n].PointerToRawData +
					m_SectionHeaders[n].SizeOfRawData,
					m_nExtraOffset);

				m_NtHeader.FileHeader.NumberOfSections--;

				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress
					= dh.nOriginalImportVirtualAddress;
				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size
					= dh.nOriginalImportSize;

				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress
					= dh.nOriginalBoundImportVirtualAddress;
				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size
					= dh.nOriginalBoundImportSize;

				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress
					= dh.nOriginalIatVirtualAddress;
				m_NtHeader.OptionalHeader
					.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size
					= dh.nOriginalIatSize;

				m_NtHeader.OptionalHeader.CheckSum = 0;
				m_NtHeader.OptionalHeader.SizeOfImage
					= dh.nOriginalSizeOfImage;

				m_fHadXHookSection = TRUE;
			}
		}

		m_pImageData = new CImageData(pbData, cbData);
		if (m_pImageData == NULL) {
			SetLastError(ERROR_OUTOFMEMORY);
		}
		return TRUE;

	fail:
		return FALSE;
	}

	static inline BOOL strneq(__in_z PCHAR pszOne, __in_z PCHAR pszTwo)
	{
		if (pszOne == pszTwo) {
			return FALSE;
		}
		if (!pszOne || !pszTwo) {
			return TRUE;
		}
		return (strcmp(pszOne, pszTwo) != 0);
	}

	BOOL CImage::CheckImportsNeeded(DWORD *pnTables, DWORD *pnThunks, DWORD *pnChars)
	{
		DWORD nTables = 0;
		DWORD nThunks = 0;
		DWORD nChars = 0;
		BOOL fNeedXHookSection = FALSE;

		for (CImageImportFile *pImportFile = m_pImportFiles;
			pImportFile != NULL; pImportFile = pImportFile->m_pNextFile) {

			nChars += (int)strlen(pImportFile->m_pszName) + 1;
			nChars += nChars & 1;

			if (pImportFile->m_fByway) {
				fNeedXHookSection = TRUE;
				nThunks++;
			}
			else {
				if (!fNeedXHookSection &&
					strneq(pImportFile->m_pszName, pImportFile->m_pszOrig)) {

					fNeedXHookSection = TRUE;
				}
				for (DWORD n = 0; n < pImportFile->m_nImportNames; n++) {
					CImageImportName *pImportName = &pImportFile->m_pImportNames[n];

					if (!fNeedXHookSection &&
						strneq(pImportName->m_pszName, pImportName->m_pszOrig)) {

						fNeedXHookSection = TRUE;
					}

					if (pImportName->m_pszName) {
						nChars += sizeof(WORD);             // Hint
						nChars += (int)strlen(pImportName->m_pszName) + 1;
						nChars += nChars & 1;
					}
					nThunks++;
				}
			}
			nThunks++;
			nTables++;
		}
		nTables++;

		*pnTables = nTables;
		*pnThunks = nThunks;
		*pnChars = nChars;

		return fNeedXHookSection;
	}

	//////////////////////////////////////////////////////////////////////////////
	//
	CImageImportFile * CImage::NewByway(__in_z PCHAR pszName)
	{
		CImageImportFile *pImportFile = new CImageImportFile;
		if (pImportFile == NULL) {
			SetLastError(ERROR_OUTOFMEMORY);
			goto fail;
		}

		pImportFile->m_pNextFile = NULL;
		pImportFile->m_fByway = TRUE;

		pImportFile->m_pszName = DuplicateString(pszName);
		if (pImportFile->m_pszName == NULL) {
			goto fail;
		}

		pImportFile->m_rvaOriginalFirstThunk = 0;
		pImportFile->m_rvaFirstThunk = 0;
		pImportFile->m_nForwarderChain = (UINT)0;
		pImportFile->m_pImportNames = NULL;
		pImportFile->m_nImportNames = 0;

		m_nImportFiles++;
		return pImportFile;

	fail:
		if (pImportFile) {
			delete pImportFile;
			pImportFile = NULL;
		}
		return NULL;
	}

	BOOL CImage::EditImports(PVOID pContext,
		PF_XHOOK_BINARY_BYWAY_CALLBACK pfBywayCallback,
		PF_XHOOK_BINARY_FILE_CALLBACK pfFileCallback,
		PF_XHOOK_BINARY_SYMBOL_CALLBACK pfSymbolCallback,
		PF_XHOOK_BINARY_COMMIT_CALLBACK pfCommitCallback)
	{
		CImageImportFile *pImportFile = NULL;
		CImageImportFile **ppLastFile = &m_pImportFiles;

		SetLastError(ERROR_CALL_NOT_IMPLEMENTED);

		while ((pImportFile = *ppLastFile) != NULL) {

			if (pfBywayCallback) {
				PCHAR pszFile = NULL;
				if (!(*pfBywayCallback)(pContext, pszFile, &pszFile)) {
					goto fail;
				}

				if (pszFile) {
					// Insert a new Byway.
					CImageImportFile *pByway = NewByway(pszFile);
					if (pByway == NULL) {
						return FALSE;
					}

					pByway->m_pNextFile = pImportFile;
					*ppLastFile = pByway;
					ppLastFile = &pByway->m_pNextFile;
					continue;                               // Retry after Byway.
				}
			}

			if (pImportFile->m_fByway) {
				if (pfBywayCallback) {
					PCHAR pszFile = pImportFile->m_pszName;

					if (!(*pfBywayCallback)(pContext, pszFile, &pszFile)) {
						goto fail;
					}

					if (pszFile) {                          // Replace? Byway
						if (ReplaceString(&pImportFile->m_pszName, pszFile) == NULL) {
							goto fail;
						}
					}
					else {                                  // Delete Byway
						*ppLastFile = pImportFile->m_pNextFile;
						pImportFile->m_pNextFile = NULL;
						delete pImportFile;
						pImportFile = *ppLastFile;
						m_nImportFiles--;
						continue;                           // Retry after delete.
					}
				}
			}
			else {
				if (pfFileCallback) {
					PCHAR pszFile = pImportFile->m_pszName;

					if (!(*pfFileCallback)(pContext, pImportFile->m_pszOrig,
						pszFile, &pszFile)) {
						goto fail;
					}

					if (pszFile != NULL) {
						if (ReplaceString(&pImportFile->m_pszName, pszFile) == NULL) {
							goto fail;
						}
					}
				}

				if (pfSymbolCallback) {
					for (DWORD n = 0; n < pImportFile->m_nImportNames; n++) {
						CImageImportName *pImportName = &pImportFile->m_pImportNames[n];

						PCHAR pszName = pImportName->m_pszName;
						ULONG nOrdinal = pImportName->m_nOrdinal;
						if (!(*pfSymbolCallback)(pContext,
							pImportName->m_nOrig,
							nOrdinal,
							&nOrdinal,
							pImportName->m_pszOrig,
							pszName,
							&pszName)) {
							goto fail;
						}

						if (pszName != NULL) {
							pImportName->m_nOrdinal = 0;
							if (ReplaceString(&pImportName->m_pszName, pszName) == NULL) {
								goto fail;
							}
						}
						else if (nOrdinal != 0) {
							pImportName->m_nOrdinal = nOrdinal;

							if (pImportName->m_pszName != NULL) {
								delete[] pImportName->m_pszName;
								pImportName->m_pszName = NULL;
							}
						}
					}
				}
			}

			ppLastFile = &pImportFile->m_pNextFile;
			pImportFile = pImportFile->m_pNextFile;
		}

		for (;;) {
			if (pfBywayCallback) {
				PCHAR pszFile = NULL;
				if (!(*pfBywayCallback)(pContext, NULL, &pszFile)) {
					goto fail;
				}
				if (pszFile) {
					// Insert a new Byway.
					CImageImportFile *pByway = NewByway(pszFile);
					if (pByway == NULL) {
						return FALSE;
					}

					pByway->m_pNextFile = pImportFile;
					*ppLastFile = pByway;
					ppLastFile = &pByway->m_pNextFile;
					continue;                               // Retry after Byway.
				}
			}
			break;
		}

		if (pfCommitCallback) {
			if (!(*pfCommitCallback)(pContext)) {
				goto fail;
			}
		}

		SetLastError(NO_ERROR);
		return TRUE;

	fail:
		return FALSE;
	}

	BOOL CImage::Write(HANDLE hFile)
	{
		DWORD cbDone;

		if (hFile == INVALID_HANDLE_VALUE) {
			SetLastError(ERROR_INVALID_HANDLE);
			return FALSE;
		}

		m_nNextFileAddr = 0;
		m_nNextVirtAddr = 0;

		DWORD nTables = 0;
		DWORD nThunks = 0;
		DWORD nChars = 0;
		BOOL fNeedXHookSection = CheckImportsNeeded(&nTables, &nThunks, &nChars);

		//////////////////////////////////////////////////////////// Copy Headers.
		//
		if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == ~0u) {
			return FALSE;
		}
		if (!CopyFileData(hFile, 0, m_NtHeader.OptionalHeader.SizeOfHeaders)) {
			return FALSE;
		}

		if (fNeedXHookSection || !m_pImageData->IsEmpty()) {
			// Replace the file's DOS header with our own.
			m_nPeOffset = sizeof(m_DosHeader) + sizeof(s_rbDosCode);
			m_nSectionsOffset = m_nPeOffset
				+ sizeof(m_NtHeader.Signature)
				+ sizeof(m_NtHeader.FileHeader)
				+ m_NtHeader.FileHeader.SizeOfOptionalHeader;
			m_DosHeader.e_lfanew = m_nPeOffset;

			if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == ~0u) {
				return FALSE;
			}
			if (!WriteFile(hFile, &m_DosHeader, sizeof(m_DosHeader), &cbDone)) {
				return FALSE;
			}
			if (!WriteFile(hFile, &s_rbDosCode, sizeof(s_rbDosCode), &cbDone)) {
				return FALSE;
			}
		}
		else {
			// Restore the file's original DOS header.
			if (m_nPrePE != 0) {
				m_nPeOffset = m_cbPrePE;
				m_nSectionsOffset = m_nPeOffset
					+ sizeof(m_NtHeader.Signature)
					+ sizeof(m_NtHeader.FileHeader)
					+ m_NtHeader.FileHeader.SizeOfOptionalHeader;
				m_DosHeader.e_lfanew = m_nPeOffset;


				if (SetFilePointer(hFile, 0, NULL, FILE_BEGIN) == ~0u) {
					return FALSE;
				}
				if (!CopyFileData(hFile, m_nPrePE, m_cbPrePE)) {
					return FALSE;
				}
			}
		}

		m_nNextFileAddr = m_NtHeader.OptionalHeader.SizeOfHeaders;
		m_nNextVirtAddr = 0;
		if (!AlignFileData(hFile)) {
			return FALSE;
		}

		/////////////////////////////////////////////////////////// Copy Sections.
		//
		DWORD n = 0;
		for (; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			if (m_SectionHeaders[n].SizeOfRawData) {
				if (SetFilePointer(hFile,
					m_SectionHeaders[n].PointerToRawData,
					NULL, FILE_BEGIN) == ~0u) {
					return FALSE;
				}
				if (!CopyFileData(hFile,
					m_SectionHeaders[n].PointerToRawData,
					m_SectionHeaders[n].SizeOfRawData)) {
					return FALSE;
				}
			}
			m_nNextFileAddr = Max(m_SectionHeaders[n].PointerToRawData +
				m_SectionHeaders[n].SizeOfRawData,
				m_nNextFileAddr);
			m_nNextVirtAddr = Max(m_SectionHeaders[n].VirtualAddress +
				m_SectionHeaders[n].Misc.VirtualSize,
				m_nNextVirtAddr);
			m_nExtraOffset = Max(m_nNextFileAddr, m_nExtraOffset);

			if (!AlignFileData(hFile)) {
				return FALSE;
			}
		}

		if (fNeedXHookSection || !m_pImageData->IsEmpty()) {
			////////////////////////////////////////////// Insert .xhook Section.
			//
			DWORD nSection = m_NtHeader.FileHeader.NumberOfSections++;
			XHOOK_SECTION_HEADER dh;

			ZeroMemory(&dh, sizeof(dh));
			ZeroMemory(&m_SectionHeaders[nSection], sizeof(m_SectionHeaders[nSection]));

			dh.cbHeaderSize = sizeof(XHOOK_SECTION_HEADER);
			dh.nSignature = XHOOK_SECTION_HEADER_SIGNATURE;

			dh.nOriginalImportVirtualAddress = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
			dh.nOriginalImportSize = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size;

			dh.nOriginalBoundImportVirtualAddress
				= m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress;
			dh.nOriginalBoundImportSize = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size;

			dh.nOriginalIatVirtualAddress = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].VirtualAddress;
			dh.nOriginalIatSize = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT].Size;

			dh.nOriginalSizeOfImage = m_NtHeader.OptionalHeader.SizeOfImage;

			DWORD clrAddr = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress;
			DWORD clrSize = m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size;
			if (clrAddr && clrSize) {
				PXHOOK_CLR_HEADER pHdr = (PXHOOK_CLR_HEADER)RvaToVa(clrAddr);
				if (pHdr != NULL) {
					XHOOK_CLR_HEADER hdr;
					hdr = *pHdr;

					dh.nOriginalClrFlags = hdr.Flags;
				}
			}

			HRESULT hrRet = StringCchCopyA((PCHAR)m_SectionHeaders[nSection].Name, IMAGE_SIZEOF_SHORT_NAME, ".xhook");
			if (FAILED(hrRet))
				return FALSE;

			m_SectionHeaders[nSection].Characteristics
				= IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE;

			m_nOutputVirtAddr = m_nNextVirtAddr;
			m_nOutputVirtSize = 0;
			m_nOutputFileAddr = m_nNextFileAddr;

			dh.nDataOffset = 0;                     // pbData
			dh.cbDataSize = m_pImageData->m_cbData;
			dh.cbPrePE = m_cbPrePE;

			//////////////////////////////////////////////////////////////////////////
			//

			DWORD rvaImportTable = 0;
			DWORD rvaLookupTable = 0;
			DWORD rvaBoundTable = 0;
			DWORD rvaNameTable = 0;
			DWORD nImportTableSize = nTables * sizeof(IMAGE_IMPORT_DESCRIPTOR);

			if (!SizeOutputBuffer(QuadAlign(sizeof(dh))
				+ m_cbPrePE
				+ QuadAlign(m_pImageData->m_cbData)
				+ QuadAlign(sizeof(IMAGE_THUNK_DATA) * nThunks)
				+ QuadAlign(sizeof(IMAGE_THUNK_DATA) * nThunks)
				+ QuadAlign(nChars)
				+ QuadAlign(nImportTableSize))) {
				return FALSE;
			}

			DWORD vaHead = 0;
			PBYTE pbHead = NULL;
			DWORD vaPrePE = 0;
			PBYTE pbPrePE = NULL;
			DWORD vaData = 0;
			PBYTE pbData = NULL;

			if ((pbHead = AllocateOutput(sizeof(dh), &vaHead)) == NULL) {
				return FALSE;
			}

			if ((pbPrePE = AllocateOutput(m_cbPrePE, &vaPrePE)) == NULL) {
				return FALSE;
			}

			CImageThunks lookupTable(this, nThunks, &rvaLookupTable);
			CImageThunks boundTable(this, nThunks, &rvaBoundTable);
			CImageChars nameTable(this, nChars, &rvaNameTable);

			if ((pbData = AllocateOutput(m_pImageData->m_cbData, &vaData)) == NULL) {
				return FALSE;
			}

			dh.nDataOffset = vaData - vaHead;
			dh.cbDataSize = dh.nDataOffset + m_pImageData->m_cbData;
			CopyMemory(pbHead, &dh, sizeof(dh));
			CopyMemory(pbPrePE, m_pMap + m_nPrePE, m_cbPrePE);
			CopyMemory(pbData, m_pImageData->m_pbData, m_pImageData->m_cbData);

			PIMAGE_IMPORT_DESCRIPTOR piidDst = (PIMAGE_IMPORT_DESCRIPTOR)
				AllocateOutput(nImportTableSize, &rvaImportTable);
			if (piidDst == NULL) {
				return FALSE;
			}

			//////////////////////////////////////////////// Step Through Imports.
			//
			for (CImageImportFile *pImportFile = m_pImportFiles;
				pImportFile != NULL; pImportFile = pImportFile->m_pNextFile) {

				ZeroMemory(piidDst, sizeof(piidDst));
				nameTable.Allocate(pImportFile->m_pszName, (DWORD *)&piidDst->Name);
				piidDst->TimeDateStamp = 0;
				piidDst->ForwarderChain = pImportFile->m_nForwarderChain;

				if (pImportFile->m_fByway) {
					ULONG rvaIgnored;

					lookupTable.Allocate(IMAGE_ORDINAL_FLAG + 1,
						(DWORD *)&piidDst->OriginalFirstThunk);
					boundTable.Allocate(IMAGE_ORDINAL_FLAG + 1,
						(DWORD *)&piidDst->FirstThunk);

					lookupTable.Allocate(0, &rvaIgnored);
					boundTable.Allocate(0, &rvaIgnored);
				}
				else {
					ULONG rvaIgnored;

					piidDst->FirstThunk = (ULONG)pImportFile->m_rvaFirstThunk;
					lookupTable.Current((DWORD *)&piidDst->OriginalFirstThunk);

					for (n = 0; n < pImportFile->m_nImportNames; n++) {
						CImageImportName *pImportName = &pImportFile->m_pImportNames[n];

						if (pImportName->m_pszName) {
							ULONG nDstName = 0;

							nameTable.Allocate(pImportName->m_pszName,
								pImportName->m_nHint,
								&nDstName);
							lookupTable.Allocate(nDstName, &rvaIgnored);
						}
						else {
							lookupTable.Allocate(IMAGE_ORDINAL_FLAG + pImportName->m_nOrdinal,
								&rvaIgnored);
						}
					}
					lookupTable.Allocate(0, &rvaIgnored);
				}
				piidDst++;
			}
			ZeroMemory(piidDst, sizeof(piidDst));

			//////////////////////////////////////////////////////////////////////////
			//
			m_nNextVirtAddr += m_nOutputVirtSize;
			m_nNextFileAddr += FileAlign(m_nOutputVirtSize);

			if (!AlignFileData(hFile)) {
				return FALSE;
			}

			//////////////////////////////////////////////////////////////////////////
			//
			m_SectionHeaders[nSection].VirtualAddress = m_nOutputVirtAddr;
			m_SectionHeaders[nSection].Misc.VirtualSize = m_nOutputVirtSize;
			m_SectionHeaders[nSection].PointerToRawData = m_nOutputFileAddr;
			m_SectionHeaders[nSection].SizeOfRawData = FileAlign(m_nOutputVirtSize);

			m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress
				= rvaImportTable;
			m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size
				= nImportTableSize;

			m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].VirtualAddress = 0;
			m_NtHeader.OptionalHeader
				.DataDirectory[IMAGE_DIRECTORY_ENTRY_BOUND_IMPORT].Size = 0;

			//////////////////////////////////////////////////////////////////////////
			//
			if (SetFilePointer(hFile, m_SectionHeaders[nSection].PointerToRawData,
				NULL, FILE_BEGIN) == ~0u) {
				return FALSE;
			}
			if (!WriteFile(hFile, m_pbOutputBuffer, m_SectionHeaders[nSection].SizeOfRawData,
				&cbDone)) {
				return FALSE;
			}
		}

		///////////////////////////////////////////////////// Adjust Extra Data.
		//
		LONG nExtraAdjust = m_nNextFileAddr - m_nExtraOffset;
		for (n = 0; n < m_NtHeader.FileHeader.NumberOfSections; n++) {
			if (m_SectionHeaders[n].PointerToRawData > m_nExtraOffset) {
				m_SectionHeaders[n].PointerToRawData += nExtraAdjust;
			}
			if (m_SectionHeaders[n].PointerToRelocations > m_nExtraOffset) {
				m_SectionHeaders[n].PointerToRelocations += nExtraAdjust;
			}
			if (m_SectionHeaders[n].PointerToLinenumbers > m_nExtraOffset) {
				m_SectionHeaders[n].PointerToLinenumbers += nExtraAdjust;
			}
		}
		if (m_NtHeader.FileHeader.PointerToSymbolTable > m_nExtraOffset) {
			m_NtHeader.FileHeader.PointerToSymbolTable += nExtraAdjust;
		}

		m_NtHeader.OptionalHeader.CheckSum = 0;
		m_NtHeader.OptionalHeader.SizeOfImage = m_nNextVirtAddr;

		////////////////////////////////////////////////// Adjust Debug Directory.
		//
		DWORD debugAddr = m_NtHeader.OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].VirtualAddress;
		DWORD debugSize = m_NtHeader.OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG].Size;
		if (debugAddr && debugSize) {
			DWORD nFileOffset = RvaToFileOffset(debugAddr);
			if (SetFilePointer(hFile, nFileOffset, NULL, FILE_BEGIN) == ~0u) {
				return FALSE;
			}

			PIMAGE_DEBUG_DIRECTORY pDir = (PIMAGE_DEBUG_DIRECTORY)RvaToVa(debugAddr);
			if (pDir == NULL) {
				return FALSE;
			}

			DWORD nEntries = debugSize / sizeof(*pDir);
			for (n = 0; n < nEntries; n++) {
				IMAGE_DEBUG_DIRECTORY dir = pDir[n];

				if (dir.PointerToRawData > m_nExtraOffset) {
					dir.PointerToRawData += nExtraAdjust;
				}
				if (!WriteFile(hFile, &dir, sizeof(dir), &cbDone)) {
					return FALSE;
				}
			}
		}

		/////////////////////////////////////////////////////// Adjust CLR Header.
		//
		DWORD clrAddr = m_NtHeader.OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress;
		DWORD clrSize = m_NtHeader.OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].Size;
		if (clrAddr && clrSize && fNeedXHookSection) {
			DWORD nFileOffset = RvaToFileOffset(clrAddr);
			if (SetFilePointer(hFile, nFileOffset, NULL, FILE_BEGIN) == ~0u) {
				return FALSE;
			}

			PXHOOK_CLR_HEADER pHdr = (PXHOOK_CLR_HEADER)RvaToVa(clrAddr);
			if (pHdr == NULL) {
				return FALSE;
			}

			XHOOK_CLR_HEADER hdr;
			hdr = *pHdr;
			hdr.Flags &= 0xfffffffe;    // Clear the IL_ONLY flag.

			if (!WriteFile(hFile, &hdr, sizeof(hdr), &cbDone)) {
				return FALSE;
			}
		}

		///////////////////////////////////////////////// Copy Left-over Data.
		//
		if (m_nFileSize > m_nExtraOffset) {
			if (SetFilePointer(hFile, m_nNextFileAddr, NULL, FILE_BEGIN) == ~0u) {
				return FALSE;
			}
			if (!CopyFileData(hFile, m_nExtraOffset, m_nFileSize - m_nExtraOffset)) {
				return FALSE;
			}
		}


		//////////////////////////////////////////////////// Finalize Headers.
		//

		if (SetFilePointer(hFile, m_nPeOffset, NULL, FILE_BEGIN) == ~0u) {
			return FALSE;
		}
		if (!WriteFile(hFile, &m_NtHeader, sizeof(m_NtHeader), &cbDone)) {
			return FALSE;
		}

		if (SetFilePointer(hFile, m_nSectionsOffset, NULL, FILE_BEGIN) == ~0u) {
			return FALSE;
		}
		if (!WriteFile(hFile, &m_SectionHeaders,
			sizeof(m_SectionHeaders[0])
			* m_NtHeader.FileHeader.NumberOfSections,
			&cbDone)) {
			return FALSE;
		}

		m_cbPostPE = SetFilePointer(hFile, 0, NULL, FILE_CURRENT);
		if (m_cbPostPE == ~0u) {
			return FALSE;
		}
		m_cbPostPE = m_NtHeader.OptionalHeader.SizeOfHeaders - m_cbPostPE;

		return TRUE;
	}

};                                                      // namespace XHook


//////////////////////////////////////////////////////////////////////////////
//
PXHOOK_BINARY WINAPI XHookBinaryOpen(HANDLE hFile)
{
	XHook::CImage *pImage = new XHook::CImage;
	if (pImage == NULL) {
		SetLastError(ERROR_OUTOFMEMORY);
		return FALSE;
	}

	if (!pImage->Read(hFile)) {
		delete pImage;
		return FALSE;
	}

	return (PXHOOK_BINARY)pImage;
}

BOOL WINAPI XHookBinaryWrite(PXHOOK_BINARY pdi, HANDLE hFile)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->Write(hFile);
}

PVOID WINAPI XHookBinaryEnumeratePayloads(PXHOOK_BINARY pdi,
	GUID *pGuid,
	DWORD *pcbData,
	DWORD *pnIterator)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->DataEnum(pGuid, pcbData, pnIterator);
}

PVOID WINAPI XHookBinaryFindPayload(PXHOOK_BINARY pdi,
	REFGUID rguid,
	DWORD *pcbData)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->DataFind(rguid, pcbData);
}

PVOID WINAPI XHookBinarySetPayload(PXHOOK_BINARY pdi,
	REFGUID rguid,
	PVOID pvData,
	DWORD cbData)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->DataSet(rguid, (PBYTE)pvData, cbData);
}

BOOL WINAPI XHookBinaryDeletePayload(PXHOOK_BINARY pdi,
	REFGUID rguid)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->DataDelete(rguid);
}

BOOL WINAPI XHookBinaryPurgePayloads(PXHOOK_BINARY pdi)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->DataPurge();
}

//////////////////////////////////////////////////////////////////////////////
//
static BOOL CALLBACK ResetBywayCallback(PVOID pContext,
	__in_z PCHAR pszFile,
	__deref PCHAR *ppszOutFile)
{
	(void)pContext;
	(void)pszFile;

	*ppszOutFile = NULL;
	return TRUE;
}

static BOOL CALLBACK ResetFileCallback(PVOID pContext,
	__in_z PCHAR pszOrigFile,
	__in_z PCHAR pszFile,
	__deref PCHAR *ppszOutFile)
{
	(void)pContext;
	(void)pszFile;

	*ppszOutFile = pszOrigFile;
	return TRUE;
}

static BOOL CALLBACK ResetSymbolCallback(PVOID pContext,
	ULONG nOrigOrdinal,
	ULONG nOrdinal,
	ULONG *pnOutOrdinal,
	__in_z PCHAR pszOrigSymbol,
	__in_z PCHAR pszSymbol,
	__deref PCHAR *ppszOutSymbol)
{
	(void)pContext;
	(void)nOrdinal;
	(void)pszSymbol;

	*pnOutOrdinal = nOrigOrdinal;
	*ppszOutSymbol = pszOrigSymbol;
	return TRUE;
}

BOOL WINAPI XHookBinaryResetImports(PXHOOK_BINARY pdi)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->EditImports(NULL,
		ResetBywayCallback,
		ResetFileCallback,
		ResetSymbolCallback,
		NULL);
}

//////////////////////////////////////////////////////////////////////////////
//
BOOL WINAPI XHookBinaryEditImports(PXHOOK_BINARY pdi,
	PVOID pContext,
	PF_XHOOK_BINARY_BYWAY_CALLBACK pfBywayCallback,
	PF_XHOOK_BINARY_FILE_CALLBACK pfFileCallback,
	PF_XHOOK_BINARY_SYMBOL_CALLBACK pfSymbolCallback,
	PF_XHOOK_BINARY_COMMIT_CALLBACK pfCommitCallback)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	return pImage->EditImports(pContext,
		pfBywayCallback,
		pfFileCallback,
		pfSymbolCallback,
		pfCommitCallback);
}

BOOL WINAPI XHookBinaryClose(PXHOOK_BINARY pdi)
{
	XHook::CImage *pImage = XHook::CImage::IsValid(pdi);
	if (pImage == NULL) {
		return FALSE;
	}

	BOOL bSuccess = pImage->Close();
	delete pImage;
	pImage = NULL;

	return bSuccess;
}

//
///////////////////////////////////////////////////////////////// End of File.

#define CLR_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR]
#define IAT_DIRECTORY OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IAT]

//////////////////////////////////////////////////////////////////////////////
//
#ifndef _STRSAFE_H_INCLUDED_
static inline HRESULT StringCchLengthA(const char* psz, size_t cchMax, size_t* pcch)
{
	HRESULT hr = S_OK;
	size_t cchMaxPrev = cchMax;

	if (cchMax > 2147483647)
	{
		return ERROR_INVALID_PARAMETER;
	}

	while (cchMax && (*psz != '\0'))
	{
		psz++;
		cchMax--;
	}

	if (cchMax == 0)
	{
		// the string is longer than cchMax
		hr = ERROR_INVALID_PARAMETER;
	}

	if (SUCCEEDED(hr) && pcch)
	{
		*pcch = cchMaxPrev - cchMax;
	}

	return hr;
}


static inline HRESULT StringCchCopyA(char* pszDest, size_t cchDest, const char* pszSrc)
{
	HRESULT hr = S_OK;

	if (cchDest == 0)
	{
		// can not null terminate a zero-byte dest buffer
		hr = ERROR_INVALID_PARAMETER;
	}
	else
	{
		while (cchDest && (*pszSrc != '\0'))
		{
			*pszDest++ = *pszSrc++;
			cchDest--;
		}

		if (cchDest == 0)
		{
			// we are going to truncate pszDest
			pszDest--;
			hr = ERROR_INVALID_PARAMETER;
		}

		*pszDest = '\0';
	}

	return hr;
}

static inline HRESULT StringCchCatA(char* pszDest, size_t cchDest, const char* pszSrc)
{
	HRESULT hr;
	size_t cchDestCurrent;

	if (cchDest > 2147483647)
	{
		return ERROR_INVALID_PARAMETER;
	}

	hr = StringCchLengthA(pszDest, cchDest, &cchDestCurrent);

	if (SUCCEEDED(hr))
	{
		hr = StringCchCopyA(pszDest + cchDestCurrent,
			cchDest - cchDestCurrent,
			pszSrc);
	}

	return hr;
}

#endif

//////////////////////////////////////////////////////////////////////////////
//
const GUID XHOOK_EXE_RESTORE_GUID = {
	0x2ed7a3ff, 0x3339, 0x4a8d,
	{ 0x80, 0x5c, 0xd4, 0x98, 0x15, 0x3f, 0xc2, 0x8f } };

const GUID XHOOK_EXE_HELPER_GUID = { /* ea0251b9-5cde-41b5-98d0-2af4a26b0fee */
	0xea0251b9, 0x5cde, 0x41b5,
	{ 0x98, 0xd0, 0x2a, 0xf4, 0xa2, 0x6b, 0x0f, 0xee } };

//////////////////////////////////////////////////////////////////////////////
//
PXHOOK_SYM_INFO XHookLoadImageHlp(VOID)
{
	static XHOOK_SYM_INFO symInfo;
	static PXHOOK_SYM_INFO pSymInfo = NULL;
	static BOOL failed = false;

	if (failed) {
		return NULL;
	}
	if (pSymInfo != NULL) {
		return pSymInfo;
	}

	ZeroMemory(&symInfo, sizeof(symInfo));
	// Create a real handle to the process.
#if 0
	DuplicateHandle(GetCurrentProcess(),
		GetCurrentProcess(),
		GetCurrentProcess(),
		&symInfo.hProcess,
		0,
		FALSE,
		DUPLICATE_SAME_ACCESS);
#else
	symInfo.hProcess = GetCurrentProcess();
#endif

	symInfo.hDbgHelp = LoadLibraryExW(L"dbghelp.dll", NULL, 0);
	if (symInfo.hDbgHelp == NULL) {
	abort:
		failed = true;
		if (symInfo.hDbgHelp != NULL) {
			FreeLibrary(symInfo.hDbgHelp);
		}
		symInfo.pfImagehlpApiVersionEx = NULL;
		symInfo.pfSymInitialize = NULL;
		symInfo.pfSymSetOptions = NULL;
		symInfo.pfSymGetOptions = NULL;
		symInfo.pfSymLoadModule64 = NULL;
		symInfo.pfSymGetModuleInfo64 = NULL;
		symInfo.pfSymFromName = NULL;
		return NULL;
	}

	symInfo.pfImagehlpApiVersionEx
		= (PF_ImagehlpApiVersionEx)GetProcAddress(symInfo.hDbgHelp,
		"ImagehlpApiVersionEx");
	symInfo.pfSymInitialize
		= (PF_SymInitialize)GetProcAddress(symInfo.hDbgHelp, "SymInitialize");
	symInfo.pfSymSetOptions
		= (PF_SymSetOptions)GetProcAddress(symInfo.hDbgHelp, "SymSetOptions");
	symInfo.pfSymGetOptions
		= (PF_SymGetOptions)GetProcAddress(symInfo.hDbgHelp, "SymGetOptions");
	symInfo.pfSymLoadModule64
		= (PF_SymLoadModule64)GetProcAddress(symInfo.hDbgHelp, "SymLoadModule64");
	symInfo.pfSymGetModuleInfo64
		= (PF_SymGetModuleInfo64)GetProcAddress(symInfo.hDbgHelp, "SymGetModuleInfo64");
	symInfo.pfSymFromName
		= (PF_SymFromName)GetProcAddress(symInfo.hDbgHelp, "SymFromName");

	API_VERSION av;
	ZeroMemory(&av, sizeof(av));
	av.MajorVersion = API_VERSION_NUMBER;

	if (symInfo.pfImagehlpApiVersionEx == NULL ||
		symInfo.pfSymInitialize == NULL ||
		symInfo.pfSymLoadModule64 == NULL ||
		symInfo.pfSymGetModuleInfo64 == NULL ||
		symInfo.pfSymFromName == NULL) {
		goto abort;
	}

	symInfo.pfImagehlpApiVersionEx(&av);
	if (av.MajorVersion < API_VERSION_NUMBER) {
		goto abort;
	}

	if (!symInfo.pfSymInitialize(symInfo.hProcess, NULL, FALSE)) {
		// We won't retry the initialize if it fails.
		goto abort;
	}

	if (symInfo.pfSymGetOptions != NULL && symInfo.pfSymSetOptions != NULL) {
		DWORD dw = symInfo.pfSymGetOptions();

		dw &= ~(SYMOPT_CASE_INSENSITIVE |
			SYMOPT_UNDNAME |
			SYMOPT_DEFERRED_LOADS |
			0);
		dw |= (
#if defined(SYMOPT_EXACT_SYMBOLS)
			SYMOPT_EXACT_SYMBOLS |
#endif
#if defined(SYMOPT_NO_UNQUALIFIED_LOADS)
			SYMOPT_NO_UNQUALIFIED_LOADS |
#endif
			SYMOPT_DEFERRED_LOADS |
#if defined(SYMOPT_FAIL_CRITICAL_ERRORS)
			SYMOPT_FAIL_CRITICAL_ERRORS |
#endif
#if defined(SYMOPT_INCLUDE_32BIT_MODULES)
			SYMOPT_INCLUDE_32BIT_MODULES |
#endif
			0);
		symInfo.pfSymSetOptions(dw);
	}

	pSymInfo = &symInfo;
	return pSymInfo;
}

PVOID WINAPI XHookFindFunction(PCSTR pszModule, PCSTR pszFunction)
{
	/////////////////////////////////////////////// First, try GetProcAddress.
	//
	HMODULE hModule = LoadLibraryExA(pszModule, NULL, 0);
	if (hModule == NULL) {
		return NULL;
	}

	PBYTE pbCode = (PBYTE)GetProcAddress(hModule, pszFunction);
	if (pbCode) {
		return pbCode;
	}

	////////////////////////////////////////////////////// Then try ImageHelp.
	//
	XHOOK_TRACE(("XHookFindFunction(%s, %s)\n", pszModule, pszFunction));
	PXHOOK_SYM_INFO pSymInfo = XHookLoadImageHlp();
	if (pSymInfo == NULL) {
		XHOOK_TRACE(("XHookLoadImageHlp failed: %d\n",
			GetLastError()));
		return NULL;
	}

	if (pSymInfo->pfSymLoadModule64(pSymInfo->hProcess, NULL,
		(PCHAR)pszModule, NULL,
		(DWORD64)hModule, 0) == 0) {
		if (ERROR_SUCCESS != GetLastError()) {
			XHOOK_TRACE(("SymLoadModule64(%p) failed: %d\n",
				pSymInfo->hProcess, GetLastError()));
			return NULL;
		}
	}

	HRESULT hrRet;
	CHAR szFullName[512];
	IMAGEHLP_MODULE64 modinfo;
	ZeroMemory(&modinfo, sizeof(modinfo));
	modinfo.SizeOfStruct = sizeof(modinfo);
	if (!pSymInfo->pfSymGetModuleInfo64(pSymInfo->hProcess, (DWORD64)hModule, &modinfo)) {
		XHOOK_TRACE(("SymGetModuleInfo64(%p, %p) failed: %d\n",
			pSymInfo->hProcess, hModule, GetLastError()));
		return NULL;
	}

	hrRet = StringCchCopyA(szFullName, sizeof(szFullName) / sizeof(CHAR), modinfo.ModuleName);
	if (FAILED(hrRet)) {
		XHOOK_TRACE(("StringCchCopyA failed: %08x\n", hrRet));
		return NULL;
	}
	hrRet = StringCchCatA(szFullName, sizeof(szFullName) / sizeof(CHAR), "!");
	if (FAILED(hrRet)) {
		XHOOK_TRACE(("StringCchCatA failed: %08x\n", hrRet));
		return NULL;
	}
	hrRet = StringCchCatA(szFullName, sizeof(szFullName) / sizeof(CHAR), pszFunction);
	if (FAILED(hrRet)) {
		XHOOK_TRACE(("StringCchCatA failed: %08x\n", hrRet));
		return NULL;
	}

	struct CFullSymbol : SYMBOL_INFO {
		CHAR szRestOfName[512];
	} symbol;
	ZeroMemory(&symbol, sizeof(symbol));
	//symbol.ModBase = (ULONG64)hModule;
	symbol.SizeOfStruct = sizeof(SYMBOL_INFO);
#ifdef DBHLPAPI
	symbol.MaxNameLen = sizeof(symbol.szRestOfName) / sizeof(symbol.szRestOfName[0]);
#else
	symbol.MaxNameLength = sizeof(symbol.szRestOfName) / sizeof(symbol.szRestOfName[0]);
#endif

	if (!pSymInfo->pfSymFromName(pSymInfo->hProcess, szFullName, &symbol)) {
		XHOOK_TRACE(("SymFromName(%s) failed: %d\n", szFullName, GetLastError()));
		return NULL;
	}

#if defined(XHOOKS_IA64)
	// On the IA64, we get a raw code pointer from the symbol engine
	// and have to convert it to a wrapped [code pointer, global pointer].
	//
	PPLABEL_DESCRIPTOR pldEntry = (PPLABEL_DESCRIPTOR)XHookGetEntryPoint(hModule);
	PPLABEL_DESCRIPTOR pldSymbol = new PLABEL_DESCRIPTOR;

	pldSymbol->EntryPoint = symbol.Address;
	pldSymbol->GlobalPointer = pldEntry->GlobalPointer;
	return (PBYTE)pldSymbol;
#elif defined(XHOOKS_ARM)
	// On the ARM, we get a raw code pointer, which we must convert into a
	// valied Thumb2 function pointer.
	return XHOOKS_PBYTE_TO_PFUNC(symbol.Address);
#else
	return (PBYTE)symbol.Address;
#endif
}

//////////////////////////////////////////////////// Module Image Functions.
//
HMODULE WINAPI XHookEnumerateModules(HMODULE hModuleLast)
{
	PBYTE pbLast;

	if (hModuleLast == NULL) {
		pbLast = (PBYTE)0x10000;
	}
	else {
		pbLast = (PBYTE)hModuleLast + 0x10000;
	}

	MEMORY_BASIC_INFORMATION mbi;
	ZeroMemory(&mbi, sizeof(mbi));

	// Find the next memory region that contains a mapped PE image.
	//
	for (;; pbLast = (PBYTE)mbi.BaseAddress + mbi.RegionSize) {
		if (VirtualQuery((PVOID)pbLast, &mbi, sizeof(mbi)) <= 0) {
			break;
		}

		// Skip uncommitted regions and guard pages.
		//
		if ((mbi.State != MEM_COMMIT) ||
			((mbi.Protect & 0xff) == PAGE_NOACCESS) ||
			(mbi.Protect & PAGE_GUARD)) {
			continue;
		}

		__try {
			PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)pbLast;
			if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE ||
				(DWORD)pDosHeader->e_lfanew > mbi.RegionSize ||
				(DWORD)pDosHeader->e_lfanew < sizeof(*pDosHeader)) {
				continue;
			}

			PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
				pDosHeader->e_lfanew);
			if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
				continue;
			}

			return (HMODULE)pDosHeader;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			continue;
		}
	}
	return NULL;
}

PVOID WINAPI XHookGetEntryPoint(HMODULE hModule)
{
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	if (hModule == NULL) {
		pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	}

	__try {
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return NULL;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return NULL;
		}

		PXHOOK_CLR_HEADER pClrHeader = NULL;
		if (pNtHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
			if (((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.VirtualAddress != 0 &&
				((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.Size != 0) {
				pClrHeader = (PXHOOK_CLR_HEADER)
					(((PBYTE)pDosHeader)
					+ ((PIMAGE_NT_HEADERS32)pNtHeader)->CLR_DIRECTORY.VirtualAddress);
			}
		}
		else if (pNtHeader->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
			if (((PIMAGE_NT_HEADERS64)pNtHeader)->CLR_DIRECTORY.VirtualAddress != 0 &&
				((PIMAGE_NT_HEADERS64)pNtHeader)->CLR_DIRECTORY.Size != 0) {
				pClrHeader = (PXHOOK_CLR_HEADER)
					(((PBYTE)pDosHeader)
					+ ((PIMAGE_NT_HEADERS64)pNtHeader)->CLR_DIRECTORY.VirtualAddress);
			}
		}

		if (pClrHeader != NULL) {
			// For MSIL assemblies, we want to use the _Cor entry points.

			HMODULE hClr = GetModuleHandleW(L"MSCOREE.DLL");
			if (hClr == NULL) {
				return NULL;
			}

			SetLastError(NO_ERROR);
			return GetProcAddress(hClr, "_CorExeMain");
		}

		SetLastError(NO_ERROR);
		return ((PBYTE)pDosHeader) +
			pNtHeader->OptionalHeader.AddressOfEntryPoint;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return NULL;
	}
}

ULONG WINAPI XHookGetModuleSize(HMODULE hModule)
{
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	if (hModule == NULL) {
		pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	}

	__try {
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return NULL;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return NULL;
		}
		SetLastError(NO_ERROR);

		return (pNtHeader->OptionalHeader.SizeOfImage);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return NULL;
	}
}

HMODULE WINAPI XHookGetContainingModule(PVOID pvAddr)
{
	MEMORY_BASIC_INFORMATION mbi;
	ZeroMemory(&mbi, sizeof(mbi));

	__try {
		if (VirtualQuery(pvAddr, &mbi, sizeof(mbi)) <= 0) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		// Skip uncommitted regions and guard pages.
		//
		if ((mbi.State != MEM_COMMIT) ||
			((mbi.Protect & 0xff) == PAGE_NOACCESS) ||
			(mbi.Protect & PAGE_GUARD)) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)mbi.AllocationBase;
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return NULL;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return NULL;
		}
		SetLastError(NO_ERROR);

		return (HMODULE)pDosHeader;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_INVALID_EXE_SIGNATURE);
		return NULL;
	}
}


static inline PBYTE RvaAdjust(PIMAGE_DOS_HEADER pDosHeader, DWORD raddr)
{
	if (raddr != NULL) {
		return ((PBYTE)pDosHeader) + raddr;
	}
	return NULL;
}

BOOL WINAPI XHookEnumerateExports(HMODULE hModule,
	PVOID pContext,
	PF_XHOOK_ENUMERATE_EXPORT_CALLBACK pfExport)
{
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	if (hModule == NULL) {
		pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	}

	__try {
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return FALSE;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}

		PIMAGE_EXPORT_DIRECTORY pExportDir
			= (PIMAGE_EXPORT_DIRECTORY)
			RvaAdjust(pDosHeader,
			pNtHeader->OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress);

		if (pExportDir == NULL) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}

		PDWORD pdwFunctions = (PDWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfFunctions);
		PDWORD pdwNames = (PDWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfNames);
		PWORD pwOrdinals = (PWORD)RvaAdjust(pDosHeader, pExportDir->AddressOfNameOrdinals);

		for (DWORD nFunc = 0; nFunc < pExportDir->NumberOfFunctions; nFunc++) {
			PBYTE pbCode = (pdwFunctions != NULL)
				? (PBYTE)RvaAdjust(pDosHeader, pdwFunctions[nFunc]) : NULL;
			PCHAR pszName = NULL;
			for (DWORD n = 0; n < pExportDir->NumberOfNames; n++) {
				if (pwOrdinals[n] == nFunc) {
					pszName = (pdwNames != NULL)
						? (PCHAR)RvaAdjust(pDosHeader, pdwNames[n]) : NULL;
					break;
				}
			}
			ULONG nOrdinal = pExportDir->Base + nFunc;

			if (!pfExport(pContext, nOrdinal, pszName, pbCode)) {
				break;
			}
		}
		SetLastError(NO_ERROR);
		return TRUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return NULL;
	}
}

BOOL WINAPI XHookEnumerateImports(HMODULE hModule,
	PVOID pContext,
	PF_XHOOK_IMPORT_FILE_CALLBACK pfImportFile,
	PF_XHOOK_IMPORT_FUNC_CALLBACK pfImportFunc)
{
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	if (hModule == NULL) {
		pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	}

	__try {
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return FALSE;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return FALSE;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}

		PIMAGE_IMPORT_DESCRIPTOR iidp
			= (PIMAGE_IMPORT_DESCRIPTOR)
			RvaAdjust(pDosHeader,
			pNtHeader->OptionalHeader
			.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

		if (iidp == NULL) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return FALSE;
		}

		for (; iidp->OriginalFirstThunk != 0; iidp++) {

			PCSTR pszName = (PCHAR)RvaAdjust(pDosHeader, iidp->Name);
			if (pszName == NULL) {
				SetLastError(ERROR_EXE_MARKED_INVALID);
				return FALSE;
			}

			PIMAGE_THUNK_DATA pThunks = (PIMAGE_THUNK_DATA)
				RvaAdjust(pDosHeader, iidp->OriginalFirstThunk);
			PVOID * pAddrs = (PVOID *)
				RvaAdjust(pDosHeader, iidp->FirstThunk);

			HMODULE hFile = XHookGetContainingModule(pAddrs[0]);

			if (pfImportFile != NULL) {
				if (!pfImportFile(pContext, hFile, pszName)) {
					break;
				}
			}

			DWORD nNames = 0;
			if (pThunks) {
				for (; pThunks[nNames].u1.Ordinal; nNames++) {
					DWORD nOrdinal = 0;
					PCSTR pszFunc = NULL;

					if (IMAGE_SNAP_BY_ORDINAL(pThunks[nNames].u1.Ordinal)) {
						nOrdinal = (DWORD)IMAGE_ORDINAL(pThunks[nNames].u1.Ordinal);
					}
					else {
						pszFunc = (PCSTR)RvaAdjust(pDosHeader,
							(DWORD)pThunks[nNames].u1.AddressOfData + 2);
					}

					if (pfImportFunc != NULL) {
						if (!pfImportFunc(pContext,
							nOrdinal,
							pszFunc,
							pAddrs[nNames])) {
							break;
						}
					}
				}
				if (pfImportFunc != NULL) {
					pfImportFunc(pContext, 0, NULL, NULL);
				}
			}
		}
		if (pfImportFile != NULL) {
			pfImportFile(pContext, NULL, NULL);
		}
		SetLastError(NO_ERROR);
		return TRUE;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return FALSE;
	}
}

static PXHOOK_LOADED_BINARY WINAPI GetPayloadSectionFromModule(HMODULE hModule)
{
	PIMAGE_DOS_HEADER pDosHeader = (PIMAGE_DOS_HEADER)hModule;
	if (hModule == NULL) {
		pDosHeader = (PIMAGE_DOS_HEADER)GetModuleHandleW(NULL);
	}

	__try {
		if (pDosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
			SetLastError(ERROR_BAD_EXE_FORMAT);
			return NULL;
		}

		PIMAGE_NT_HEADERS pNtHeader = (PIMAGE_NT_HEADERS)((PBYTE)pDosHeader +
			pDosHeader->e_lfanew);
		if (pNtHeader->Signature != IMAGE_NT_SIGNATURE) {
			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return NULL;
		}
		if (pNtHeader->FileHeader.SizeOfOptionalHeader == 0) {
			SetLastError(ERROR_EXE_MARKED_INVALID);
			return NULL;
		}

		PIMAGE_SECTION_HEADER pSectionHeaders
			= (PIMAGE_SECTION_HEADER)((PBYTE)pNtHeader
			+ sizeof(pNtHeader->Signature)
			+ sizeof(pNtHeader->FileHeader)
			+ pNtHeader->FileHeader.SizeOfOptionalHeader);

		for (DWORD n = 0; n < pNtHeader->FileHeader.NumberOfSections; n++) {
			if (strcmp((PCHAR)pSectionHeaders[n].Name, ".xhook") == 0) {
				if (pSectionHeaders[n].VirtualAddress == 0 ||
					pSectionHeaders[n].SizeOfRawData == 0) {

					break;
				}

				PBYTE pbData = (PBYTE)pDosHeader + pSectionHeaders[n].VirtualAddress;
				XHOOK_SECTION_HEADER *pHeader = (XHOOK_SECTION_HEADER *)pbData;
				if (pHeader->cbHeaderSize < sizeof(XHOOK_SECTION_HEADER) ||
					pHeader->nSignature != XHOOK_SECTION_HEADER_SIGNATURE) {

					break;
				}

				if (pHeader->nDataOffset == 0) {
					pHeader->nDataOffset = pHeader->cbHeaderSize;
				}
				SetLastError(NO_ERROR);
				return (PBYTE)pHeader;
			}
		}
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return NULL;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_EXE_MARKED_INVALID);
		return NULL;
	}
}

DWORD WINAPI XHookGetSizeOfPayloads(HMODULE hModule)
{
	PXHOOK_LOADED_BINARY pBinary = GetPayloadSectionFromModule(hModule);
	if (pBinary == NULL) {
		// Error set by GetPayloadSectionFromModule.
		return 0;
	}

	__try {
		XHOOK_SECTION_HEADER *pHeader = (XHOOK_SECTION_HEADER *)pBinary;
		if (pHeader->cbHeaderSize < sizeof(XHOOK_SECTION_HEADER) ||
			pHeader->nSignature != XHOOK_SECTION_HEADER_SIGNATURE) {

			SetLastError(ERROR_INVALID_HANDLE);
			return 0;
		}
		SetLastError(NO_ERROR);
		return pHeader->cbDataSize;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_INVALID_HANDLE);
		return 0;
	}
}

PVOID WINAPI XHookFindPayload(HMODULE hModule, REFGUID rguid, DWORD * pcbData)
{
	PBYTE pbData = NULL;
	if (pcbData) {
		*pcbData = 0;
	}

	PXHOOK_LOADED_BINARY pBinary = GetPayloadSectionFromModule(hModule);
	if (pBinary == NULL) {
		// Error set by GetPayloadSectionFromModule.
		return NULL;
	}

	__try {
		XHOOK_SECTION_HEADER *pHeader = (XHOOK_SECTION_HEADER *)pBinary;
		if (pHeader->cbHeaderSize < sizeof(XHOOK_SECTION_HEADER) ||
			pHeader->nSignature != XHOOK_SECTION_HEADER_SIGNATURE) {

			SetLastError(ERROR_INVALID_EXE_SIGNATURE);
			return NULL;
		}

		PBYTE pbBeg = ((PBYTE)pHeader) + pHeader->nDataOffset;
		PBYTE pbEnd = ((PBYTE)pHeader) + pHeader->cbDataSize;

		for (pbData = pbBeg; pbData < pbEnd;) {
			XHOOK_SECTION_RECORD *pSection = (XHOOK_SECTION_RECORD *)pbData;

			if (pSection->guid.Data1 == rguid.Data1 &&
				pSection->guid.Data2 == rguid.Data2 &&
				pSection->guid.Data3 == rguid.Data3 &&
				pSection->guid.Data4[0] == rguid.Data4[0] &&
				pSection->guid.Data4[1] == rguid.Data4[1] &&
				pSection->guid.Data4[2] == rguid.Data4[2] &&
				pSection->guid.Data4[3] == rguid.Data4[3] &&
				pSection->guid.Data4[4] == rguid.Data4[4] &&
				pSection->guid.Data4[5] == rguid.Data4[5] &&
				pSection->guid.Data4[6] == rguid.Data4[6] &&
				pSection->guid.Data4[7] == rguid.Data4[7]) {

				if (pcbData) {
					*pcbData = pSection->cbBytes - sizeof(*pSection);
					SetLastError(NO_ERROR);
					return (PBYTE)(pSection + 1);
				}
			}

			pbData = (PBYTE)pSection + pSection->cbBytes;
		}
		SetLastError(ERROR_INVALID_HANDLE);
		return NULL;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		SetLastError(ERROR_INVALID_HANDLE);
		return NULL;
	}
}

PVOID WINAPI XHookFindPayloadEx(REFGUID rguid, DWORD * pcbData)
{
	for (HMODULE hMod = NULL; (hMod = XHookEnumerateModules(hMod)) != NULL;) {
		PVOID pvData;

		pvData = XHookFindPayload(hMod, rguid, pcbData);
		if (pvData != NULL) {
			return pvData;
		}
	}
	SetLastError(ERROR_MOD_NOT_FOUND);
	return NULL;
}

BOOL WINAPI XHookRestoreAfterWithEx(PVOID pvData, DWORD cbData)
{
	PXHOOK_EXE_RESTORE pder = (PXHOOK_EXE_RESTORE)pvData;

	if (pder->cb != sizeof(*pder) || pder->cb > cbData) {
		SetLastError(ERROR_BAD_EXE_FORMAT);
		return FALSE;
	}

	DWORD dwPermIdh = ~0u;
	DWORD dwPermInh = ~0u;
	DWORD dwPermClr = ~0u;
	DWORD dwIgnore;
	BOOL fSucceeded = FALSE;

#if 0
	if (pder->pclr != NULL && pder->clr.Flags != ((PXHOOK_CLR_HEADER)pder->pclr)->Flags) {
		// If we had to promote the 32/64-bit agnostic IL to 64-bit, we don't want
		// to restore its IAT.
		__debugbreak();
		return TRUE;
	}
#endif

	if (VirtualProtect(pder->pidh, pder->cbidh,
		PAGE_EXECUTE_READWRITE, &dwPermIdh)) {
		if (VirtualProtect(pder->pinh, pder->cbinh,
			PAGE_EXECUTE_READWRITE, &dwPermInh)) {

			CopyMemory(pder->pidh, &pder->idh, pder->cbidh);
			CopyMemory(pder->pinh, &pder->inh, pder->cbinh);

			if (pder->pclr != NULL) {
				if (VirtualProtect(pder->pclr, pder->cbclr,
					PAGE_EXECUTE_READWRITE, &dwPermClr)) {
					CopyMemory(pder->pclr, &pder->clr, pder->cbclr);
					VirtualProtect(pder->pclr, pder->cbclr, dwPermClr, &dwIgnore);
					fSucceeded = TRUE;
				}
			}
			else {
				fSucceeded = TRUE;
			}
			VirtualProtect(pder->pinh, pder->cbinh, dwPermInh, &dwIgnore);
		}
		VirtualProtect(pder->pidh, pder->cbidh, dwPermIdh, &dwIgnore);
	}
	return fSucceeded;
}

BOOL WINAPI XHookRestoreAfterWith()
{
	PVOID pvData;
	DWORD cbData;

	pvData = XHookFindPayloadEx(XHOOK_EXE_RESTORE_GUID, &cbData);

	if (pvData != NULL && cbData != 0) {
		return XHookRestoreAfterWithEx(pvData, cbData);
	}
	SetLastError(ERROR_MOD_NOT_FOUND);
	return FALSE;
}

//  End of File