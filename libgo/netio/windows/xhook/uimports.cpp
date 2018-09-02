#define XHOOKS_INTERNAL 1
#include <Windows.h>
#include "xhook.h"

// UpdateImports32 aka UpdateImports64
static BOOL UPDATE_IMPORTS_XX(HANDLE hProcess,
                              HMODULE hModule,
                              LPCSTR *plpDlls,
                              DWORD nDlls)
{
    BOOL fSucceeded = FALSE;
    BYTE * pbNew = NULL;
    DWORD i;

    PBYTE pbModule = (PBYTE)hModule;

    IMAGE_DOS_HEADER idh;
    ZeroMemory(&idh, sizeof(idh));
    if (!ReadProcessMemory(hProcess, pbModule, &idh, sizeof(idh), NULL)) {
        XHOOK_TRACE(("ReadProcessMemory(idh@%p..%p) failed: %d\n",
                      pbModule, pbModule + sizeof(idh), GetLastError()));

      finish:
        if (pbNew != NULL) {
            delete[] pbNew;
            pbNew = NULL;
        }
        return fSucceeded;
    }

    IMAGE_NT_HEADERS_XX inh;
    ZeroMemory(&inh, sizeof(inh));

    if (!ReadProcessMemory(hProcess, pbModule + idh.e_lfanew, &inh, sizeof(inh), NULL)) {
        XHOOK_TRACE(("ReadProcessMemory(inh@%p..%p) failed: %d\n",
                      pbModule + idh.e_lfanew,
                      pbModule + idh.e_lfanew + sizeof(inh),
                      GetLastError()));
        goto finish;
    }

    if (inh.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR_MAGIC_XX) {
        XHOOK_TRACE(("Wrong size image (%04x != %04x).\n",
                      inh.OptionalHeader.Magic, IMAGE_NT_OPTIONAL_HDR_MAGIC_XX));
        SetLastError(ERROR_INVALID_BLOCK);
        goto finish;
    }

    // Zero out the bound table so loader doesn't use it instead of our new table.
    inh.BOUND_DIRECTORY.VirtualAddress = 0;
    inh.BOUND_DIRECTORY.Size = 0;

    // Find the size of the mapped file.
    DWORD dwFileSize = 0;
    DWORD dwSec = idh.e_lfanew +
        FIELD_OFFSET(IMAGE_NT_HEADERS_XX, OptionalHeader) +
        inh.FileHeader.SizeOfOptionalHeader;

    for (i = 0; i < inh.FileHeader.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER ish;
        ZeroMemory(&ish, sizeof(ish));

        if (!ReadProcessMemory(hProcess, pbModule + dwSec + sizeof(ish) * i, &ish,
                               sizeof(ish), NULL)) {
            XHOOK_TRACE(("ReadProcessMemory(ish@%p..%p) failed: %d\n",
                          pbModule + dwSec + sizeof(ish) * i,
                          pbModule + dwSec + sizeof(ish) * (i + 1),
                          GetLastError()));
            goto finish;
        }

        XHOOK_TRACE(("ish[%d] : va=%08x sr=%d\n", i, ish.VirtualAddress, ish.SizeOfRawData));

        // If the file didn't have an IAT_DIRECTORY, we assign it...
        if (inh.IAT_DIRECTORY.VirtualAddress == 0 &&
            inh.IMPORT_DIRECTORY.VirtualAddress >= ish.VirtualAddress &&
            inh.IMPORT_DIRECTORY.VirtualAddress < ish.VirtualAddress + ish.SizeOfRawData) {

            inh.IAT_DIRECTORY.VirtualAddress = ish.VirtualAddress;
            inh.IAT_DIRECTORY.Size = ish.SizeOfRawData;
        }

        // Find the end of the file...
        if (dwFileSize < ish.PointerToRawData + ish.SizeOfRawData) {
            dwFileSize = ish.PointerToRawData + ish.SizeOfRawData;
        }
    }
    XHOOK_TRACE(("dwFileSize = %08x\n", dwFileSize));

#if IGNORE_CHECKSUMS
    // Find the current checksum.
    WORD wBefore = ComputeChkSum(hProcess, pbModule, &inh);
    XHOOK_TRACE(("ChkSum: %04x + %08x => %08x\n", wBefore, dwFileSize, wBefore + dwFileSize));
#endif

    XHOOK_TRACE(("     Imports: %p..%p\n",
                  (DWORD_PTR)pbModule + inh.IMPORT_DIRECTORY.VirtualAddress,
                  (DWORD_PTR)pbModule + inh.IMPORT_DIRECTORY.VirtualAddress +
                  inh.IMPORT_DIRECTORY.Size));

    DWORD obRem = sizeof(IMAGE_IMPORT_DESCRIPTOR) * nDlls;
    DWORD obTab = PadToDwordPtr(obRem +
                                inh.IMPORT_DIRECTORY.Size +
                                sizeof(IMAGE_IMPORT_DESCRIPTOR));
    DWORD obDll = obTab + sizeof(DWORD_XX) * 4 * nDlls;
    DWORD obStr = obDll;
    DWORD cbNew = obStr;
    DWORD n;
    for (n = 0; n < nDlls; n++) {
        cbNew += PadToDword((DWORD)strlen(plpDlls[n]) + 1);
    }

    pbNew = new BYTE [cbNew];
    if (pbNew == NULL) {
        XHOOK_TRACE(("new BYTE [cbNew] failed.\n"));
        goto finish;
    }
    ZeroMemory(pbNew, cbNew);

    PBYTE pbBase = pbModule;
    PBYTE pbNext = pbBase
        + inh.OptionalHeader.BaseOfCode
        + inh.OptionalHeader.SizeOfCode
        + inh.OptionalHeader.SizeOfInitializedData
        + inh.OptionalHeader.SizeOfUninitializedData;
    if (pbBase < pbNext) {
        pbBase = pbNext;
    }
    XHOOK_TRACE(("pbBase = %p\n", pbBase));

    PBYTE pbNewIid = FindAndAllocateNearBase(hProcess, pbBase, cbNew);
    if (pbNewIid == NULL) {
        XHOOK_TRACE(("FindAndAllocateNearBase failed.\n"));
        goto finish;
    }

    DWORD obBase = (DWORD)(pbNewIid - pbModule);
    DWORD dwProtect = 0;
    if (inh.IMPORT_DIRECTORY.VirtualAddress != 0) {
        // Read the old import directory if it exists.
#if 0
        if (!VirtualProtectEx(hProcess,
                              pbModule + inh.IMPORT_DIRECTORY.VirtualAddress,
                              inh.IMPORT_DIRECTORY.Size, PAGE_EXECUTE_READWRITE, &dwProtect)) {
            XHOOK_TRACE(("VirtualProtectEx(import) write failed: %d\n", GetLastError()));
            goto finish;
        }
#endif
        XHOOK_TRACE(("IMPORT_DIRECTORY perms=%x\n", dwProtect));

        if (!ReadProcessMemory(hProcess,
                               pbModule + inh.IMPORT_DIRECTORY.VirtualAddress,
                               pbNew + obRem,
                               inh.IMPORT_DIRECTORY.Size, NULL)) {
            XHOOK_TRACE(("ReadProcessMemory(imports) failed: %d\n", GetLastError()));
            goto finish;
        }
    }

    PIMAGE_IMPORT_DESCRIPTOR piid = (PIMAGE_IMPORT_DESCRIPTOR)pbNew;
    DWORD_XX *pt;

    for (n = 0; n < nDlls; n++) {
        HRESULT hrRet = StringCchCopyA((char*)pbNew + obStr, cbNew - obStr, plpDlls[n]);
        if (FAILED(hrRet)) {
            XHOOK_TRACE(("StringCchCopyA failed: %d\n", GetLastError()));
            goto finish;
        }

        // After copying the string, we patch up the size "??" bits if any.
        hrRet = ReplaceOptionalSizeA((char*)pbNew + obStr,
                                     cbNew - obStr,
                                     XHOOKS_STRINGIFY(XHOOKS_BITS_XX));
        if (FAILED(hrRet)) {
            XHOOK_TRACE(("ReplaceOptionalSizeA failed: %d\n", GetLastError()));
            goto finish;
        }

        DWORD nOffset = obTab + (sizeof(DWORD_XX) * (4 * n));
        piid[n].OriginalFirstThunk = obBase + nOffset;
        pt = ((DWORD_XX*)(pbNew + nOffset));
        pt[0] = IMAGE_ORDINAL_FLAG_XX + 1;
        pt[1] = 0;

        nOffset = obTab + (sizeof(DWORD_XX) * ((4 * n) + 2));
        piid[n].FirstThunk = obBase + nOffset;
        pt = ((DWORD_XX*)(pbNew + nOffset));
        pt[0] = IMAGE_ORDINAL_FLAG_XX + 1;
        pt[1] = 0;
        piid[n].TimeDateStamp = 0;
        piid[n].ForwarderChain = 0;
        piid[n].Name = obBase + obStr;

        obStr += PadToDword((DWORD)strlen(plpDlls[n]) + 1);
    }

    for (i = 0; i < nDlls + (inh.IMPORT_DIRECTORY.Size / sizeof(*piid)); i++) {
        XHOOK_TRACE(("%8d. Look=%08x Time=%08x Fore=%08x Name=%08x Addr=%08x\n",
                      i,
                      piid[i].OriginalFirstThunk,
                      piid[i].TimeDateStamp,
                      piid[i].ForwarderChain,
                      piid[i].Name,
                      piid[i].FirstThunk));
        if (piid[i].OriginalFirstThunk == 0 && piid[i].FirstThunk == 0) {
            break;
        }
    }

    if (!WriteProcessMemory(hProcess, pbNewIid, pbNew, obStr, NULL)) {
        XHOOK_TRACE(("WriteProcessMemory(iid) failed: %d\n", GetLastError()));
        goto finish;
    }

    XHOOK_TRACE(("obBaseBef = %08x..%08x\n",
                  inh.IMPORT_DIRECTORY.VirtualAddress,
                  inh.IMPORT_DIRECTORY.VirtualAddress + inh.IMPORT_DIRECTORY.Size));
    XHOOK_TRACE(("obBaseAft = %08x..%08x\n", obBase, obBase + obStr));

    // If the file doesn't have an IAT_DIRECTORY, we create it...
    if (inh.IAT_DIRECTORY.VirtualAddress == 0) {
        inh.IAT_DIRECTORY.VirtualAddress = obBase;
        inh.IAT_DIRECTORY.Size = cbNew;
    }

    inh.IMPORT_DIRECTORY.VirtualAddress = obBase;
    inh.IMPORT_DIRECTORY.Size = cbNew;

    /////////////////////// Update the NT header for the new import directory.
    /////////////////////////////// Update the DOS header to fix the checksum.
    //
    if (!VirtualProtectEx(hProcess, pbModule, inh.OptionalHeader.SizeOfHeaders,
                          PAGE_EXECUTE_READWRITE, &dwProtect)) {
        XHOOK_TRACE(("VirtualProtectEx(inh) write failed: %d\n", GetLastError()));
        goto finish;
    }

#if IGNORE_CHECKSUMS
    idh.e_res[0] = 0;
#else
    inh.OptionalHeader.CheckSum = 0;
#endif // IGNORE_CHECKSUMS

    if (!WriteProcessMemory(hProcess, pbModule, &idh, sizeof(idh), NULL)) {
        XHOOK_TRACE(("WriteProcessMemory(idh) failed: %d\n", GetLastError()));
        goto finish;
    }
    XHOOK_TRACE(("WriteProcessMemory(idh:%p..%p)\n", pbModule, pbModule + sizeof(idh)));

    if (!WriteProcessMemory(hProcess, pbModule + idh.e_lfanew, &inh, sizeof(inh), NULL)) {
        XHOOK_TRACE(("WriteProcessMemory(inh) failed: %d\n", GetLastError()));
        goto finish;
    }
    XHOOK_TRACE(("WriteProcessMemory(inh:%p..%p)\n",
                  pbModule + idh.e_lfanew,
                  pbModule + idh.e_lfanew + sizeof(inh)));

#if IGNORE_CHECKSUMS
    WORD wDuring = ComputeChkSum(hProcess, pbModule, &inh);
    XHOOK_TRACE(("ChkSum: %04x + %08x => %08x\n", wDuring, dwFileSize, wDuring + dwFileSize));

    idh.e_res[0] = xhook_sum_minus(idh.e_res[0], xhook_sum_minus(wDuring, wBefore));

    if (!WriteProcessMemory(hProcess, pbModule, &idh, sizeof(idh), NULL)) {
        XHOOK_TRACE(("WriteProcessMemory(idh) failed: %d\n", GetLastError()));
        goto finish;
    }
#endif // IGNORE_CHECKSUMS

    if (!VirtualProtectEx(hProcess, pbModule, inh.OptionalHeader.SizeOfHeaders,
                          dwProtect, &dwProtect)) {
        XHOOK_TRACE(("VirtualProtectEx(idh) restore failed: %d\n", GetLastError()));
        goto finish;
    }

#if IGNORE_CHECKSUMS
    WORD wAfter = ComputeChkSum(hProcess, pbModule, &inh);
    XHOOK_TRACE(("ChkSum: %04x + %08x => %08x\n", wAfter, dwFileSize, wAfter + dwFileSize));
    XHOOK_TRACE(("Before: %08x, After: %08x\n", wBefore + dwFileSize, wAfter + dwFileSize));

    if (wBefore != wAfter) {
        XHOOK_TRACE(("Restore of checksum failed %04x != %04x.\n", wBefore, wAfter));
        goto finish;
    }
#endif // IGNORE_CHECKSUMS

    fSucceeded = TRUE;
    goto finish;
}

