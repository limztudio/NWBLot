// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "common.h"

#include <core/alloc/alloc.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics{
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
    bool runFrame();
    void updateWindowState(u32 width, u32 height, bool windowVisible, bool windowIsInFocus);
    void destroy();
    [[nodiscard]] const SystemMemoryAllocator& getSystemMemoryAllocator()const noexcept{ return m_systemMemoryAllocator; }
    [[nodiscard]] IDeviceManager* getDeviceManager()const noexcept{ return m_deviceManager; }


private:
    PersistentArena m_persistentArena;
    SystemMemoryAllocator m_systemMemoryAllocator;

private:
    DeviceCreationParameters m_deviceCreationParams;

private:
    IDeviceManager* m_deviceManager = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

