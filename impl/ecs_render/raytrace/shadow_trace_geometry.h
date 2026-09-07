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
    inline constexpr u8 HardwarePosition = 1u << 0u;
    inline constexpr u8 HardwareIndex = 1u << 1u;
    inline constexpr u8 HardwareAttribute = 1u << 2u;
    inline constexpr u8 SoftwareNode = 1u << 3u;
    inline constexpr u8 SoftwarePosition = 1u << 4u;
    inline constexpr u8 SoftwareIndex = 1u << 5u;
    inline constexpr u8 SoftwareAttribute = 1u << 6u;
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
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& hardwarePositions;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& hardwareIndices;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& hardwareAttributes;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& softwareNodes;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& softwarePositions;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& softwareIndices;
    const Vector<Core::Buffer*, Core::Alloc::GlobalArena>& softwareAttributes;
    bool includeHardware = false;
    bool includeSoftware = false;
};


[[nodiscard]] bool FreezePreparedShadowTraceGeometryBuffers(
    const Vector<ECSRenderDetail::MeshRayTracingResourceSnapshot, Core::Alloc::ScratchArena>& meshes,
    const ShadowTraceGeometrySelection& selection,
    Core::Alloc::ScratchArena& scratchArena,
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena>& acceptedBuffers,
    PreparedShadowTraceGeometryBufferVector& outPrepared
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

