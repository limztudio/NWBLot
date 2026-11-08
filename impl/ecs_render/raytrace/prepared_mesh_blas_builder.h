// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/prepared_builds.h>


NWB_IMPL_BEGIN


namespace ECSRenderDetail{ struct MeshRayTracingResourceSnapshot; };

class PreparedMeshBlasBuilder final{
public:
    [[nodiscard]] static Expected<PreparedMeshBlasBuild> resolve(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources)noexcept;
    [[nodiscard]] static b8 matches(const ECSRenderDetail::MeshRayTracingResourceSnapshot& meshResources, const PreparedMeshBlasBuild& build)noexcept;
    [[nodiscard]] static b8 record(const PreparedMeshBlasBuild& build, b8 meshBlasGeometryBuildInputStatesGraphOwned, Core::CommandList& commandList);
};


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
