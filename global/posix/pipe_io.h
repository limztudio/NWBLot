// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../basic_string.h"
#include "../termination.h"

#include <cerrno>
#include <csignal>
#include <ctime>
#include <pthread.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline ssize_t WriteWithoutSigpipe(const int fd, const AStringView bytes)noexcept{
    sigset_t blocked;
    sigset_t previous;
    sigset_t pending;
    if(sigemptyset(&blocked) != 0 || sigaddset(&blocked, SIGPIPE) != 0 || pthread_sigmask(SIG_BLOCK, &blocked, &previous) != 0 || sigpending(&pending) != 0)
        TerminateInvariant();
    const b8 alreadyPending = sigismember(&pending, SIGPIPE) == 1;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

