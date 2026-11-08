// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


struct MeshResources;
namespace ECSRenderDetail{ struct MeshRayTracingResourceSnapshot; };


class MeshRayTracingSnapshotPolicy{
public:
    [[nodiscard]] static ECSRenderDetail::MeshRayTracingResourceSnapshot capture(const MeshResources& mesh)noexcept;
    [[nodiscard]] static b8 matches(const MeshResources& mesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot)noexcept;
    [[nodiscard]] static b8 sameIdentity(const ECSRenderDetail::MeshRayTracingResourceSnapshot& lhs, const ECSRenderDetail::MeshRayTracingResourceSnapshot& rhs)noexcept;

private:
    [[nodiscard]] static b8 boundsReady(const MeshResources& mesh)noexcept;
    [[nodiscard]] static b8 meshletMatches(const MeshResources& mesh, const ECSRenderDetail::MeshRayTracingResourceSnapshot& snapshot)noexcept;
};


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
