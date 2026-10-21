// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuCompiledGraph;
class GpuTaskGraph;
class GpuGraphSubmissionTransaction;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuGraphSubmissionBinding final{
    friend class GpuCompiledGraph;
    friend class GpuTaskGraph;
    friend class GpuGraphSubmissionTransaction;
    friend bool operator==(const GpuGraphSubmissionBinding& lhs, const GpuGraphSubmissionBinding& rhs)noexcept;


public:
    GpuGraphSubmissionBinding()noexcept = default;
private:
    GpuGraphSubmissionBinding(const u64 transactionIdentity, const u64 resetGeneration)noexcept
        : m_transactionIdentity(transactionIdentity)
        , m_resetGeneration(resetGeneration)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{
        return m_transactionIdentity != 0u && m_resetGeneration != 0u;
    }


private:
    u64 m_transactionIdentity = 0u;
    u64 m_resetGeneration = 0u;
};


[[nodiscard]] inline bool operator==(const GpuGraphSubmissionBinding& lhs, const GpuGraphSubmissionBinding& rhs)noexcept{
    return lhs.m_transactionIdentity == rhs.m_transactionIdentity && lhs.m_resetGeneration == rhs.m_resetGeneration;
}

[[nodiscard]] inline bool operator!=(const GpuGraphSubmissionBinding& lhs, const GpuGraphSubmissionBinding& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

