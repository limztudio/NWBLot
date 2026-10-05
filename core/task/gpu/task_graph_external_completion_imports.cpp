// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph.h"

#include <global/allocation_size.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_task_graph_external_completion_imports{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool HasExternalCompletionTokenValue(const QueueSubmissionToken& token)noexcept{
    return token.queue != CommandQueue::kCount
        || token.value != 0u
        || token.physicalQueueIndex != Limit<u16>::s_Max
        || token.deviceGeneration != 0u
    ;
}

[[nodiscard]] static bool SameSubmissionToken(
    const QueueSubmissionToken& lhs,
    const QueueSubmissionToken& rhs)noexcept{
    return lhs.queue == rhs.queue
        && lhs.value == rhs.value
        && lhs.physicalQueueIndex == rhs.physicalQueueIndex
        && lhs.deviceGeneration == rhs.deviceGeneration
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuExternalCompletionId GpuTaskGraph::importExternalCompletion(const GpuExternalCompletionDesc& desc){
    DeclarationMutationScope mutation(*this);
    if(!mutation.valid())
        return {};

    using namespace __hidden_gpu_task_graph_external_completion_imports;
    const bool hasToken = HasExternalCompletionTokenValue(desc.token);
    if(
        !desc.identity
        || desc.markerLabel.empty()
        || (hasToken && (
            !desc.token.valid()
            || !desc.token.hasPhysicalQueueIdentity()
            || !validForDeviceGeneration(desc.token.deviceGeneration)
        ))
    )
        return {};

    const u32 completionIndex = findExternalCompletionIdentity(desc.identity);
    if(completionIndex != s_InvalidImportIndex){
        GpuExternalCompletionNode& existing = m_externalCompletions[completionIndex];
        if(hasToken){
            if(existing.hasToken){
                if(!SameSubmissionToken(existing.token, desc.token))
                    return {};
            }
            else{
                existing.token = desc.token;
                existing.hasToken = true;
                m_externalCompletionDeviceGeneration = desc.token.deviceGeneration;
                m_declarationRevision = AllocateGeneration();
            }
        }
        return GpuExternalCompletionId{ .generation = m_generation, .index = completionIndex };
    }

    return appendExternalCompletion(desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


u32 GpuTaskGraph::findExternalCompletionIdentity(const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_externalCompletionIdentityIndex){
        const auto found = m_externalCompletionIdentityIndex->find(key);
        return found == m_externalCompletionIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < m_externalCompletions.size(); ++index){
        if(m_externalCompletions[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

void GpuTaskGraph::prepareExternalCompletionIndex(){
    if(m_externalCompletionIdentityIndex || m_externalCompletions.size() < s_InlineImportIndexCount)
        return;
    const usize identityCount = AddSize(m_externalCompletions.size(), 1u);
    ImportIdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
    for(usize index = 0u; index < m_externalCompletions.size(); ++index){
        if(!identities.emplace(m_externalCompletions[index].identity.identityHash(), static_cast<u32>(index)).second){
            GLB_FATAL_ASSERT_MSG(false, "Completion import index requires unique retained identities");
            TerminateInvariant();
        }
    }
    static_assert(IsNothrowMoveConstructible_V<ImportIdentityIndex>);
    m_externalCompletionIdentityIndex.emplace(Move(identities));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

