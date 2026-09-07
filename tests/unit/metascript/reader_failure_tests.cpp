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
    [[nodiscard]] virtual isize read(MChar*, usize)override{ throw UnexpectedReadFailure{}; }
};

class RejectedReader final : public IMetaReader{
public:
    explicit RejectedReader(const bool oversized)
        : m_oversized(oversized)
    {}


public:
    [[nodiscard]] virtual isize read(MChar*, const usize maxBytes)override{
        return m_oversized ? static_cast<isize>(maxBytes + 1u) : -1;
    }


private:
    bool m_oversized;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(Metascript, UnexpectedReaderExceptionsPropagateWithoutBecomingParseErrors){
    ReaderArena arena;
    Document document(arena.arena);
    ThrowingReader reader;

    EXPECT_THROW(EXPECT_FALSE(document.parse(reader)), UnexpectedReadFailure);
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

