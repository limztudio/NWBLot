// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shadow_prepare_geometry_resources.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ShadowPrepareGeometryResources::ShadowPrepareGeometryResources(
    const ShadowPrepareGeometryInputs& inputs,
    Core::Alloc::ScratchArena& scratchArena)
    : m_blasBuildInputs(scratchArena)
    , m_softwareTailInputs(scratchArena)
    , m_remainingTraceGeometry(scratchArena)
    , m_inputs(inputs)
    , m_scratchArena(scratchArena)
{}

void ShadowPrepareGeometryResources::prepareStorage(
    const bool blasInputStatesGraphOwned,
    const bool softwareInputStatesGraphOwned){
    NWB_ASSERT(!m_storagePrepared);
    if(blasInputStatesGraphOwned){
        for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
            addRequest(build.positionBuffer, s_BlasRole);
            addRequest(build.triangleIndexBuffer, s_BlasRole);
        }
    }
    if(softwareInputStatesGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : m_inputs.softwareBuilds){
            addRequest(build.positionBuffer, s_SoftwareRole);
            addRequest(build.triangleIndexBuffer, s_SoftwareRole);
        }
    }
    m_blasBuildInputs.reserve(m_blasRequestCount);
    m_softwareTailInputs.reserve(m_softwareRequestCount);
    m_remainingTraceGeometry.reserve(m_inputs.traceResourceCount);
    m_preparedBlasPolicy = blasInputStatesGraphOwned;
    m_preparedSoftwarePolicy = softwareInputStatesGraphOwned;
    m_storagePrepared = true;
}

void ShadowPrepareGeometryResources::gatherBuildInputs(
    const Core::GpuTaskGraph& graph,
    bool& blasInputStatesGraphOwned,
    bool& softwareInputStatesGraphOwned){
    NWB_ASSERT(m_storagePrepared && !m_inputsGathered);
    NWB_ASSERT(m_preparedBlasPolicy == blasInputStatesGraphOwned && m_preparedSoftwarePolicy == softwareInputStatesGraphOwned);
    if(!resolveRequests(graph)){
        blasInputStatesGraphOwned = false;
        softwareInputStatesGraphOwned = false;
        clearBuildInputs();
        m_inputsGathered = true;
        return;
    }
    if(blasInputStatesGraphOwned){
        for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
            if(
                !appendBuildInput(m_blasBuildInputs, build.positionBuffer, s_BlasRole)
                || !appendBuildInput(m_blasBuildInputs, build.triangleIndexBuffer, s_BlasRole)
            ){
                // A missing frozen stream retains the complete native BLAS/SW bridge instead of rejecting the packet.
                blasInputStatesGraphOwned = false;
                softwareInputStatesGraphOwned = false;
                clearBuildInputs();
                break;
            }
        }
    }
    if(softwareInputStatesGraphOwned){
        for(const PreparedMeshSwBvhBuild& build : m_inputs.softwareBuilds){
            if(
                !appendBuildInput(m_softwareTailInputs, build.positionBuffer, s_SoftwareRole)
                || !appendBuildInput(m_softwareTailInputs, build.triangleIndexBuffer, s_SoftwareRole)
            ){
                // The SW recorder accepts one all-or-native input-state policy for both callbacks.
                blasInputStatesGraphOwned = false;
                softwareInputStatesGraphOwned = false;
                clearBuildInputs();
                break;
            }
        }
    }
    m_inputsGathered = true;
}

bool ShadowPrepareGeometryResources::gatherRemainingTraceResources(){
    NWB_ASSERT(m_inputsGathered);
    const bool hasBlasInputs = !m_blasBuildInputs.empty();
    for(usize resourceIndex = 0u; resourceIndex < m_inputs.traceResourceCount; ++resourceIndex){
        const Core::GpuGraphResourceId resource = m_inputs.traceResources[resourceIndex];
        if(!resource.valid())
            return false;
        const BufferRequest* const request = hasBlasInputs ? findResolvedRequest(resource) : nullptr;
        if(request && (request->selectedRoles & s_BlasRole) != 0u)
            continue;
        m_remainingTraceGeometry.push_back(resource);
    }
    return true;
}

bool ShadowPrepareGeometryResources::isPreparedMeshBlasBuild(const Name& meshName)const{
    if(!m_inputs.blasBuildsGraphOwned)
        return false;
    if(m_inputs.blasBuilds.size() <= s_InlineCount){
        const PreparedMeshBlasBuild* const builds = m_inputs.blasBuilds.data();
        for(usize index = 0u; index < m_inputs.blasBuilds.size(); ++index){
            if(builds[index].meshName == meshName)
                return true;
        }
        return false;
    }
    if(!m_namesPrepared)
        prepareNames();
    const NameHash& identity = meshName.identityHash();
    if(m_names)
        return m_names->find(MakeNotNull(&identity)) != m_names->end();
    for(usize index = 0u; index < m_inlineNameCount; ++index){
        if(*m_inlineNames[index] == identity)
            return true;
    }
    return false;
}

void ShadowPrepareGeometryResources::addRequest(const Core::BufferHandle& buffer, const u8 role){
    if(!buffer)
        return;
    BufferRequest* request = findRequest(buffer.get());
    if(!request){
        if(!m_requests && m_inlineRequestCount == s_InlineCount)
            promoteRequests();
        if(m_requests){
            request = &m_requests->try_emplace(buffer.get()).first.value();
            request->source = &buffer;
        }
        else{
            InlineRequest& entry = m_inlineRequests[m_inlineRequestCount];
            entry.buffer = buffer.get();
            entry.request.source = &buffer;
            request = &entry.request;
            ++m_inlineRequestCount;
        }
    }
    if((request->requestedRoles & role) != 0u)
        return;
    request->requestedRoles |= role;
    if(role == s_BlasRole)
        ++m_blasRequestCount;
    else
        ++m_softwareRequestCount;
}

ShadowPrepareGeometryResources::BufferRequest* ShadowPrepareGeometryResources::findRequest(Core::Buffer* const buffer){
    if(m_requests){
        const auto found = m_requests->find(buffer);
        return found == m_requests->end() ? nullptr : &found.value();
    }
    for(usize index = 0u; index < m_inlineRequestCount; ++index){
        if(m_inlineRequests[index].buffer == buffer)
            return &m_inlineRequests[index].request;
    }
    return nullptr;
}

void ShadowPrepareGeometryResources::promoteRequests(){
    RequestIndex requests(s_InlineCount * 4u, m_scratchArena);
    for(const InlineRequest& entry : m_inlineRequests)
        requests.emplace(entry.buffer, entry.request);
    static_assert(IsNothrowMoveConstructible_V<RequestIndex>);
    m_requests.emplace(Move(requests));
}

bool ShadowPrepareGeometryResources::resolveRequests(const Core::GpuTaskGraph& graph){
    const usize requestCount = m_requests ? m_requests->size() : m_inlineRequestCount;
    if(requestCount == 0u)
        return true;
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return false;
        m_graphGeneration = declarations.generation();
        if(m_requests){
            for(auto request = m_requests->begin(); request != m_requests->end(); ++request)
                request.value().resource = declarations.findImportedBuffer(*request.value().source);
        }
        else{
            for(usize index = 0u; index < m_inlineRequestCount; ++index){
                BufferRequest& request = m_inlineRequests[index].request;
                request.resource = declarations.findImportedBuffer(*request.source);
            }
        }
    }
    if(m_requests){
        ResourceIndex resources(m_requests->bucket_count(), m_scratchArena);
        for(auto request = m_requests->begin(); request != m_requests->end(); ++request){
            if(request.value().resource.valid())
                resources.emplace(request.value().resource.index, &request.value());
        }
        static_assert(IsNothrowMoveConstructible_V<ResourceIndex>);
        m_resources.emplace(Move(resources));
    }
    // Only resolved requests receive trace membership. Unrelated trace IDs consume no index storage, and a stale
    // generation remains distinct even when its index matches a current resource. Shape validation stays in partition.
    for(usize index = 0u; index < m_inputs.traceResourceCount; ++index){
        if(BufferRequest* const request = findResolvedRequest(m_inputs.traceResources[index]))
            request->listedForTrace = true;
    }
    return true;
}

ShadowPrepareGeometryResources::BufferRequest* ShadowPrepareGeometryResources::findResolvedRequest(
    const Core::GpuGraphResourceId resource){
    if(!resource.valid() || resource.generation != m_graphGeneration)
        return nullptr;
    if(m_resources){
        const auto found = m_resources->find(resource.index);
        return found == m_resources->end() ? nullptr : found.value();
    }
    for(usize index = 0u; index < m_inlineRequestCount; ++index){
        BufferRequest& request = m_inlineRequests[index].request;
        if(request.resource == resource)
            return &request;
    }
    return nullptr;
}

bool ShadowPrepareGeometryResources::appendBuildInput(
    ResourceVector& resources,
    const Core::BufferHandle& buffer,
    const u8 role){
    BufferRequest* const request = findRequest(buffer.get());
    if(!request || !request->resource.valid() || !request->listedForTrace)
        return false;
    if((request->selectedRoles & role) == 0u){
        resources.push_back(request->resource);
        request->selectedRoles |= role;
    }
    return true;
}

void ShadowPrepareGeometryResources::clearBuildInputs()noexcept{
    m_blasBuildInputs.clear();
    m_softwareTailInputs.clear();
    if(m_requests){
        for(auto request = m_requests->begin(); request != m_requests->end(); ++request)
            request.value().selectedRoles = 0u;
    }
    else{
        for(usize index = 0u; index < m_inlineRequestCount; ++index)
            m_inlineRequests[index].request.selectedRoles = 0u;
    }
}

void ShadowPrepareGeometryResources::prepareNames()const{
    for(const PreparedMeshBlasBuild& build : m_inputs.blasBuilds){
        const NameHash& identity = build.meshName.identityHash();
        if(m_names){
            m_names->insert(MakeNotNull(&identity));
            continue;
        }
        bool found = false;
        for(usize index = 0u; index < m_inlineNameCount; ++index){
            if(*m_inlineNames[index] == identity){
                found = true;
                break;
            }
        }
        if(found)
            continue;
        if(m_inlineNameCount < s_InlineCount){
            m_inlineNames[m_inlineNameCount] = &identity;
            ++m_inlineNameCount;
            continue;
        }
        NameIndex names(s_InlineCount * 4u, m_scratchArena);
        for(const NameHash* const existing : m_inlineNames)
            names.insert(MakeNotNull(existing));
        names.insert(MakeNotNull(&identity));
        static_assert(IsNothrowMoveConstructible_V<NameIndex>);
        m_names.emplace(Move(names));
    }
    m_namesPrepared = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

