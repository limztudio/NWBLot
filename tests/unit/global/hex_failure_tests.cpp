// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/global/hex_case_types.adhd.h>

#include <global/binary.h>
#include <global/compact_string.h>
#include <global/diagnostics.h>
#include <global/filesystem/operations_file_io.h>
#include <global/hash_utils.h>
#include <global/inplace_function.h>
#include <global/numeric_text.h>
#include <global/refcount_ptr.h>
#include <global/unique_ptr.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_hex_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ThrowingHexCapacityPredicate{
    explicit operator b8()const{
        throw s_HexCapacityFailure;
    }
};

[[nodiscard]] ThrowingHexCapacityPredicate operator>(HexCapacityValue, HexCapacityValue)noexcept{ return {}; }

[[nodiscard]] usize operator-(const HexCapacityValue lhs, const HexCapacityValue rhs)noexcept{ return lhs.value - rhs.value; }

struct ThrowingHexCapacityString{
    using value_type = char;


public:
    ACompactString text;


public:
    [[nodiscard]] HexCapacityValue size()const noexcept{ return { .value = text.size() }; }

    [[nodiscard]] HexCapacityValue max_size()const noexcept{ return { .value = text.max_size() }; }

    void push_back(const char ch)noexcept{ text.push_back(ch); }
};

struct ThrowingHexAppendString{
    using value_type = char;


public:
    ACompactString text;


public:
    ThrowingHexAppendString& operator+=(const char ch){
        push_back(ch);
        return *this;
    }


public:
    [[nodiscard]] usize size()const noexcept{ return text.size(); }

    [[nodiscard]] usize max_size()const noexcept{ return text.max_size(); }

    void push_back(const char ch){
        if(text.size() == 2u)
            throw s_HexAppendFailure;
        text.push_back(ch);
    }
};




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NoexceptFailureTests, HexAppendCapacityAndOutputFailuresPreservePublishedCharacters){
    using namespace __hidden_hex_failure_tests;
    ACompactString fixed;
    static_assert(noexcept(AppendHexU32(u32{}, fixed)));
    static_assert(noexcept(AppendHexU64(u64{}, fixed)));
    static_assert(noexcept(AppendHexU32UnsignedLiteral(u32{}, fixed)));

    ThrowingHexCapacityString capacity;
    static_assert(!noexcept(AppendHexU64(u64{}, capacity)));
    EXPECT_THROW(AppendHexU64(1u, capacity), u32);
    EXPECT_TRUE(capacity.text.empty());

    ThrowingHexAppendString output;
    static_assert(!noexcept(AppendHexU64(u64{}, output)));
    EXPECT_THROW(AppendHexU64(0xabcdef0123456789u, output), u32);
    EXPECT_EQ(output.text.view(), AStringView("ab"));
    output.text.clear();
    static_assert(!noexcept(AppendHexU32UnsignedLiteral(u32{}, output)));
    EXPECT_THROW(AppendHexU32UnsignedLiteral(0x89abcdefu, output), u32);
    EXPECT_EQ(output.text.view(), AStringView("0x"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

