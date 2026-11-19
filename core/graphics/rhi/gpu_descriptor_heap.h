// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>

#include <global/span.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuDescriptorClass{
    static constexpr u8 s_GpuDescriptorClassSampledImageBase = 0;
    enum Enum : u8{
        SampledImage = s_GpuDescriptorClassSampledImageBase,
        StorageImage,
        SampledBuffer,
        StorageBuffer,
        UniformBuffer,
        AccelStruct,
        Sampler,
        SampledImage2DArray,
        SampledImage3D,
        SampledImage2DArrayUint,
        SampledImageCube,

        Count
    };
};

struct GpuDescriptorHandle{
    static constexpr u32 s_Invalid = 0xFFFFFFFFu;
    static constexpr u32 s_ClassShift = 28u;
    static constexpr u32 s_SlotMask = (1u << s_ClassShift) - 1u;


public:
    u32 value = s_Invalid;


public:
    static constexpr GpuDescriptorHandle Make(GpuDescriptorClass::Enum cls, u32 slot)noexcept{
        return GpuDescriptorHandle((static_cast<u32>(cls) << s_ClassShift) | (slot & s_SlotMask));
    }

    static constexpr GpuDescriptorHandle Invalid()noexcept{ return GpuDescriptorHandle(s_Invalid); }


public:
    constexpr GpuDescriptorHandle()noexcept = default;
    constexpr explicit GpuDescriptorHandle(u32 raw)noexcept : value(raw){}


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{ return value != s_Invalid; }

    [[nodiscard]] constexpr GpuDescriptorClass::Enum descriptorClass()const noexcept{ return static_cast<GpuDescriptorClass::Enum>(value >> s_ClassShift); }

    [[nodiscard]] constexpr u32 slot()const noexcept{ return value & s_SlotMask; }
};
inline constexpr b8 operator==(const GpuDescriptorHandle lhs, const GpuDescriptorHandle rhs)noexcept{ return lhs.value == rhs.value; }

inline constexpr b8 operator!=(const GpuDescriptorHandle lhs, const GpuDescriptorHandle rhs)noexcept{ return lhs.value != rhs.value; }
static constexpr usize s_GpuDescriptorHandleByteSize = 4;
static_assert(sizeof(GpuDescriptorHandle) == s_GpuDescriptorHandleByteSize, "GpuDescriptorHandle is supposed to be a single 32-bit word");

struct GpuDescriptorHeapAbi{
    u32 resourceSetIndex = s_MaxU32;
    u32 samplerSetIndex = s_MaxU32;
    u32 accelStructSetIndex = s_MaxU32;
    u32 sampledImageBinding = s_MaxU32;
    u32 storageImageBinding = s_MaxU32;
    u32 sampledBufferBinding = s_MaxU32;
    u32 storageBufferBinding = s_MaxU32;
    u32 uniformBufferBinding = s_MaxU32;
    u32 sampledImage2DArrayBinding = s_MaxU32;
    u32 sampledImage3DBinding = s_MaxU32;
    u32 sampledImage2DArrayUintBinding = s_MaxU32;
    u32 sampledImageCubeBinding = s_MaxU32;
    u32 samplerBinding = s_MaxU32;
    u32 accelStructBinding = s_MaxU32;


public:
    [[nodiscard]] constexpr b8 valid()const noexcept{
        return
            resourceSetIndex != s_MaxU32
            && samplerSetIndex != s_MaxU32
            && accelStructSetIndex != s_MaxU32
            && sampledImageBinding != s_MaxU32
            && storageImageBinding != s_MaxU32
            && sampledBufferBinding != s_MaxU32
            && storageBufferBinding != s_MaxU32
            && uniformBufferBinding != s_MaxU32
            && sampledImage2DArrayBinding != s_MaxU32
            && sampledImage3DBinding != s_MaxU32
            && sampledImage2DArrayUintBinding != s_MaxU32
            && sampledImageCubeBinding != s_MaxU32
            && samplerBinding != s_MaxU32
            && accelStructBinding != s_MaxU32
        ;
    }
};

struct GpuDescriptorHeapDesc{
    u32 resourceCapacity = 0;
    u32 samplerCapacity = 0;
    GpuDescriptorHeapAbi bindlessHeapAbi;


public:

    constexpr GpuDescriptorHeapDesc& setBindlessHeapAbi(const GpuDescriptorHeapAbi& value)noexcept{ bindlessHeapAbi = value; return *this; }
};

struct GpuDescriptorHeapLifecycleStatistics{
    b8 initialized = false;
    u32 resourceCapacity = 0u;
    u32 samplerCapacity = 0u;
    u32 accelStructCapacity = 0u;
    usize resourceLiveSlotCount = 0u;
    usize samplerLiveSlotCount = 0u;
    usize accelStructLiveSlotCount = 0u;
    usize pendingRetiredSlotCount = 0u;
    usize acceptedHeapUseCount = 0u;
    usize unsubmittedHeapUseCount = 0u;
    usize abandonedHeapUseCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename HeapT>
class GpuDescriptorRetirementGuard final : NoCopy{
public:
    GpuDescriptorRetirementGuard(HeapT& heap, const GpuDescriptorHandle& handle)noexcept
        : m_heap(heap)
        , m_handles(&handle, 1u)
    {}
    GpuDescriptorRetirementGuard(HeapT& heap, const Span<const GpuDescriptorHandle> handles)noexcept
        : m_heap(heap)
        , m_handles(handles)
    {}
    GpuDescriptorRetirementGuard(HeapT&, GpuDescriptorHandle&&) = delete;
    GpuDescriptorRetirementGuard(HeapT&, const GpuDescriptorHandle&&) = delete;
    GpuDescriptorRetirementGuard(const GpuDescriptorRetirementGuard&) = delete;
    GpuDescriptorRetirementGuard(GpuDescriptorRetirementGuard&&) = delete;
    ~GpuDescriptorRetirementGuard()noexcept(noexcept(DeclVal<HeapT&>().free(DeclVal<GpuDescriptorHandle>()))){
        if(!m_active)
            return;
        for(const GpuDescriptorHandle handle : m_handles){
            if(handle.valid())
                m_heap.free(handle);
        }
    }


public:
    GpuDescriptorRetirementGuard& operator=(const GpuDescriptorRetirementGuard&) = delete;

    GpuDescriptorRetirementGuard& operator=(GpuDescriptorRetirementGuard&&) = delete;


public:
    void release()noexcept{ m_active = false; }


private:
    HeapT& m_heap;
    Span<const GpuDescriptorHandle> m_handles;
    b8 m_active = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

