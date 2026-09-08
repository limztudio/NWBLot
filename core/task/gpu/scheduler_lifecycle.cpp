// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <core/graphics/backend_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskScheduler::DeviceOperation::DeviceOperation(const GpuTaskScheduler& scheduler)noexcept
    : m_scheduler(scheduler)
{
    NothrowScopedLock lock(m_scheduler.m_lifecycleMutex);

    if(!m_scheduler.m_device)
        return;
    ++m_scheduler.m_activeOperations;
    m_admitted = true;
}

GpuTaskScheduler::DeviceOperation::~DeviceOperation()noexcept{
    if(!m_admitted)
        return;
    NothrowScopedLock lock(m_scheduler.m_lifecycleMutex);

    NWB_ASSERT(m_scheduler.m_activeOperations != 0u);
    --m_scheduler.m_activeOperations;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::attachDevice(Device& device)noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    if(m_device)
        return m_device == &device;
    m_device = &device;
    return true;
}

bool GpuTaskScheduler::detachDevice(Device& device)noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    if(m_device != &device || m_activeOperations != 0u)
        return false;
    m_device = nullptr;
    return true;
}

bool GpuTaskScheduler::isAttachedTo(const Device& device)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    return m_device == &device;
}

bool GpuTaskScheduler::isInitialized()const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    return m_device != nullptr;
}

bool GpuTaskScheduler::wait()const{
    DeviceOperation operation(*this);

    return !operation.m_admitted || device().waitForIdle();
}

bool GpuTaskScheduler::wait(const QueueSubmissionToken& token)const{
    DeviceOperation operation(*this);

    return operation.m_admitted && device().waitForSubmissionToken(token);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device& GpuTaskScheduler::device()const noexcept{
    NWB_ASSERT(m_device);
    return *m_device;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

