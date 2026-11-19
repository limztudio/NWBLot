// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/algorithm.h>
#include <global/base64.h>
#include <global/binary.h>
#include <global/diagnostics.h>
#include <global/interval.h>
#include <global/projected_index.h>
#include <tests/common/memory_sdk.h>
#include <tests/unit/global/algorithm_case_types.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Base64Bytes = ::Vector<u8, Core::Alloc::GlobalArena>;
using Base64Text = ::AString<Core::Alloc::GlobalArena>;
using NullableU32 = const u32*;
using NullableFormat = const char*;
using NullableU32View = Span<const u32* const>;

[[nodiscard]] inline AStringView Base64View(const Base64Text& text)noexcept{ return AStringView(text); }
[[nodiscard]] inline NullableU32 AddressU32(const u32& value)noexcept{ return AddressOf(value); }
[[nodiscard]] inline b8 SameU32Address(const NullableU32 lhs, const NullableU32 rhs)noexcept{ return lhs == rhs; }
[[nodiscard]] inline u32 ReadU32Address(const NullableU32 address)noexcept{ return *address; }

struct U32VectorView{
    using value_type = u32;
    const u32* values;
    usize count;
    explicit U32VectorView(const Span<const u32> span)noexcept : values(span.data()), count(span.size()){}
    [[nodiscard]] u32 operator[](const usize index)const noexcept{ return values[index]; }
    [[nodiscard]] b8 empty()const noexcept{ return count == 0u; }
    [[nodiscard]] usize size()const noexcept{ return count; }
    [[nodiscard]] const u32* data()const noexcept{ return values; }
    [[nodiscard]] const u32* begin()const noexcept{ return values; }
    [[nodiscard]] const u32* end()const noexcept{ return values + count; }
};

inline constexpr WStringView s_WideFormat = L"value {}.trailing";
inline constexpr const char* s_NullFormat = nullptr;
inline constexpr usize s_CaptureCapacity = s_MaxEventTextBytes;

struct CapturedField{
    void assign(const AStringView text)noexcept{ CopyFixedBuffer(m_bytes, text); }
    [[nodiscard]] AStringView view()const noexcept{ return AStringView(m_bytes.data()); }
private:
    Array<char, s_MaxEventTextBytes> m_bytes{};
};

[[nodiscard]] inline b8 HasTextAddress(const AStringView text)noexcept{ return text.data() != nullptr; }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingAssignment{
    u32 key = 0u;
    ThrowingAssignment& operator=(ThrowingAssignment& other)noexcept{ key = other.key; return *this; }
    ThrowingAssignment& operator=(const ThrowingAssignment&){ throw RuntimeException("const assignment failure"); }
};

struct SmallSequence{
    Array<ThrowingAssignment, 3u> entries{};
    usize count = 3u;
    [[nodiscard]] usize size()const noexcept{ return count; }
    ThrowingAssignment& operator[](const usize index)noexcept{ return entries[index]; }
    void resize(const usize value)noexcept{ count = value; }
};

struct ThrowingComparison{
    [[nodiscard]] b8 operator()(ThrowingAssignment& lhs, ThrowingAssignment& rhs)const noexcept{ return lhs.key == rhs.key; }
    [[nodiscard]] b8 operator()(ThrowingAssignment&, const ThrowingAssignment&)const{ throw RuntimeException("const comparison failure"); }
};

struct ThrowingIndexSequence{
    StableDedupValue value{};
    [[nodiscard]] usize size()const noexcept{ return 1u; }
    StableDedupValue& operator[](usize&){ throw RuntimeException("lvalue index failure"); }
    StableDedupValue& operator[](usize&&)noexcept{ return value; }
    void resize(usize&)noexcept{}
    void resize(usize&&)noexcept{}
};

struct ThrowingResizeSequence{
    StableDedupValue value{};
    [[nodiscard]] usize size()const noexcept{ return 0u; }
    StableDedupValue& operator[](usize&)noexcept{ return value; }
    StableDedupValue& operator[](usize&&)noexcept{ return value; }
    void resize(usize&){ throw RuntimeException("lvalue resize failure"); }
    void resize(usize&&)noexcept{}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

