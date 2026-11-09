// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pipe_io.h"

#include <global/termination.h>
#include <global/posix/descriptor.h>
#include <global/posix/pipe_io.h>

#include <fcntl.h>
#include <pthread.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_pipe_io{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_PumpByteBudget = 65536u;
static constexpr usize s_PumpCallBudget = 64u;
static constexpr u32 s_TimeoutMs = 5000u;
static constexpr usize s_ReadChunkBytes = 4096u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ClipboardPipe> OpenClipboardPipe()noexcept{
    Array<int, 2> descriptors{};
    if(pipe(descriptors.data()) != 0)
        return MakeUnexpected(Failure{});
    int readFd = descriptors[0];
    int writeFd = descriptors[1];
    const int writeFlags = fcntl(writeFd, F_GETFD);
    if(ConfigureNonblockingCloseOnExec(readFd) && writeFlags >= 0 && fcntl(writeFd, F_SETFD, writeFlags | FD_CLOEXEC) == 0)
        return ClipboardPipe{ .readFd = readFd, .writeFd = writeFd };
    CloseClipboardPipe(readFd);
    CloseClipboardPipe(writeFd);
    return MakeUnexpected(Failure{});
}

void CloseClipboardPipe(int& fd)noexcept{
    if(fd < 0)
        return;
    const int released = fd;
    fd = -1;
    if(close(released) != 0 && errno != EINTR)
        TerminateInvariant();
}

Expected<b8, ClipboardPipeReadFailure> ReadClipboardPipe(const int fd, ClipboardTextAccumulator& text){
    Array<char, __hidden_pipe_io::s_ReadChunkBytes> bytes;
    usize consumed = 0u;
    usize calls = 0u;
    while(consumed < __hidden_pipe_io::s_PumpByteBudget && calls < __hidden_pipe_io::s_PumpCallBudget){
        ++calls;
        const ssize_t count = read(fd, bytes.data(), bytes.size());
        if(count > 0){
            const ClipboardStatus::Enum status = text.appendBytes(AStringView(bytes.data(), static_cast<usize>(count)));
            if(status != ClipboardStatus::Success)
                return MakeUnexpected(ClipboardPipeReadFailure{ .status = status });
            consumed += static_cast<usize>(count);
        }
        else if(count == 0){
            const auto status = ValidateClipboardUtf8Text(text.text());
            if(status != ClipboardStatus::Success)
                return MakeUnexpected(ClipboardPipeReadFailure{ .status = status, .finished = true });
            return true;
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK)
            return false;
        else if(errno != EINTR)
            return MakeUnexpected(ClipboardPipeReadFailure{});
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardPipeWriter::ClipboardPipeWriter(Alloc::GlobalArena& arena)noexcept
    : m_text(arena)
{}

ClipboardPipeWriter::~ClipboardPipeWriter()noexcept{
    CloseClipboardPipe(m_fd);
}

b8 ClipboardPipeWriter::begin(const int fd, const AStringView text){
    FOUNDATION_FATAL_ASSERT(m_fd < 0);
    m_text.clear();
    m_offset = 0u;
    m_fd = fd;
    if(fd < 0 || !ConfigureNonblockingCloseOnExec(fd) || ValidateClipboardUtf8Text(text) != ClipboardStatus::Success){
        CloseClipboardPipe(m_fd);
        return false;
    }
    if(!text.empty())
        m_text.assign(text.data(), text.size());
    m_deadline = TimerAddMS(TimerNow(), __hidden_pipe_io::s_TimeoutMs);
    return true;
}

ClipboardStatus::Enum ClipboardPipeWriter::advance()noexcept{
    if(m_fd < 0)
        return ClipboardStatus::Success;
    if(TimerNow() >= m_deadline){
        CloseClipboardPipe(m_fd);
        return ClipboardStatus::Unavailable;
    }
    usize consumed = 0u;
    usize calls = 0u;
    while(m_offset < m_text.size() && consumed < __hidden_pipe_io::s_PumpByteBudget && calls < __hidden_pipe_io::s_PumpCallBudget){
        ++calls;
        const usize size = Min(m_text.size() - m_offset, __hidden_pipe_io::s_PumpByteBudget - consumed);
        const ssize_t count = WriteWithoutSigpipe(m_fd, AStringView(m_text).substr(m_offset, size));
        if(count > 0){
            m_offset += static_cast<usize>(count);
            consumed += static_cast<usize>(count);
        }
        else if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            return ClipboardStatus::Success;
        else if(count < 0 && errno == EINTR)
            continue;
        else{
            CloseClipboardPipe(m_fd);
            return ClipboardStatus::NativeFailure;
        }
    }
    if(m_offset == m_text.size())
        CloseClipboardPipe(m_fd);
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

