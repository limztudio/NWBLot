// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/os/clipboard_text.h>

#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ClipboardPipe{
    int readFd = -1;
    int writeFd = -1;
};

struct ClipboardPipeReadFailure{
    ClipboardStatus::Enum status = ClipboardStatus::NativeFailure;
    bool finished = false;
};

[[nodiscard]] Expected<ClipboardPipe> OpenClipboardPipe()noexcept;
[[nodiscard]] bool ConfigureClipboardPipe(int fd)noexcept;
void CloseClipboardPipe(int& fd)noexcept;
[[nodiscard]] Expected<bool, ClipboardPipeReadFailure> ReadClipboardPipe(int fd, ClipboardTextAccumulator& text);


class ClipboardPipeWriter final : private NoCopy{
public:
    explicit ClipboardPipeWriter(Alloc::GlobalArena& arena);
    ~ClipboardPipeWriter()noexcept;


public:
    [[nodiscard]] bool begin(int fd, AStringView text);
    [[nodiscard]] ClipboardStatus::Enum advance()noexcept;
    [[nodiscard]] bool finished()const noexcept{ return m_fd < 0; }


private:
    AString<Alloc::GlobalArena> m_text;
    int m_fd = -1;
    usize m_offset = 0u;
    Timer m_deadline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

