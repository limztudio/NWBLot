// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "pipe_io.h"

#include <global/termination.h>

#include <cerrno>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_clipboard_pipe{
static constexpr usize s_PumpByteBudget = 65536u;
static constexpr usize s_PumpCallBudget = 64u;
static constexpr u32 s_TimeoutMs = 5000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static ssize_t WriteWithoutSigpipe(const int fd, const AStringView bytes){
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    if(
        sigemptyset(&blocked) != 0
        || sigaddset(&blocked, SIGPIPE) != 0
        || pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0
        || sigpending(&pending) != 0
    )
        TerminateInvariant();
    const bool alreadyPending = sigismember(&pending, SIGPIPE) == 1;
    const ssize_t count = write(fd, bytes.data(), bytes.size());
    const int writeError = errno;
    if(count < 0 && writeError == EPIPE && !alreadyPending){
        const timespec timeout{};
        int signal = -1;
        do{
            signal = sigtimedwait(&blocked, nullptr, &timeout);
        } while(signal < 0 && errno == EINTR);
        if(signal < 0 && errno != EAGAIN)
            TerminateInvariant();
    }
    if(pthread_sigmask(SIG_SETMASK, &previous, nullptr) != 0)
        TerminateInvariant();
    errno = writeError;
    return count;
}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool OpenClipboardPipe(int& readFd, int& writeFd){
    int descriptors[2]{ -1, -1 };
    if(pipe(descriptors) != 0)
        return false;
    readFd = descriptors[0];
    writeFd = descriptors[1];
    const int writeFlags = fcntl(writeFd, F_GETFD);
    if(ConfigureClipboardPipe(readFd) && writeFlags >= 0 && fcntl(writeFd, F_SETFD, writeFlags | FD_CLOEXEC) == 0)
        return true;
    CloseClipboardPipe(readFd);
    CloseClipboardPipe(writeFd);
    return false;
}

bool ConfigureClipboardPipe(const int fd){
    const int flags = fcntl(fd, F_GETFL);
    const int descriptorFlags = fcntl(fd, F_GETFD);
    return
        flags >= 0 && descriptorFlags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0
        && fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0
    ;
}

void CloseClipboardPipe(int& fd){
    if(fd < 0)
        return;
    const int released = fd;
    fd = -1;
    if(close(released) != 0 && errno != EINTR)
        TerminateInvariant();
}

ClipboardStatus::Enum ReadClipboardPipe(const int fd, ClipboardTextAccumulator& text, bool& finished){
    finished = false;
    Array<char, 4096u> bytes{};
    usize consumed = 0u;
    usize calls = 0u;
    while(consumed < __hidden_clipboard_pipe::s_PumpByteBudget && calls < __hidden_clipboard_pipe::s_PumpCallBudget){
        ++calls;
        const ssize_t count = read(fd, bytes.data(), bytes.size());
        if(count > 0){
            const ClipboardStatus::Enum status = text.appendBytes(AStringView(bytes.data(), static_cast<usize>(count)));
            if(status != ClipboardStatus::Success)
                return status;
            consumed += static_cast<usize>(count);
        }
        else if(count == 0){
            finished = true;
            return ValidateClipboardUtf8Text(text.text());
        }
        else if(errno == EAGAIN || errno == EWOULDBLOCK)
            return ClipboardStatus::Success;
        else if(errno != EINTR)
            return ClipboardStatus::NativeFailure;
    }
    return ClipboardStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ClipboardPipeWriter::ClipboardPipeWriter(Alloc::GlobalArena& arena)
    : m_text(arena)
{}

ClipboardPipeWriter::~ClipboardPipeWriter(){
    CloseClipboardPipe(m_fd);
}

bool ClipboardPipeWriter::begin(const int fd, const AStringView text){
    GLOBAL_FATAL_ASSERT(m_fd < 0);
    m_text.clear();
    m_offset = 0u;
    m_fd = fd;
    if(fd < 0 || !ConfigureClipboardPipe(fd) || ValidateClipboardUtf8Text(text) != ClipboardStatus::Success){
        CloseClipboardPipe(m_fd);
        return false;
    }
    if(!text.empty())
        m_text.assign(text.data(), text.size());
    m_deadline = TimerAddMS(TimerNow(), __hidden_clipboard_pipe::s_TimeoutMs);
    return true;
}

ClipboardStatus::Enum ClipboardPipeWriter::advance(){
    if(m_fd < 0)
        return ClipboardStatus::Success;
    if(TimerNow() >= m_deadline){
        CloseClipboardPipe(m_fd);
        return ClipboardStatus::Unavailable;
    }
    usize consumed = 0u;
    usize calls = 0u;
    while(
        m_offset < m_text.size()
        && consumed < __hidden_clipboard_pipe::s_PumpByteBudget
        && calls < __hidden_clipboard_pipe::s_PumpCallBudget
    ){
        ++calls;
        const usize size = Min(m_text.size() - m_offset, __hidden_clipboard_pipe::s_PumpByteBudget - consumed);
        const ssize_t count = __hidden_clipboard_pipe::WriteWithoutSigpipe(m_fd, AStringView(m_text).substr(m_offset, size));
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

