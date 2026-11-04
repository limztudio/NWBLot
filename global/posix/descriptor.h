// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../type.h"

#include <fcntl.h>
#include <unistd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline b8 SetCloseOnExec(const int descriptor)noexcept{
    const int flags = fcntl(descriptor, F_GETFD, 0);
    return flags >= 0 && fcntl(descriptor, F_SETFD, flags | FD_CLOEXEC) == 0;
}

[[nodiscard]] inline b8 MoveFdAboveStdio(int& descriptor, const int firstNonStdio)noexcept{
    if(descriptor > STDERR_FILENO)
        return true;
    const int duplicated = fcntl(descriptor, F_DUPFD, firstNonStdio);
    if(duplicated < 0)
        return false;
    const int owned = descriptor;
    const int result = close(owned);
    descriptor = duplicated;
    return result == 0;
}

[[nodiscard]] inline b8 ConfigureNonblockingCloseOnExec(const int fd)noexcept{
    const int flags = fcntl(fd, F_GETFL);
    const int descriptorFlags = fcntl(fd, F_GETFD);
    return flags >= 0 && descriptorFlags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0 && fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) == 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

