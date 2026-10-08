// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_software_bvh_graph_resources.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<PreparedMeshSwBvhGraphResourceVector> ResolvePreparedSoftwareBvhGraphResources(
    Core::Alloc::ScratchArena& scratchArena,
    const Core::GpuTaskGraph& graph,
    const PreparedMeshSwBvhBuildVector& builds
){
    PreparedMeshSwBvhGraphResourceVector result(scratchArena);
    if(builds.empty())
        return result;
    result.reserve(builds.size());
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    if(!declarations.valid())
        return MakeUnexpected(Failure{});
    for(const PreparedMeshSwBvhBuild& build : builds){
        const PreparedMeshSwBvhGraphResources resources{
            .build = build,
            .position = declarations.findImportedBuffer(build.positionBuffer),
            .triangleIndex = declarations.findImportedBuffer(build.triangleIndexBuffer),
            .node = declarations.findImportedBuffer(build.nodeBuffer),
            .parent = declarations.findImportedBuffer(build.parentBuffer),
            .sortKeys = declarations.findImportedBuffer(build.sortKeysBuffer),
            .sortPayload = declarations.findImportedBuffer(build.sortPayloadBuffer),
            .visitCounter = declarations.findImportedBuffer(build.visitCounterBuffer),
        };
        if(
            !resources.position.valid()
            || !resources.triangleIndex.valid()
            || !resources.node.valid()
            || !resources.parent.valid()
            || !resources.sortKeys.valid()
            || !resources.sortPayload.valid()
            || !resources.visitCounter.valid()
        ){
            return MakeUnexpected(Failure{});
        }
        result.push_back(resources);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

