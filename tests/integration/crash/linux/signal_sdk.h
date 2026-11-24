// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/array.h>
#include <global/exception.h>
#include <global/termination.h>
#include <signal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::PosixSignals{

class AlternateStack{
public:
    [[nodiscard]] b8 read()noexcept{ return ::sigaltstack(nullptr, &m_value) == 0; }
    [[nodiscard]] b8 install()const noexcept{ return ::sigaltstack(&m_value, nullptr) == 0; }
    void disable()noexcept{ m_value.ss_flags = SS_DISABLE; }
    [[nodiscard]] b8 disabled()const noexcept{ return (m_value.ss_flags & SS_DISABLE) != 0; }
    [[nodiscard]] usize address()const noexcept{ return reinterpret_cast<usize>(m_value.ss_sp); }

    template<usize N>
    void use(Array<u8, N>& storage)noexcept{
        m_value.ss_sp = storage.data();
        m_value.ss_size = storage.size();
        m_value.ss_flags = 0;
    }

private:
    stack_t m_value = {};
};

class SignalAction{
public:
    [[nodiscard]] b8 read(const int number)noexcept{ return ::sigaction(number, nullptr, &m_value) == 0; }
    [[nodiscard]] b8 install(const int number)const noexcept{ return ::sigaction(number, &m_value, nullptr) == 0; }
    [[nodiscard]] b8 clearMask()noexcept{ return ::sigemptyset(&m_value.sa_mask) == 0; }
    void setHandler(void (*handler)(int))noexcept{ m_value.sa_handler = handler; }
    [[nodiscard]] b8 matches(void (*handler)(int))const noexcept{ return m_value.sa_handler == handler; }

private:
    struct sigaction m_value = {};
};

struct alignas(MaxAlign) AlternateSignalStackStorage : Array<u8, 256u * 1024u>{};

[[nodiscard]] inline b8 TerminateMatches(TerminateHandler handler)noexcept{
    return QueryTerminateHandler() == handler;
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

