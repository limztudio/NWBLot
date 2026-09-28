// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/linux/pipe_io.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_pipe_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

struct OwnedPipe final : NoCopy{
    int m_readFd = -1;
    int m_writeFd = -1;

    ~OwnedPipe(){
        CloseClipboardPipe(m_readFd);
        CloseClipboardPipe(m_writeFd);
    }

    [[nodiscard]] bool open(){ return OpenClipboardPipe(m_readFd, m_writeFd); }

    [[nodiscard]] int releaseWrite(){
        const int fd = m_writeFd;
        m_writeFd = -1;
        return fd;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ClipboardPipe, OnlyOwnedReaderIsNonblockingAndBothDescriptorsCloseOnExec){
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    const int readFlags = fcntl(pipe.m_readFd, F_GETFL);
    const int writeFlags = fcntl(pipe.m_writeFd, F_GETFL);
    ASSERT_GE(readFlags, 0);
    ASSERT_GE(writeFlags, 0);
    EXPECT_NE(readFlags & O_NONBLOCK, 0);
    EXPECT_EQ(writeFlags & O_NONBLOCK, 0);
    const int readDescriptorFlags = fcntl(pipe.m_readFd, F_GETFD);
    const int writeDescriptorFlags = fcntl(pipe.m_writeFd, F_GETFD);
    ASSERT_GE(readDescriptorFlags, 0);
    ASSERT_GE(writeDescriptorFlags, 0);
    EXPECT_NE(readDescriptorFlags & FD_CLOEXEC, 0);
    EXPECT_NE(writeDescriptorFlags & FD_CLOEXEC, 0);
}

TEST(ClipboardPipe, PartialUnicodeStaysPendingUntilValidEof){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    ClipboardTextAccumulator received(arena.arena);
    bool finished = false;
    ASSERT_EQ(write(pipe.m_writeFd, "\xED", 1u), 1);
    EXPECT_EQ(ReadClipboardPipe(pipe.m_readFd, received, finished), ClipboardStatus::Success);
    EXPECT_FALSE(finished);
    ASSERT_EQ(write(pipe.m_writeFd, "\x95\x9C\xF0\x9F", 4u), 4);
    EXPECT_EQ(ReadClipboardPipe(pipe.m_readFd, received, finished), ClipboardStatus::Success);
    EXPECT_FALSE(finished);
    ASSERT_EQ(write(pipe.m_writeFd, "\x98\x80", 2u), 2);
    CloseClipboardPipe(pipe.m_writeFd);
    EXPECT_EQ(ReadClipboardPipe(pipe.m_readFd, received, finished), ClipboardStatus::Success);
    EXPECT_TRUE(finished);
    EXPECT_EQ(received.text(), "\xED\x95\x9C\xF0\x9F\x98\x80");
}

TEST(ClipboardPipe, TruncatedUnicodeIsRejectedAtEof){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    ClipboardTextAccumulator received(arena.arena);
    ASSERT_EQ(write(pipe.m_writeFd, "\xF0\x9F", 2u), 2);
    CloseClipboardPipe(pipe.m_writeFd);
    bool finished = false;
    EXPECT_EQ(ReadClipboardPipe(pipe.m_readFd, received, finished), ClipboardStatus::InvalidText);
    EXPECT_TRUE(finished);
}

TEST(ClipboardPipe, WriterOwnsLargeInputAndResumesAfterFullPipe){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    AString<Alloc::GlobalArena> input(2u * 1024u * 1024u, 'x', arena.arena);
    const AString<Alloc::GlobalArena> expected = input;
    ClipboardPipeWriter writer(arena.arena);
    const int writeFd = pipe.releaseWrite();
    ASSERT_TRUE(writer.begin(writeFd, input));
    input.assign("caller changed bytes");
    const int writerFlags = fcntl(writeFd, F_GETFL);
    ASSERT_GE(writerFlags, 0);
    ASSERT_NE(writerFlags & O_NONBLOCK, 0);
    for(usize attempt = 0u; attempt < 64u; ++attempt)
        ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_FALSE(writer.finished());
    pollfd writable{ .fd = writeFd, .events = POLLOUT, .revents = 0 };
    ASSERT_EQ(poll(&writable, 1u, 0), 0);

    ClipboardTextAccumulator received(arena.arena);
    bool finished = false;
    for(usize attempt = 0u; attempt < 4096u && !finished; ++attempt){
        ASSERT_EQ(ReadClipboardPipe(pipe.m_readFd, received, finished), ClipboardStatus::Success);
        ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    }
    EXPECT_TRUE(finished);
    EXPECT_TRUE(writer.finished());
    EXPECT_EQ(received.text(), expected);
}

TEST(ClipboardPipe, ClosedReaderFailsWithoutSigpipeOrChangingThreadMask){
    NWB::Tests::TestArena arena;
    OwnedPipe pipe;
    ASSERT_TRUE(pipe.open());
    CloseClipboardPipe(pipe.m_readFd);
    sigset_t before;
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &before), 0);
    ClipboardPipeWriter writer(arena.arena);
    ASSERT_TRUE(writer.begin(pipe.releaseWrite(), "reader is gone"));
    EXPECT_EQ(writer.advance(), ClipboardStatus::NativeFailure);
    EXPECT_TRUE(writer.finished());
    sigset_t after;
    ASSERT_EQ(pthread_sigmask(SIG_BLOCK, nullptr, &after), 0);
    EXPECT_EQ(sigismember(&before, SIGPIPE), sigismember(&after, SIGPIPE));
}

TEST(ClipboardPipe, WriterCanBeReusedForEmptyAndShorterTransfers){
    NWB::Tests::TestArena arena;
    ClipboardPipeWriter writer(arena.arena);
    ClipboardTextAccumulator received(arena.arena);
    bool finished = false;
    OwnedPipe first;
    ASSERT_TRUE(first.open());
    ASSERT_TRUE(writer.begin(first.releaseWrite(), "old long contents"));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    ASSERT_EQ(ReadClipboardPipe(first.m_readFd, received, finished), ClipboardStatus::Success);
    ASSERT_TRUE(finished);
    EXPECT_EQ(received.text(), "old long contents");

    OwnedPipe empty;
    ASSERT_TRUE(empty.open());
    ASSERT_TRUE(writer.begin(empty.releaseWrite(), {}));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    received.clear();
    ASSERT_EQ(ReadClipboardPipe(empty.m_readFd, received, finished), ClipboardStatus::Success);
    EXPECT_TRUE(finished);
    EXPECT_TRUE(received.text().empty());

    OwnedPipe last;
    ASSERT_TRUE(last.open());
    ASSERT_TRUE(writer.begin(last.releaseWrite(), "new"));
    ASSERT_EQ(writer.advance(), ClipboardStatus::Success);
    ASSERT_TRUE(writer.finished());
    received.clear();
    ASSERT_EQ(ReadClipboardPipe(last.m_readFd, received, finished), ClipboardStatus::Success);
    EXPECT_TRUE(finished);
    EXPECT_EQ(received.text(), "new");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

