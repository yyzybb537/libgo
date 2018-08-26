#pragma once

#ifdef _WIN64
#define XHOOKS_64BIT 1
#define XHOOKS_X64 1
#else
#define XHOOKS_32BIT 1
#define XHOOKS_X86 1
#endif

#define XHOOKS_VERSION     10000   // 1.00.00

//////////////////////////////////////////////////////////////////////////////
//

#if (_MSC_VER < 1299)
typedef LONG LONG_PTR;
typedef ULONG ULONG_PTR;
#endif

#ifndef __in_z
#define __in_z
#endif

//////////////////////////////////////////////////////////////////////////////
//
#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct  _GUID
{
	DWORD Data1;
	WORD Data2;
	WORD Data3;
	BYTE Data4[8];
} GUID;

#ifdef INITGUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
        const GUID name \
                = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }
#else
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    const GUID name
#endif // INITGUID
#endif // !GUID_DEFINED

#if defined(__cplusplus)
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID &
#endif // !_REFGUID_DEFINED
#else // !__cplusplus
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
#define REFGUID             const GUID * const
#endif // !_REFGUID_DEFINED
#endif // !__cplusplus

//
//////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

	/////////////////////////////////////////////////// Instruction Target Macros.
	//
#define XHOOK_INSTRUCTION_TARGET_NONE          ((PVOID)0)
#define XHOOK_INSTRUCTION_TARGET_DYNAMIC       ((PVOID)(LONG_PTR)-1)
#define XHOOK_SECTION_HEADER_SIGNATURE         0x00727444   // "Dtr\0"

	extern const GUID XHOOK_EXE_RESTORE_GUID;
	extern const GUID XHOOK_EXE_HELPER_GUID;

#define XHOOK_TRAMPOLINE_SIGNATURE             0x21727444  // Dtr!
	typedef struct _XHOOK_TRAMPOLINE XHOOK_TRAMPOLINE, *PXHOOK_TRAMPOLINE;

	/////////////////////////////////////////////////////////// Binary Structures.
	//
#pragma pack(push, 8)
	typedef struct _XHOOK_SECTION_HEADER
	{
		DWORD       cbHeaderSize;
		DWORD       nSignature;
		DWORD       nDataOffset;
		DWORD       cbDataSize;

		DWORD       nOriginalImportVirtualAddress;
		DWORD       nOriginalImportSize;
		DWORD       nOriginalBoundImportVirtualAddress;
		DWORD       nOriginalBoundImportSize;

		DWORD       nOriginalIatVirtualAddress;
		DWORD       nOriginalIatSize;
		DWORD       nOriginalSizeOfImage;
		DWORD       cbPrePE;

		DWORD       nOriginalClrFlags;
		DWORD       reserved1;
		DWORD       reserved2;
		DWORD       reserved3;

		// Followed by cbPrePE bytes of data.
	} XHOOK_SECTION_HEADER, *PXHOOK_SECTION_HEADER;

	typedef struct _XHOOK_SECTION_RECORD
	{
		DWORD       cbBytes;
		DWORD       nReserved;
		GUID        guid;
	} XHOOK_SECTION_RECORD, *PXHOOK_SECTION_RECORD;

	typedef struct _XHOOK_CLR_HEADER
	{
		// Header versioning
		ULONG                   cb;
		USHORT                  MajorRuntimeVersion;
		USHORT                  MinorRuntimeVersion;

		// Symbol table and startup information
		IMAGE_DATA_DIRECTORY    MetaData;
		ULONG                   Flags;

		// Followed by the rest of the IMAGE_COR20_HEADER
	} XHOOK_CLR_HEADER, *PXHOOK_CLR_HEADER;

	typedef struct _XHOOK_EXE_RESTORE
	{
		DWORD               cb;
		DWORD               cbidh;
		DWORD               cbinh;
		DWORD               cbclr;

		PBYTE               pidh;
		PBYTE               pinh;
		PBYTE               pclr;

		IMAGE_DOS_HEADER    idh;
		union {
			IMAGE_NT_HEADERS    inh;
			IMAGE_NT_HEADERS32  inh32;
			IMAGE_NT_HEADERS64  inh64;
			BYTE                raw[sizeof(IMAGE_NT_HEADERS64) +
				sizeof(IMAGE_SECTION_HEADER) * 32];
		};
		XHOOK_CLR_HEADER   clr;

	} XHOOK_EXE_RESTORE, *PXHOOK_EXE_RESTORE;

	typedef struct _XHOOK_EXE_HELPER
	{
		DWORD               cb;
		DWORD               pid;
		CHAR                DllName[MAX_PATH];

	} XHOOK_EXE_HELPER, *PXHOOK_EXE_HELPER;

#pragma pack(pop)

#define XHOOK_SECTION_HEADER_DECLARE(cbSectionSize) \
	{ \
      sizeof(XHOOK_SECTION_HEADER),\
      XHOOK_SECTION_HEADER_SIGNATURE,\
      sizeof(XHOOK_SECTION_HEADER),\
      (cbSectionSize),\
      \
      0,\
      0,\
      0,\
      0,\
      \
      0,\
      0,\
      0,\
      0,\
	}

	/////////////////////////////////////////////////////////////// Helper Macros.
	//
#define XHOOKS_STRINGIFY(x)    XHOOKS_STRINGIFY_(x)
#define XHOOKS_STRINGIFY_(x)    #x

	///////////////////////////////////////////////////////////// Binary Typedefs.
	//
	typedef BOOL(CALLBACK *PF_XHOOK_BINARY_BYWAY_CALLBACK)(PVOID pContext,
		PCHAR pszFile,
		PCHAR *ppszOutFile);

	typedef BOOL(CALLBACK *PF_XHOOK_BINARY_FILE_CALLBACK)(PVOID pContext,
		PCHAR pszOrigFile,
		PCHAR pszFile,
		PCHAR *ppszOutFile);

	typedef BOOL(CALLBACK *PF_XHOOK_BINARY_SYMBOL_CALLBACK)(PVOID pContext,
		ULONG nOrigOrdinal,
		ULONG nOrdinal,
		ULONG *pnOutOrdinal,
		PCHAR pszOrigSymbol,
		PCHAR pszSymbol,
		PCHAR *ppszOutSymbol);

	typedef BOOL(CALLBACK *PF_XHOOK_BINARY_COMMIT_CALLBACK)(PVOID pContext);

	typedef BOOL(CALLBACK *PF_XHOOK_ENUMERATE_EXPORT_CALLBACK)(PVOID pContext,
		ULONG nOrdinal,
		PCHAR pszName,
		PVOID pCode);

	typedef BOOL(CALLBACK *PF_XHOOK_IMPORT_FILE_CALLBACK)(PVOID pContext,
		HMODULE hModule,
		PCSTR pszFile);

	typedef BOOL(CALLBACK *PF_XHOOK_IMPORT_FUNC_CALLBACK)(PVOID pContext,
		DWORD nOrdinal,
		PCSTR pszFunc,
		PVOID pvFunc);

	typedef VOID * PXHOOK_BINARY;
	typedef VOID * PXHOOK_LOADED_BINARY;

	//////////////////////////////////////////////////////////// Transaction APIs.
	//
	LONG WINAPI XHookTransactionBegin(VOID);
	LONG WINAPI XHookTransactionAbort(VOID);
	LONG WINAPI XHookTransactionCommit(VOID);
	LONG WINAPI XHookTransactionCommitEx(PVOID **pppFailedPointer);

	LONG WINAPI XHookUpdateThread(HANDLE hThread);

	LONG WINAPI XHookAttach(PVOID *ppPointer,
		PVOID pXHook);

	LONG WINAPI XHookAttachEx(PVOID *ppPointer,
		PVOID pXHook,
		PXHOOK_TRAMPOLINE *ppRealTrampoline,
		PVOID *ppRealTarget,
		PVOID *ppRealXHook);

	LONG WINAPI XHookDetach(PVOID *ppPointer,
		PVOID pXHook);

	BOOL WINAPI XHookSetIgnoreTooSmall(BOOL fIgnore);
	BOOL WINAPI XHookSetRetainRegions(BOOL fRetain);

	////////////////////////////////////////////////////////////// Code Functions.
	//
	PVOID WINAPI XHookFindFunction(PCSTR pszModule, PCSTR pszFunction);
	PVOID WINAPI XHookCodeFromPointer(PVOID pPointer, PVOID *ppGlobals);
	PVOID WINAPI XHookCopyInstruction(PVOID pDst,
		PVOID *pDstPool,
		PVOID pSrc,
		PVOID *ppTarget,
		LONG *plExtra);

	///////////////////////////////////////////////////// Loaded Binary Functions.
	//
	HMODULE WINAPI XHookGetContainingModule(PVOID pvAddr);
	HMODULE WINAPI XHookEnumerateModules(HMODULE hModuleLast);
	PVOID WINAPI XHookGetEntryPoint(HMODULE hModule);
	ULONG WINAPI XHookGetModuleSize(HMODULE hModule);
	BOOL WINAPI XHookEnumerateExports(HMODULE hModule,
		PVOID pContext,
		PF_XHOOK_ENUMERATE_EXPORT_CALLBACK pfExport);
	BOOL WINAPI XHookEnumerateImports(HMODULE hModule,
		PVOID pContext,
		PF_XHOOK_IMPORT_FILE_CALLBACK pfImportFile,
		PF_XHOOK_IMPORT_FUNC_CALLBACK pfImportFunc);

	PVOID WINAPI XHookFindPayload(HMODULE hModule, REFGUID rguid, DWORD *pcbData);
	PVOID WINAPI XHookFindPayloadEx(REFGUID rguid, DWORD * pcbData);
	DWORD WINAPI XHookGetSizeOfPayloads(HMODULE hModule);

	///////////////////////////////////////////////// Persistent Binary Functions.
	//

	PXHOOK_BINARY WINAPI XHookBinaryOpen(HANDLE hFile);
	PVOID WINAPI XHookBinaryEnumeratePayloads(PXHOOK_BINARY pBinary,
		GUID *pGuid,
		DWORD *pcbData,
		DWORD *pnIterator);
	PVOID WINAPI XHookBinaryFindPayload(PXHOOK_BINARY pBinary,
		REFGUID rguid,
		DWORD *pcbData);
	PVOID WINAPI XHookBinarySetPayload(PXHOOK_BINARY pBinary,
		REFGUID rguid,
		PVOID pData,
		DWORD cbData);
	BOOL WINAPI XHookBinaryDeletePayload(PXHOOK_BINARY pBinary, REFGUID rguid);
	BOOL WINAPI XHookBinaryPurgePayloads(PXHOOK_BINARY pBinary);
	BOOL WINAPI XHookBinaryResetImports(PXHOOK_BINARY pBinary);
	BOOL WINAPI XHookBinaryEditImports(PXHOOK_BINARY pBinary,
		PVOID pContext,
		PF_XHOOK_BINARY_BYWAY_CALLBACK pfByway,
		PF_XHOOK_BINARY_FILE_CALLBACK pfFile,
		PF_XHOOK_BINARY_SYMBOL_CALLBACK pfSymbol,
		PF_XHOOK_BINARY_COMMIT_CALLBACK pfCommit);
	BOOL WINAPI XHookBinaryWrite(PXHOOK_BINARY pBinary, HANDLE hFile);
	BOOL WINAPI XHookBinaryClose(PXHOOK_BINARY pBinary);

	/////////////////////////////////////////////////// Create Process & Load Dll.
	//
	typedef BOOL(WINAPI *PXHOOK_CREATE_PROCESS_ROUTINEA)
		(LPCSTR lpApplicationName,
		LPSTR lpCommandLine,
		LPSECURITY_ATTRIBUTES lpProcessAttributes,
		LPSECURITY_ATTRIBUTES lpThreadAttributes,
		BOOL bInheritHandles,
		DWORD dwCreationFlags,
		LPVOID lpEnvironment,
		LPCSTR lpCurrentDirectory,
		LPSTARTUPINFOA lpStartupInfo,
		LPPROCESS_INFORMATION lpProcessInformation);

	typedef BOOL(WINAPI *PXHOOK_CREATE_PROCESS_ROUTINEW)
		(LPCWSTR lpApplicationName,
		LPWSTR lpCommandLine,
		LPSECURITY_ATTRIBUTES lpProcessAttributes,
		LPSECURITY_ATTRIBUTES lpThreadAttributes,
		BOOL bInheritHandles,
		DWORD dwCreationFlags,
		LPVOID lpEnvironment,
		LPCWSTR lpCurrentDirectory,
		LPSTARTUPINFOW lpStartupInfo,
		LPPROCESS_INFORMATION lpProcessInformation);

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
		PXHOOK_CREATE_PROCESS_ROUTINEA
		pfCreateProcessA);

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
		PXHOOK_CREATE_PROCESS_ROUTINEW
		pfCreateProcessW);

#ifdef UNICODE
#define XHookCreateProcessWithDll      XHookCreateProcessWithDllW
#define PXHOOK_CREATE_PROCESS_ROUTINE  PXHOOK_CREATE_PROCESS_ROUTINEW
#else
#define XHookCreateProcessWithDll      XHookCreateProcessWithDllA
#define PXHOOK_CREATE_PROCESS_ROUTINE  PXHOOK_CREATE_PROCESS_ROUTINEA
#endif // !UNICODE

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
		pfCreateProcessA);

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
		pfCreateProcessW);

#ifdef UNICODE
#define XHookCreateProcessWithDllEx    XHookCreateProcessWithDllExW
#define PXHOOK_CREATE_PROCESS_ROUTINE  PXHOOK_CREATE_PROCESS_ROUTINEW
#else
#define XHookCreateProcessWithDllEx    XHookCreateProcessWithDllExA
#define PXHOOK_CREATE_PROCESS_ROUTINE  PXHOOK_CREATE_PROCESS_ROUTINEA
#endif // !UNICODE

	BOOL WINAPI XHookProcessViaHelperA(DWORD dwTargetPid,
		LPCSTR lpDllName,
		PXHOOK_CREATE_PROCESS_ROUTINEA pfCreateProcessA);

	BOOL WINAPI XHookProcessViaHelperW(DWORD dwTargetPid,
		LPCSTR lpDllName,
		PXHOOK_CREATE_PROCESS_ROUTINEW pfCreateProcessW);

#ifdef UNICODE
#define XHookProcessViaHelper          XHookProcessViaHelperW
#else
#define XHookProcessViaHelper          XHookProcessViaHelperA
#endif // !UNICODE

	BOOL WINAPI XHookUpdateProcessWithDll(HANDLE hProcess,
		LPCSTR *plpDlls,
		DWORD nDlls);

	BOOL WINAPI XHookCopyPayloadToProcess(HANDLE hProcess,
		REFGUID rguid,
		PVOID pvData,
		DWORD cbData);
	BOOL WINAPI XHookRestoreAfterWith(VOID);
	BOOL WINAPI XHookRestoreAfterWithEx(PVOID pvData, DWORD cbData);
	BOOL WINAPI XHookIsHelperProcess(VOID);
	VOID CALLBACK XHookFinishHelperProcess(HWND, HINSTANCE, LPSTR, INT);

	//
	//////////////////////////////////////////////////////////////////////////////
#ifdef __cplusplus
}
#endif // __cplusplus

//////////////////////////////////////////////// XHooks Internal Definitions.
//
#ifdef __cplusplus
#ifdef XHOOKS_INTERNAL

#ifndef __deref_out
#define __deref_out
#endif

#ifndef __deref
#define __deref
#endif

//////////////////////////////////////////////////////////////////////////////
//
#if (_MSC_VER < 1299)
#include <imagehlp.h>
typedef IMAGEHLP_MODULE IMAGEHLP_MODULE64;
typedef PIMAGEHLP_MODULE PIMAGEHLP_MODULE64;
typedef IMAGEHLP_SYMBOL SYMBOL_INFO;
typedef PIMAGEHLP_SYMBOL PSYMBOL_INFO;

static inline
LONG InterlockedCompareExchange(LONG *ptr, LONG nval, LONG oval)
{
	return (LONG)::InterlockedCompareExchange((PVOID*)ptr, (PVOID)nval, (PVOID)oval);
}
#else
#include <dbghelp.h>
#endif

#ifdef IMAGEAPI // defined by DBGHELP.H
typedef LPAPI_VERSION(NTAPI *PF_ImagehlpApiVersionEx)(LPAPI_VERSION AppVersion);

typedef BOOL(NTAPI *PF_SymInitialize)(IN HANDLE hProcess,
	IN LPCSTR UserSearchPath,
	IN BOOL fInvadeProcess);
typedef DWORD(NTAPI *PF_SymSetOptions)(IN DWORD SymOptions);
typedef DWORD(NTAPI *PF_SymGetOptions)(VOID);
typedef DWORD64(NTAPI *PF_SymLoadModule64)(IN HANDLE hProcess,
	IN HANDLE hFile,
	IN PSTR ImageName,
	IN PSTR ModuleName,
	IN DWORD64 BaseOfDll,
	IN DWORD SizeOfDll);
typedef BOOL(NTAPI *PF_SymGetModuleInfo64)(IN HANDLE hProcess,
	IN DWORD64 qwAddr,
	OUT PIMAGEHLP_MODULE64 ModuleInfo);
typedef BOOL(NTAPI *PF_SymFromName)(IN HANDLE hProcess,
	IN LPSTR Name,
	OUT PSYMBOL_INFO Symbol);

typedef struct _XHOOK_SYM_INFO
{
	HANDLE                  hProcess;
	HMODULE                 hDbgHelp;
	PF_ImagehlpApiVersionEx pfImagehlpApiVersionEx;
	PF_SymInitialize        pfSymInitialize;
	PF_SymSetOptions        pfSymSetOptions;
	PF_SymGetOptions        pfSymGetOptions;
	PF_SymLoadModule64      pfSymLoadModule64;
	PF_SymGetModuleInfo64   pfSymGetModuleInfo64;
	PF_SymFromName          pfSymFromName;
} XHOOK_SYM_INFO, *PXHOOK_SYM_INFO;

PXHOOK_SYM_INFO XHookLoadDbgHelp(VOID);

#endif // IMAGEAPI

#ifndef XHOOK_TRACE
#if XHOOK_DEBUG
#define XHOOK_TRACE(x) printf x
#define XHOOK_BREAK()  __debugbreak()
#include <stdio.h>
#include <limits.h>
#else
#define XHOOK_TRACE(x)
#define XHOOK_BREAK()
#endif
#endif

#ifdef XHOOKS_IA64
__declspec(align(16)) struct XHOOK_IA64_BUNDLE
{
public:
	union
	{
		BYTE    data[16];
		UINT64  wide[2];
	};

public:
	struct XHOOK_IA64_METADATA;

	typedef BOOL(XHOOK_IA64_BUNDLE::* XHOOK_IA64_METACOPY)
		(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;

	enum {
		A_UNIT = 1u,
		I_UNIT = 2u,
		M_UNIT = 3u,
		B_UNIT = 4u,
		F_UNIT = 5u,
		L_UNIT = 6u,
		X_UNIT = 7u,
		UNIT_MASK = 7u,
		STOP = 8u
	};
	struct XHOOK_IA64_METADATA
	{
		ULONG       nTemplate : 8;    // Instruction template.
		ULONG       nUnit0 : 4;    // Unit for slot 0
		ULONG       nUnit1 : 4;    // Unit for slot 1
		ULONG       nUnit2 : 4;    // Unit for slot 2
		XHOOK_IA64_METACOPY    pfCopy;     // Function pointer.
	};

protected:
	BOOL CopyBytes(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;
	BOOL CopyBytesMMB(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;
	BOOL CopyBytesMBB(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;
	BOOL CopyBytesBBB(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;
	BOOL CopyBytesMLX(const XHOOK_IA64_METADATA *pMeta, XHOOK_IA64_BUNDLE *pDst) const;

	static const XHOOK_IA64_METADATA s_rceCopyTable[33];

public:
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
	BYTE    GetTemplate() const;
	BYTE    GetInst0() const;
	BYTE    GetInst1() const;
	BYTE    GetInst2() const;
	BYTE    GetUnit0() const;
	BYTE    GetUnit1() const;
	BYTE    GetUnit2() const;
	UINT64  GetData0() const;
	UINT64  GetData1() const;
	UINT64  GetData2() const;

public:
	BOOL    IsBrl() const;
	VOID    SetBrl();
	VOID    SetBrl(UINT64 target);
	UINT64  GetBrlTarget() const;
	VOID    SetBrlTarget(UINT64 target);
	VOID    SetBrlImm(UINT64 imm);
	UINT64  GetBrlImm() const;

	BOOL    IsMovlGp() const;
	UINT64  GetMovlGp() const;
	VOID    SetMovlGp(UINT64 gp);

	VOID    SetInst0(BYTE nInst);
	VOID    SetInst1(BYTE nInst);
	VOID    SetInst2(BYTE nInst);
	VOID    SetData0(UINT64 nData);
	VOID    SetData1(UINT64 nData);
	VOID    SetData2(UINT64 nData);
	BOOL    SetNop0();
	BOOL    SetNop1();
	BOOL    SetNop2();
	BOOL    SetStop();

	BOOL    Copy(XHOOK_IA64_BUNDLE *pDst) const;
};
#endif // XHOOKS_IA64

//#ifdef XHOOKS_ARM
//#error Feature not supported in this release.



//#endif // XHOOKS_ARM

//////////////////////////////////////////////////////////////////////////////

#endif // XHOOKS_INTERNAL
#endif // __cplusplus


#ifndef XHOOKS_STRINGIFY
#define XHOOKS_STRINGIFY(x)    XHOOKS_STRINGIFY_(x)
#define XHOOKS_STRINGIFY_(x)    #x
#endif

#define VER_FILEFLAGSMASK   0x3fL
#define VER_FILEFLAGS       0x0L
#define VER_FILEOS          0x00040004L
#define VER_FILETYPE        0x00000002L
#define VER_FILESUBTYPE     0x00000000L

#define VER_XHOOKS_BITS    XHOOK_STRINGIFY(XHOOKS_BITS)

