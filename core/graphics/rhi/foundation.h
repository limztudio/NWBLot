// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/global.h>
#include <core/common/module.h>
#include <core/alloc/module.h>

#include "../resource_base.h"
#include "shader_param.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend{
    class BackendContext;
    using Backend = BackendContext;
    class Device;
    class Heap;
    class Texture;
    class StagingTexture;
    class Buffer;
    class Shader;
    class ShaderLibrary;
    class Sampler;
    class InputLayout;
    class Framebuffer;
    class AccelStruct;
    class OpacityMicromap;
    class BindingLayout;
    class GraphicsPipeline;
    class ComputePipeline;
    class MeshletPipeline;
    class EventQuery;
    class TimerQuery;
    class ShaderTable;
    class RayTracingPipeline;
    class CommandList;
    class GpuDescriptorHeap;   // Device-owned global descriptor heap; not a refcounted GraphicsResource.

    using RayTracingOpacityMicromap = OpacityMicromap;
    using RayTracingAccelStruct = AccelStruct;
    using RayTracingShaderTable = ShaderTable;

#define NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Type) \
    u32 RefCountAddReference(Type* value)noexcept; \
    u32 RefCountRelease(Type* value)noexcept; \
    void DestroyArenaReference(Alloc::GlobalArena& arena, Type& value)noexcept;

    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Device)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Heap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Texture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(StagingTexture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Buffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Shader)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ShaderLibrary)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Sampler)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(InputLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Framebuffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(AccelStruct)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(OpacityMicromap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(BindingLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(GraphicsPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ComputePipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(MeshletPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(EventQuery)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(TimerQuery)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ShaderTable)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(RayTracingPipeline)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(CommandList)

#undef NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS

    template<typename T>
    using Handle = RefCountPtr<T, ::ArenaRefDeleter<T, Alloc::GlobalArena>>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Device = GraphicsBackend::Device;
using Heap = GraphicsBackend::Heap;
using Texture = GraphicsBackend::Texture;
using StagingTexture = GraphicsBackend::StagingTexture;
using InputLayout = GraphicsBackend::InputLayout;
using Buffer = GraphicsBackend::Buffer;
using Shader = GraphicsBackend::Shader;
using ShaderLibrary = GraphicsBackend::ShaderLibrary;
using Sampler = GraphicsBackend::Sampler;
using Framebuffer = GraphicsBackend::Framebuffer;
using RayTracingOpacityMicromap = GraphicsBackend::RayTracingOpacityMicromap;
using RayTracingAccelStruct = GraphicsBackend::RayTracingAccelStruct;
using BindingLayout = GraphicsBackend::BindingLayout;
using GraphicsPipeline = GraphicsBackend::GraphicsPipeline;
using ComputePipeline = GraphicsBackend::ComputePipeline;
using MeshletPipeline = GraphicsBackend::MeshletPipeline;
using EventQuery = GraphicsBackend::EventQuery;
using TimerQuery = GraphicsBackend::TimerQuery;
using RayTracingShaderTable = GraphicsBackend::RayTracingShaderTable;
using RayTracingPipeline = GraphicsBackend::RayTracingPipeline;
using CommandList = GraphicsBackend::CommandList;
using GpuDescriptorHeap = GraphicsBackend::GpuDescriptorHeap;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MaxRenderTargets = 8;
inline constexpr u32 s_MaxViewports = 16;
inline constexpr u32 s_MaxVertexAttributes = 16;
inline constexpr u32 s_MaxBindingLayouts = 8;
inline constexpr u32 s_MaxBindlessRegisterSpaces = 16;
inline constexpr u32 s_MaxPushConstantSize = 128;
inline constexpr u32 s_PushConstantSizeAlignment = 4;
inline constexpr u32 s_BackBufferWidth = 1280;
inline constexpr u32 s_BackBufferHeight = 720;
inline constexpr u32 s_SwapChainBufferCount = 3;
inline constexpr u32 s_MaxFramesInFlight = 2;
inline constexpr f32 s_DepthClearValue = 1.0f;

using GraphicsArena = Alloc::GlobalArena;
using GraphicsString = AString<GraphicsArena>;
using GraphicsTString = TString<GraphicsArena>;
template<typename T>
using GraphicsVector = Vector<T, GraphicsArena>;
using GraphicsBytes = GraphicsVector<u8>;
template<typename T>
using GraphicsDeque = Deque<T, GraphicsArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_GRAPHICS_MASK_OPERATORS(MaskType) \
    constexpr MaskType operator|(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); } \
    constexpr MaskType operator&(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); } \
    constexpr MaskType operator^(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) ^ static_cast<u32>(rhs)); } \
    constexpr MaskType& operator|=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs | rhs; return lhs; } \
    constexpr MaskType& operator&=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs & rhs; return lhs; } \
    constexpr MaskType& operator^=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs ^ rhs; return lhs; } \
    constexpr MaskType operator~(MaskType value)noexcept{ return static_cast<MaskType>(~static_cast<u32>(value)); } \
    constexpr b8 operator!(MaskType value)noexcept{ return static_cast<u32>(value) == 0; } \
    constexpr b8 operator==(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); } \
    constexpr b8 operator!=(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsAllocator : NoCopy{
public:
    explicit GraphicsAllocator(Alloc::GlobalArena& objectArena)noexcept
        : m_objectArena(objectArena)
    {}


public:
    [[nodiscard]] Alloc::GlobalArena& getObjectArena()noexcept{ return m_objectArena; }


private:
    Alloc::GlobalArena& m_objectArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Container>
concept GpuContiguousContainer = requires(const RemoveCVRef_T<Container>& container){
    { container.data() };
    static_cast<usize>(container.size());
} && IsPointer_V<decltype(static_cast<const RemoveCVRef_T<Container>*>(nullptr)->data())>;

template<typename Container>
using GpuContiguousElement_T = RemoveCV_T<RemovePointer_T<decltype(static_cast<const RemoveCVRef_T<Container>*>(nullptr)->data())>>;

template<typename Container>
concept GpuContiguousPodContainer = GpuContiguousContainer<Container>
    && !IsSame_V<void, GpuContiguousElement_T<Container>>
    && IsTriviallyCopyable_V<GpuContiguousElement_T<Container>>;

template<typename Container, typename Element>
concept GpuContiguousTypedContainer = GpuContiguousContainer<Container>
    && IsSame_V<GpuContiguousElement_T<Container>, Element>;

template<typename Container>
concept GpuMutableContiguousPodContainer = GpuContiguousPodContainer<Container>
    && IsPointer_V<decltype(static_cast<RemoveReference_T<Container>*>(nullptr)->data())>
    && !IsConst_V<RemovePointer_T<decltype(static_cast<RemoveReference_T<Container>*>(nullptr)->data())>>;

template<typename T>
concept GpuPodValue = !IsPointer_V<RemoveCVRef_T<T>>
    && !IsSame_V<RemoveCVRef_T<T>, decltype(nullptr)>
    && IsTriviallyCopyable_V<RemoveCVRef_T<T>>
    && !requires(const RemoveCVRef_T<T>& container){
        container.data();
        container.size();
    };

template<GpuContiguousContainer Container>
[[nodiscard]] constexpr usize ContiguousContainerByteSize(const Container& container)noexcept{
    return static_cast<usize>(container.size()) * sizeof(GpuContiguousElement_T<Container>);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

