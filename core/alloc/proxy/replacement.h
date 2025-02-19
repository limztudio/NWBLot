// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <stddef.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef enum{
    FRR_OK,     /* Succeeded in replacing the function */
    FRR_NODLL,  /* The requested DLL was not found */
    FRR_NOFUNC, /* The requested function was not found */
    FRR_FAILED, /* The function replacement request failed */
} FRR_TYPE;

typedef enum{
    FRR_FAIL,     /* Required function */
    FRR_IGNORE,   /* optional function */
} FRR_ON_ERROR;

typedef void (*FUNCPTR)();

#ifdef NWB_UNICODE
#define ReplaceFunction ReplaceFunctionW
#define IsPrologueKnown IsPrologueKnownW
#else
#define ReplaceFunction ReplaceFunctionA
#define IsPrologueKnown IsPrologueKnownA
#endif

FRR_TYPE ReplaceFunctionA(const char* dllName, const char* funcName, FUNCPTR newFunc, const char** opcodes, FUNCPTR* origFunc = nullptr);
FRR_TYPE ReplaceFunctionW(const wchar_t* dllName, const char* funcName, FUNCPTR newFunc, const char** opcodes, FUNCPTR* origFunc = nullptr);

bool IsPrologueKnownA(const char* dllName, const char* funcName, const char** opcodes, HMODULE module);
bool IsPrologueKnownW(const wchar_t* dllName, const char* funcName, const char** opcodes, HMODULE module);

// Utilities to convert between ADDRESS and LPVOID
union Int2Ptr{
    UINT_PTR uip;
    LPVOID lpv;
};

inline UINT_PTR Ptr2Addrint(LPVOID ptr);
inline LPVOID Addrint2Ptr(UINT_PTR ptr);

// The size of a trampoline region
const unsigned MAX_PROBE_SIZE = 32;

// The size of a jump relative instruction "e9 00 00 00 00"
const unsigned SIZE_OF_RELJUMP = 5;

// The size of jump RIP relative indirect "ff 25 00 00 00 00"
const unsigned SIZE_OF_INDJUMP = 6;

// The size of address we put in the location (in Intel64)
const unsigned SIZE_OF_ADDRESS = 8;

// The size limit (in bytes) for an opcode pattern to fit into a trampoline
// There should be enough space left for a relative jump; +1 is for the extra pattern byte.
const unsigned MAX_PATTERN_SIZE = MAX_PROBE_SIZE - SIZE_OF_RELJUMP + 1;

// The max distance covered in 32 bits: 2^31 - 1 - C
// where C should not be smaller than the size of a probe.
// The latter is important to correctly handle "backward" jumps.
const __int64 MAX_DISTANCE = (((__int64)1 << 31) - 1) - MAX_PROBE_SIZE;

// The maximum number of distinct buffers in memory
const ptrdiff_t MAX_NUM_BUFFERS = 256;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

