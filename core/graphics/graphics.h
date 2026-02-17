// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"

#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanEngine;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
    friend VulkanEngine;


private:
    template<typename T>
    using PersistentAllocator = Alloc::MemoryAllocator<T>;
    using PersistentArena = Alloc::MemoryArena;


public:
    static constexpr const u32 s_maxDynamicAllocSize = 64 * 1024 * 1024;


private:
    static void* allocatePersistentSystemMemory(void* userData, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope);
    static void* reallocatePersistentSystemMemory(void* userData, void* original, usize size, usize alignment, SystemMemoryAllocationScope::Enum scope);
    static void freePersistentSystemMemory(void* userData, void* memory);


public:
    Graphics();
    ~Graphics();


public:
    bool init(const Common::FrameData& data);
    void destroy();
    [[nodiscard]] const SystemMemoryAllocator& getSystemMemoryAllocator()const noexcept{ return m_systemMemoryAllocator; }


private:
    PersistentArena m_persistentArena;
    SystemMemoryAllocator m_systemMemoryAllocator;

private:
    u16 m_swapchainWidth = 0;
    u16 m_swapchainHeight = 0;
    u8 m_swapchainImageCount = 0;

    u16 m_gpuTimeQueriesPerFrame = 32;
    bool m_gpuTimeQueriesEnabled = false;

private:
    DeviceHandle m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

