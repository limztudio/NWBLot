
#pragma once


#include <global/core/global.h>

#include <global/core/common/module.h>
#include <global/core/alloc/module.h>

#include "../resource_base.h"
#include "../shader_param.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsAPI{
    enum Enum : u8;
};

namespace GraphicsBackend{
    class BackendContext;
    using Backend = BackendContext;
    class Device;
    class Heap;
    class Texture;
    class StagingTexture;
    class SamplerFeedbackTexture;
    class Buffer;
    class Shader;
    class ShaderLibrary;
    class Sampler;
    class InputLayout;
    class Framebuffer;
    class AccelStruct;
    class OpacityMicromap;
    class BindingLayout;
    class BindingSet;
    class DescriptorTable;
    class GraphicsPipeline;
    class ComputePipeline;
    class MeshletPipeline;
    class EventQuery;
    class TimerQuery;
    class ShaderTable;
    class RayTracingPipeline;
    class CommandList;

    using RayTracingOpacityMicromap = OpacityMicromap;
    using RayTracingAccelStruct = AccelStruct;
    using RayTracingShaderTable = ShaderTable;

#define NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Type) \
    u32 RefCountAddReference(Type* value)noexcept; \
    u32 RefCountRelease(Type* value)noexcept; \
    void DestroyArenaReference(Alloc::GlobalArena* arena, Type* value)noexcept;

    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Device)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Heap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Texture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(StagingTexture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(SamplerFeedbackTexture)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Buffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Shader)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(ShaderLibrary)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Sampler)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(InputLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(Framebuffer)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(AccelStruct)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(OpacityMicromap)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(BindingLayout)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(BindingSet)
    NWB_DECLARE_GRAPHICS_REFCOUNT_HOOKS(DescriptorTable)
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
    using Handle = RefCountPtr<T, Alloc::ArenaRefDeleter<T, Alloc::GlobalArena>>;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Device = GraphicsBackend::Device;
using Heap = GraphicsBackend::Heap;
using Texture = GraphicsBackend::Texture;
using StagingTexture = GraphicsBackend::StagingTexture;
using SamplerFeedbackTexture = GraphicsBackend::SamplerFeedbackTexture;
using InputLayout = GraphicsBackend::InputLayout;
using Buffer = GraphicsBackend::Buffer;
using Shader = GraphicsBackend::Shader;
using ShaderLibrary = GraphicsBackend::ShaderLibrary;
using Sampler = GraphicsBackend::Sampler;
using Framebuffer = GraphicsBackend::Framebuffer;
using RayTracingOpacityMicromap = GraphicsBackend::RayTracingOpacityMicromap;
using RayTracingAccelStruct = GraphicsBackend::RayTracingAccelStruct;
using BindingLayout = GraphicsBackend::BindingLayout;
using BindingSet = GraphicsBackend::BindingSet;
using DescriptorTable = GraphicsBackend::DescriptorTable;
using GraphicsPipeline = GraphicsBackend::GraphicsPipeline;
using ComputePipeline = GraphicsBackend::ComputePipeline;
using MeshletPipeline = GraphicsBackend::MeshletPipeline;
using EventQuery = GraphicsBackend::EventQuery;
using TimerQuery = GraphicsBackend::TimerQuery;
using RayTracingShaderTable = GraphicsBackend::RayTracingShaderTable;
using RayTracingPipeline = GraphicsBackend::RayTracingPipeline;
using CommandList = GraphicsBackend::CommandList;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_MaxRenderTargets = 8;
inline constexpr u32 s_MaxViewports = 16;
inline constexpr u32 s_MaxVertexAttributes = 16;
inline constexpr u32 s_MaxBindingLayouts = 8;
inline constexpr u32 s_MaxBindlessRegisterSpaces = 16;
inline constexpr u32 s_MaxVolatileConstantBuffersPerLayout = 6;
inline constexpr u32 s_MaxVolatileConstantBuffers = 32;
inline constexpr u32 s_MaxPushConstantSize = 128;
inline constexpr u32 s_ConstantBufferOffsetSizeAlignment = 256;
inline constexpr u32 s_MaxGpuCrashMarkerStrings = 128;

inline constexpr u32 s_BindingOffsetShaderResource = 0;
inline constexpr u32 s_BindingOffsetSampler = 128;
inline constexpr u32 s_BindingOffsetConstantBuffer = 256;
inline constexpr u32 s_BindingOffsetUnorderedAccess = 384;

inline constexpr i32 s_WindowPositionAuto = -1;
inline constexpr u32 s_BackBufferWidth = 1280;
inline constexpr u32 s_BackBufferHeight = 720;
inline constexpr u32 s_SwapChainBufferCount = 3;
inline constexpr u32 s_MaxFramesInFlight = 2;
inline constexpr f32 s_DepthClearValue = 1.0f;
inline constexpr f64 s_AverageFrameTimeUpdateIntervalSeconds = 0.5;

using GraphicsArena = Alloc::GlobalArena;
using GraphicsString = AString<GraphicsArena>;
using GraphicsTString = TString<GraphicsArena>;
template<typename T>
using GraphicsVector = Vector<T, GraphicsArena>;
using GraphicsBytes = GraphicsVector<u8>;
template<typename T>
using GraphicsSet = Set<T, GraphicsArena>;
template<typename T>
using GraphicsDeque = Deque<T, GraphicsArena>;
template<typename T, typename V>
using GraphicsHashMap = HashMap<T, V, GraphicsArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_GRAPHICS_MASK_OPERATORS(MaskType) \
    constexpr MaskType operator|(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) | static_cast<u32>(rhs)); } \
    constexpr MaskType operator&(MaskType lhs, MaskType rhs)noexcept{ return static_cast<MaskType>(static_cast<u32>(lhs) & static_cast<u32>(rhs)); } \
    constexpr MaskType& operator|=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs | rhs; return lhs; } \
    constexpr MaskType& operator&=(MaskType& lhs, MaskType rhs)noexcept{ lhs = lhs & rhs; return lhs; } \
    constexpr MaskType operator~(MaskType value)noexcept{ return static_cast<MaskType>(~static_cast<u32>(value)); } \
    constexpr bool operator!(MaskType value)noexcept{ return static_cast<u32>(value) == 0; } \
    constexpr bool operator==(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) == static_cast<u32>(rhs); } \
    constexpr bool operator!=(MaskType lhs, MaskType rhs)noexcept{ return static_cast<u32>(lhs) != static_cast<u32>(rhs); }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Graphics allocator


class GraphicsAllocator : NoCopy{
public:
    explicit GraphicsAllocator(Alloc::GlobalArena& objectArena);


public:
    [[nodiscard]] Alloc::GlobalArena& getObjectArena()noexcept{ return m_objectArena; }


private:
    Alloc::GlobalArena& m_objectArena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

