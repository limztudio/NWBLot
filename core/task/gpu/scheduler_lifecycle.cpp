// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuTaskScheduler::DeviceOperation::DeviceOperation(const GpuTaskScheduler& scheduler)noexcept
    : m_scheduler(scheduler)
{
    u32 state = m_scheduler.m_deviceLifecycleState.load(MemoryOrder::acquire);
    while((state & s_DeviceAttachedBit) != 0u){
        FOUNDATION_FATAL_ASSERT_MSG((state & s_DeviceActiveOpMask) < s_DeviceActiveOpMask, "GPU task scheduler active device operation count overflowed");
        if(m_scheduler.m_deviceLifecycleState.compare_exchange_weak(state, state + 1u, MemoryOrder::acq_rel, MemoryOrder::acquire)){
            m_admitted = true;
            return;
        }
    }
}

GpuTaskScheduler::DeviceOperation::~DeviceOperation()noexcept{
    if(!m_admitted)
        return;

    const u32 previous = m_scheduler.m_deviceLifecycleState.fetch_sub(1u, MemoryOrder::release);
    FOUNDATION_FATAL_ASSERT_MSG((previous & s_DeviceAttachedBit) != 0u && (previous & s_DeviceActiveOpMask) != 0u, "GPU task scheduler released an invalid device operation");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 GpuTaskScheduler::attachDevice(Device& device)noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    if(m_device)
        return m_device == &device;
    m_device = &device;
    m_deviceLifecycleState.store(s_DeviceAttachedBit, MemoryOrder::release);
    return true;
}

b8 GpuTaskScheduler::detachDevice(Device& device)noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    if(m_device != &device)
        return false;
    u32 expected = s_DeviceAttachedBit;
    if(!m_deviceLifecycleState.compare_exchange_strong(expected, 0u, MemoryOrder::acq_rel, MemoryOrder::acquire))
        return false;
    m_device = nullptr;
    return true;
}

b8 GpuTaskScheduler::isAttachedTo(const Device& device)const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    return m_device == &device;
}

b8 GpuTaskScheduler::isInitialized()const noexcept{
    NothrowScopedLock lock(m_lifecycleMutex);

    return m_device != nullptr;
}

b8 GpuTaskScheduler::wait()const{
    DeviceOperation operation(*this);

    return !operation.m_admitted || device().waitForIdle();
}

b8 GpuTaskScheduler::wait(const QueueSubmissionToken& token)const{
    DeviceOperation operation(*this);

    return operation.m_admitted && device().waitForSubmissionToken(token);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Device& GpuTaskScheduler::device()const noexcept{
    FOUNDATION_ASSERT(m_device);
    return *m_device;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

