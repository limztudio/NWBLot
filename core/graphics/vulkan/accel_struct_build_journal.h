// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"

#include <global/span.h>


NWB_VULKAN_BEGIN


struct AccelStructGeometryBuildSignature{
    VkDeviceSize vertexStride = 0u;
    VkDeviceSize radiusStride = 0u;
    VkDeviceSize indexStride = 0u;
    VkGeometryTypeKHR geometryType = VK_GEOMETRY_TYPE_MAX_ENUM_KHR;
    VkGeometryFlagsKHR geometryFlags = 0u;
    u32 primitiveCount = 0u;
    VkFormat vertexFormat = VK_FORMAT_UNDEFINED;
    VkFormat radiusFormat = VK_FORMAT_UNDEFINED;
    VkIndexType indexType = VK_INDEX_TYPE_NONE_KHR;
    u32 maxVertex = 0u;
    VkRayTracingLssIndexingModeNV lssIndexingMode = VK_RAY_TRACING_LSS_INDEXING_MODE_MAX_ENUM_NV;
    VkRayTracingLssPrimitiveEndCapsModeNV lssEndCapsMode = VK_RAY_TRACING_LSS_PRIMITIVE_END_CAPS_MODE_MAX_ENUM_NV;
    b8 transformDataPresent = false;
};

struct AccelStructBottomLevelReferences{
    Vector<RayTracingAccelStructHandle, Alloc::GlobalArena> resources;


public:
    explicit AccelStructBottomLevelReferences(Alloc::GlobalArena& arena)noexcept
        : resources(arena)
    {}
};
using AccelStructBottomLevelReferenceControl = RefCounter<AccelStructBottomLevelReferences>;
using AccelStructBottomLevelReferenceHandle = RefCountPtr<AccelStructBottomLevelReferenceControl, ArenaRefDeleter<AccelStructBottomLevelReferenceControl, Alloc::GlobalArena>>;

struct AccelStructBuildSignatureRole{
    VkAccelerationStructureTypeKHR accelStructType = VK_ACCELERATION_STRUCTURE_TYPE_MAX_ENUM_KHR;
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0u;
    Vector<AccelStructGeometryBuildSignature, Alloc::GlobalArena> geometrySignatures;
    AccelStructBottomLevelReferenceHandle bottomLevelReferences;
    AccelStructBuildSignatureRole* nextRetiredRole = nullptr;


public:
    explicit AccelStructBuildSignatureRole(Alloc::GlobalArena& arena)noexcept
        : geometrySignatures(arena)
    {}
};

struct PendingAccelStructBuildCommit{
    NotNull<AccelStruct*> accelStruct;
    AccelStructBuildSignatureRole* preparedRole = nullptr;
    AccelStructBuildSignatureRole* displacedRole = nullptr;
};
static_assert(IsTriviallyCopyable_V<PendingAccelStructBuildCommit>, "accepted acceleration-structure publication must remain scalar-only");

class AccelStructBuildJournal final : NoCopy{
    friend class TrackedCommandBuffer;


public:
    AccelStructBuildJournal(TrackedCommandBuffer& owner, Alloc::GlobalArena& arena)noexcept
        : m_owner(owner)
        , m_pendingAccelStructBuildCommits(arena)
        , m_referencedTopLevelAccelStructs(arena)
        , m_referencedBottomLevelReferenceSets(arena)
    {}
    [[nodiscard]] b8 hasRetainedTopLevelAccelStructs()const noexcept{ return !m_referencedTopLevelAccelStructs.empty(); }


private:
    [[nodiscard]] b8 appendPendingAccelStructBuildCommit(AccelStruct& accelStruct, VkAccelerationStructureTypeKHR accelStructType, VkBuildAccelerationStructureFlagsKHR buildFlags, Span<const AccelStructGeometryBuildSignature> geometrySignatures, Span<const RayTracingInstanceDesc> instances);
    [[nodiscard]] Expected<NotNull<const AccelStructBuildSignatureRole*>> getPendingAccelStructBuildSignature(const AccelStruct& accelStruct)const noexcept;
    [[nodiscard]] b8 validatePendingAccelStructBuildCommits()const noexcept;
    void commitPendingAccelStructBuildCommits()noexcept;
    void retainTopLevelAccelStruct(AccelStruct& accelStruct);
    void retainBottomLevelReferenceSet(const AccelStructBottomLevelReferenceHandle& references);
    void retainAcceptedTopLevelAccelStructReferences();
    void retainPendingTopLevelAccelStructReferences(const AccelStructBuildJournal& buildJournal);
    void abandonPendingAccelStructBuildCommits()noexcept;
    void clearTrackedReferences()noexcept{
        m_referencedBottomLevelReferenceSets.clear();
        m_referencedTopLevelAccelStructs.clear();
    }


private:
    TrackedCommandBuffer& m_owner;
    Vector<PendingAccelStructBuildCommit, Alloc::GlobalArena> m_pendingAccelStructBuildCommits;
    Vector<RayTracingAccelStructHandle, Alloc::GlobalArena> m_referencedTopLevelAccelStructs;
    Vector<AccelStructBottomLevelReferenceHandle, Alloc::GlobalArena> m_referencedBottomLevelReferenceSets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
