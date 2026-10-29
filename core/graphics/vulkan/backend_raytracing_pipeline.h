// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_pipeline_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderTableRecordKind{
    enum Enum : u8{
        RayGeneration,
        Miss,
        HitGroup,
        Callable,

        Count,
        Invalid = Count,
    };
};

struct ShaderTableGroupMetadata{
    GraphicsString exportName;
    ShaderTableRecordKind::Enum kind = ShaderTableRecordKind::Invalid;
    u32 groupIndex = 0u;


public:
    explicit ShaderTableGroupMetadata(GraphicsArena& arena)noexcept
        : exportName(arena)
    {}
};

class RayTracingPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;
    friend class ShaderTable;


public:
    RayTracingPipeline(const VulkanContext& context, Device& device, b8 allowClusterAccelerationStructures)noexcept;
    ~RayTracingPipeline()noexcept;


public:
    [[nodiscard]] const RayTracingPipelineDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }

    [[nodiscard]] b8 allowsClusterAccelerationStructures()const noexcept{ return m_allowClusterAccelerationStructuresAtCreation; }
    [[nodiscard]] Expected<RayTracingShaderTableHandle> createShaderTable();
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    RayTracingPipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    // vkGetRayTracingShaderGroupHandlesKHR returns tightly packed handles; SBT records add alignment later.
    Vector<u8, Alloc::GlobalArena> m_shaderGroupHandles;
    GraphicsVector<ShaderTableGroupMetadata> m_shaderGroups;

    const b8 m_allowClusterAccelerationStructuresAtCreation;
    const VulkanContext& m_context;
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ShaderTable final : public RefCounter<GraphicsResource>, NoCopy{
    friend class CommandList;
    friend class RayTracingPipeline;


private:
    struct DispatchRegionSnapshot{
        BufferHandle buffer;
        u64 offset = 0u;
        u32 recordCount = 0u;
        usize selectedGroupCount = 0u;
    };

    struct DispatchSnapshot{
        Handle<RayTracingPipeline> pipeline;
        DispatchRegionSnapshot rayGeneration;
        DispatchRegionSnapshot miss;
        DispatchRegionSnapshot hit;
        DispatchRegionSnapshot callable;
    };

    struct ShaderTableAllocation{
        BufferHandle buffer;
        u64 offset = 0u;
    };

    struct ShaderRecordPreflight{
        usize handleOffset = 0u;
        u64 recordByteSize = 0u;
        u64 allocationByteSize = 0u;
        u32 groupIndex = 0u;
        u32 handleSize = 0u;
        u32 handleSizeAligned = 0u;
        u32 baseAlignment = 0u;
    };


public:
    ShaderTable(const VulkanContext& context, Device& device)noexcept;
    ~ShaderTable()noexcept;


public:
    [[nodiscard]] b8 setRayGenerationShader(AStringView exportName);
    [[nodiscard]] Expected<u32> addMissShader(AStringView exportName);
    [[nodiscard]] Expected<u32> addHitGroup(AStringView exportName);
    [[nodiscard]] Expected<u32> addCallableShader(AStringView exportName);
    void clearMissShaders()noexcept;
    void clearHitShaders()noexcept;
    void clearCallableShaders()noexcept;
    [[nodiscard]] RayTracingPipeline& getPipeline()noexcept{ return *m_pipeline; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    [[nodiscard]] DispatchSnapshot captureDispatchSnapshot()const noexcept;
    [[nodiscard]] Expected<u32> findGroupIndex(AStringView exportName, ShaderTableRecordKind::Enum expectedKind, TStringView operationName, TStringView exportKind)const;
    [[nodiscard]] Expected<ShaderRecordPreflight> preflightShaderRecord(AStringView exportName, ShaderTableRecordKind::Enum expectedKind, u32 recordCount, TStringView operationName, TStringView exportKind)const;
    [[nodiscard]] Expected<ShaderTableAllocation> allocateSBTBuffer(const ShaderRecordPreflight& preflight, TStringView operationName, TStringView recordName);
    [[nodiscard]] Expected<u32> appendShaderRecord(
        AStringView exportName,
        ShaderTableRecordKind::Enum expectedKind,
        TStringView operationName,
        TStringView recordName,
        TStringView exportKind,
        GraphicsVector<u32>& groupIndices,
        BufferHandle& buffer,
        u64& offset,
        u32& count
    );


private:
    Handle<RayTracingPipeline> m_pipeline;

    BufferHandle m_raygenBuffer;
    BufferHandle m_missBuffer;
    BufferHandle m_hitBuffer;
    BufferHandle m_callableBuffer;

    GraphicsVector<u32> m_missGroupIndices;
    GraphicsVector<u32> m_hitGroupIndices;
    GraphicsVector<u32> m_callableGroupIndices;

    u64 m_raygenOffset = 0;
    u64 m_missOffset = 0;
    u64 m_hitOffset = 0;
    u64 m_callableOffset = 0;

    mutable Futex m_mutex;

    u32 m_missCount = 0;
    u32 m_hitCount = 0;
    u32 m_callableCount = 0;

    const VulkanContext& m_context;
    Device& m_device;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

