// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/metascript/parser.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metascript_reader_failure_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Metascript;

struct ReaderArenaTag{};
using ReaderArena = NWB::Tests::TestArena<ReaderArenaTag>;

class UnexpectedReadFailure final : public GeneralException{
public:
    [[nodiscard]] virtual const char* what()const noexcept override{ return "unexpected reader failure"; }
};

class ThrowingReader final : public IMetaReader{
public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, usize)override{ throw UnexpectedReadFailure{}; }
};

class EndOfFileReader final : public IMetaReader{
public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, usize)noexcept override{ return 0u; }
};

class ChunkedReader final : public IMetaReader{
public:
    explicit ChunkedReader(MStringView source)noexcept
        : m_source(source)
    {}


public:
    [[nodiscard]] virtual Expected<usize> read(MChar* buffer, const usize maxBytes)noexcept override{
        usize count = 0u;
        while(count < 3u && count < maxBytes && m_offset < m_source.size())
            buffer[count++] = m_source[m_offset++];
        return count;
    }


private:
    MStringView m_source;
    usize m_offset = 0u;
};

class RejectedReader final : public IMetaReader{
public:
    explicit RejectedReader(const bool oversized)noexcept
        : m_oversized(oversized)
    {}


public:
    [[nodiscard]] virtual Expected<usize> read(MChar*, const usize maxBytes)noexcept override{
        if(m_oversized)
            return maxBytes + 1u;
        return MakeUnexpected(Failure{});
    }


private:
    bool m_oversized;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Metascript, UnexpectedReaderExceptionsPropagateWithoutBecomingParseErrors){
    ReaderArena arena;
    Document document(arena.arena);
    ThrowingReader reader;

    EXPECT_THROW(EXPECT_TRUE(document.parse(reader)), UnexpectedReadFailure);
    EXPECT_FALSE(document.hasErrors());
}

TEST(Metascript, ReportedReaderFailureRemainsAnExplicitParseError){
    ReaderArena arena;
    Document document(arena.arena);
    RejectedReader reader(false);

    EXPECT_FALSE(document.parse(reader));
    ASSERT_EQ(document.errors().size(), 1u);
    EXPECT_EQ(document.errors()[0u].message, "read error");
}

TEST(Metascript, SuccessfulZeroReaderCountIsEndOfFileInsteadOfReadFailure){
    ReaderArena arena;
    Document document(arena.arena);
    EndOfFileReader reader;

    ASSERT_TRUE(document.parse(LiteralView("struct Previous{}; number prior;")));
    EXPECT_TRUE(document.parse(reader));
    EXPECT_FALSE(document.hasErrors());
    EXPECT_TRUE(document.declarations().empty());
    EXPECT_TRUE(document.structs().empty());
    EXPECT_EQ(document.findField("prior"), nullptr);
}

TEST(Metascript, ReaderBufferProducesOwnedStructsAndNamedVariables){
    ReaderArena arena;
    Document document(arena.arena);
    ChunkedReader reader(LiteralView("struct Description{ float value; }; Description runtime; runtime.value = 7;"));

    ASSERT_TRUE(document.parse(reader));
    EXPECT_FALSE(document.hasErrors());
    ASSERT_EQ(document.structs().size(), 1u);
    const auto structure = document.structs().find(LiteralView("Description"));
    ASSERT_NE(structure, document.structs().end());
    const Value* fields = structure.value().findField("fields");
    ASSERT_NE(fields, nullptr);
    ASSERT_EQ(fields->asList().size(), 1u);
    EXPECT_EQ(fields->asList()[0u].findField("name")->asString(), "value");
    const auto* declaration = document.findDeclaration("runtime");
    ASSERT_NE(declaration, nullptr);
    EXPECT_EQ(declaration->type, "Description");
    const Value* runtime = document.findField("runtime");
    ASSERT_NE(runtime, nullptr);
    EXPECT_EQ(runtime->findField("value")->asInteger(), 7);
    EXPECT_EQ(document.findField("asset"), nullptr);
}

TEST(Metascript, OversizedReaderResultIsRejectedBeforeUsingItsBuffer){
    ReaderArena arena;
    Document document(arena.arena);
    RejectedReader reader(true);

    EXPECT_FALSE(document.parse(reader));
    ASSERT_EQ(document.errors().size(), 1u);
    EXPECT_EQ(document.errors()[0u].message, "reader returned more bytes than requested");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

