// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/alloc/scratch.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{
    struct MeshRayTracingResourceSnapshot;
};


namespace PreparedShadowTraceGeometryRole{
    inline constexpr u8 s_HardwarePosition = 1u << 0u;
    inline constexpr u8 s_HardwareIndex = 1u << 1u;
    inline constexpr u8 s_HardwareAttribute = 1u << 2u;
    inline constexpr u8 s_SoftwareNode = 1u << 3u;
    inline constexpr u8 s_SoftwarePosition = 1u << 4u;
    inline constexpr u8 s_SoftwareIndex = 1u << 5u;
    inline constexpr u8 s_SoftwareAttribute = 1u << 6u;
};

struct PreparedShadowTraceGeometryBuffer{
    Core::BufferHandle buffer;
    Name identity;
    Core::ResourceStates::Mask initialState = Core::ResourceStates::Common;
    u8 roles = 0u;
    bool normalizationPending = false;
};

using PreparedShadowTraceGeometryBufferVector = Vector<PreparedShadowTraceGeometryBuffer, Core::Alloc::GlobalArena>;

struct ShadowTraceGeometrySelection{
    Span<const NotNull<Core::Buffer*>> hardwarePositions{};
    Span<const NotNull<Core::Buffer*>> hardwareIndices{};
    Span<const NotNull<Core::Buffer*>> hardwareAttributes{};
    Span<const NotNull<Core::Buffer*>> softwareNodes{};
    Span<const NotNull<Core::Buffer*>> softwarePositions{};
    Span<const NotNull<Core::Buffer*>> softwareIndices{};
    Span<const NotNull<Core::Buffer*>> softwareAttributes{};
    bool includeHardware = false;
    bool includeSoftware = false;
};


[[nodiscard]] bool FreezePreparedShadowTraceGeometryBuffers(
    Span<const ECSRenderDetail::MeshRayTracingResourceSnapshot> meshes,
    const ShadowTraceGeometrySelection& selection,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedBuffers,
    PreparedShadowTraceGeometryBufferVector& outPrepared
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

