// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prepared_software_bvh_graph_resources.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolvePreparedSoftwareBvhGraphResources(
    const Core::GpuTaskGraph& graph,
    const PreparedMeshSwBvhBuildVector& builds,
    PreparedMeshSwBvhGraphResourceVector& outResources){
    outResources.clear();
    if(builds.empty())
        return true;
    const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
    if(!declarations.valid())
        return false;
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
            outResources.clear();
            return false;
        }
        outResources.push_back(resources);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

