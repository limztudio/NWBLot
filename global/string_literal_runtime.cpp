// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_string_literal_runtime{
struct StringLiteralRecordHeader{
    u32 magic;
    u32 byteCount;
    u32 payloadOffset;
    u32 recordStride;
    u64 seed;
};

static_assert(sizeof(StringLiteralRecordHeader) == 24);
static_assert(alignof(StringLiteralRecordHeader) <= 16);

constexpr u32 s_RecordMagic = 0x474c4253;
constexpr usize s_RecordAlignment = 16;

#if defined(_WIN32)
#pragma section(".glb$A", read, write)
#pragma section(".glb$Z", read, write)

alignas(16) static __declspec(allocate(".glb$A")) __attribute__((used)) u8 s_StringLiteralPoolBegin[16] = {};
alignas(16) static __declspec(allocate(".glb$Z")) __attribute__((used)) u8 s_StringLiteralPoolEnd[16] = {};
#elif defined(__APPLE__)
__attribute__((section("__DATA,__glblit"), used, aligned(16))) static u8 s_StringLiteralPoolEmpty[16] = {};
extern "C" u8 g_StringLiteralPoolBegin[] __asm("section$start$__DATA$__glblit");
extern "C" u8 g_StringLiteralPoolEnd[] __asm("section$end$__DATA$__glblit");
#elif defined(__ELF__)
__attribute__((section("glb_literals"), used, retain, aligned(16))) static u8 s_StringLiteralPoolEmpty[16] = {};
extern "C" __attribute__((visibility("hidden"))) u8 __start_glb_literals[];
extern "C" __attribute__((visibility("hidden"))) u8 __stop_glb_literals[];
#else
#error Unsupported string literal pool object format
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_string_literal_runtime{
static void DecodeStringLiteralPool(const usize begin, const usize end)noexcept{
    if(end < begin || (begin & (s_RecordAlignment - 1)) != 0)
        __builtin_trap();

    usize cursor = begin;
    while(cursor < end){
        const usize remaining = end - cursor;
        const usize paddingSize = remaining < s_RecordAlignment ? remaining : s_RecordAlignment;
        const u8* const recordBytes = reinterpret_cast<const u8*>(cursor);
        bool zeroPadding = true;
        for(usize i = 0; i < paddingSize; ++i){
            if(recordBytes[i] != 0){
                zeroPadding = false;
                break;
            }
        }
        if(zeroPadding){
            cursor += paddingSize;
            continue;
        }

        if(remaining < sizeof(StringLiteralRecordHeader))
            __builtin_trap();

        const StringLiteralRecordHeader& header = *reinterpret_cast<const StringLiteralRecordHeader*>(cursor);
        if(
            header.magic != s_RecordMagic
            || header.payloadOffset < sizeof(StringLiteralRecordHeader)
            || (header.payloadOffset & (s_RecordAlignment - 1)) != 0
            || header.recordStride < header.payloadOffset
            || (header.recordStride & (s_RecordAlignment - 1)) != 0
            || header.recordStride > remaining
            || header.byteCount > header.recordStride - header.payloadOffset
        )
            __builtin_trap();

        const u32 byteCount = header.byteCount;
        const u32 recordStride = header.recordStride;
        const u64 seed = header.seed;
        u8* const payload = reinterpret_cast<u8*>(cursor + header.payloadOffset);
        for(usize i = 0; i < byteCount; ++i){
            const u8 mask = static_cast<u8>((seed >> ((i & 7) * 8)) ^ (i * 0x9d + (i >> 8)));
            payload[i] ^= mask;
        }
        cursor += recordStride;
    }
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Link this object first in each image; Mach-O runs constructors in object order.
#if defined(__APPLE__)
extern "C" __attribute__((constructor, used, visibility("hidden"))) void GlbInitializeStringLiteralPool()noexcept{
#elif defined(__ELF__)
extern "C" __attribute__((constructor(101), used, visibility("hidden"))) void GlbInitializeStringLiteralPool()noexcept{
#else
extern "C" __attribute__((constructor(101), used)) void GlbInitializeStringLiteralPool()noexcept{
#endif
    using namespace __hidden_string_literal_runtime;
#if defined(_WIN32)
    const usize begin = reinterpret_cast<usize>(s_StringLiteralPoolBegin) + sizeof(s_StringLiteralPoolBegin);
    const usize end = reinterpret_cast<usize>(s_StringLiteralPoolEnd);
#elif defined(__APPLE__)
    const usize begin = reinterpret_cast<usize>(g_StringLiteralPoolBegin);
    const usize end = reinterpret_cast<usize>(g_StringLiteralPoolEnd);
#else
    const usize begin = reinterpret_cast<usize>(__start_glb_literals);
    const usize end = reinterpret_cast<usize>(__stop_glb_literals);
#endif
    DecodeStringLiteralPool(begin, end);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

