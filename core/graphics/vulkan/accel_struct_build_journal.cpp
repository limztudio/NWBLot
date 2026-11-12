// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "accel_struct_build_journal.h"

#include <global/termination.h>


NWB_VULKAN_BEGIN


b8 AccelStructBuildJournal::appendPendingAccelStructBuildCommit(
    AccelStruct& accelStruct,
    const VkAccelerationStructureTypeKHR accelStructType,
    const VkBuildAccelerationStructureFlagsKHR buildFlags,
    const Span<const AccelStructGeometryBuildSignature> geometrySignatures,
    const Span<const RayTracingInstanceDesc> instances
){
    FOUNDATION_ASSERT(geometrySignatures.size() <= UINT32_MAX);
    FOUNDATION_ASSERT(geometrySignatures.empty() || geometrySignatures.data());

    GlobalUniquePtr<AccelStructBuildSignatureRole> preparedRole = accelStruct.acquireBuildSignatureRole();
    if(!preparedRole)
        return false;

    preparedRole->accelStructType = accelStructType;
    preparedRole->buildFlags = buildFlags;
    preparedRole->geometrySignatures.clear();
    if(!geometrySignatures.empty()){
        preparedRole->geometrySignatures.reserve(geometrySignatures.size());
        preparedRole->geometrySignatures.assign(geometrySignatures.begin(), geometrySignatures.end());
    }

    if(instances.data() && !instances.empty()){
        AccelStructBottomLevelReferenceControl* references = nullptr;
        if(preparedRole->bottomLevelReferences && preparedRole->bottomLevelReferences->getReferenceCount() == 1u){
            references = preparedRole->bottomLevelReferences.get();
            references->resources.clear();
        }
        else{
            references = NewArenaObject<AccelStructBottomLevelReferenceControl>(m_owner.m_context.objectArena, m_owner.m_context.objectArena);
            if(!references)
                return false;
            preparedRole->bottomLevelReferences = AccelStructBottomLevelReferenceHandle(references, AccelStructBottomLevelReferenceHandle::deleter_type(&m_owner.m_context.objectArena), s_AdoptRef);
        }
        references->resources.reserve(instances.size());
        for(const RayTracingInstanceDesc& instance : instances){
            if(AccelStruct* const bottomLevel = instance.bottomLevelAS)
                references->resources.emplace_back(bottomLevel, RayTracingAccelStructHandle::deleter_type(&m_owner.m_context.objectArena));
        }
    }
    else if(preparedRole->bottomLevelReferences){
        if(preparedRole->bottomLevelReferences->getReferenceCount() == 1u)
            preparedRole->bottomLevelReferences->resources.clear();
        else
            preparedRole->bottomLevelReferences = nullptr;
    }

    m_owner.m_resourceReferences.retainResource(accelStruct);
    retainBottomLevelReferenceSet(preparedRole->bottomLevelReferences);
    m_pendingAccelStructBuildCommits.push_back(PendingAccelStructBuildCommit{ .accelStruct = MakeNotNull(&accelStruct), .preparedRole = preparedRole.get(), });
    preparedRole.release();
    return true;
}

Expected<NotNull<const AccelStructBuildSignatureRole*>> AccelStructBuildJournal::getPendingAccelStructBuildSignature(const AccelStruct& accelStruct)const noexcept{
    for(usize commitIndex = m_pendingAccelStructBuildCommits.size(); commitIndex > 0u; --commitIndex){
        const PendingAccelStructBuildCommit& commit = m_pendingAccelStructBuildCommits[commitIndex - 1u];
        if(commit.accelStruct.get() == &accelStruct){
            if(!commit.preparedRole)
                return MakeUnexpected(Failure{});

            return MakeNotNull<const AccelStructBuildSignatureRole*>(commit.preparedRole);
        }
    }

    return MakeUnexpected(Failure{});
}

b8 AccelStructBuildJournal::validatePendingAccelStructBuildCommits()const noexcept{
    for(const PendingAccelStructBuildCommit& commit : m_pendingAccelStructBuildCommits){
        if(!commit.preparedRole || commit.displacedRole)
            return false;
    }
    return true;
}

void AccelStructBuildJournal::commitPendingAccelStructBuildCommits()noexcept{
    for(PendingAccelStructBuildCommit& commit : m_pendingAccelStructBuildCommits){
        if(!commit.preparedRole || commit.displacedRole)
            TerminateInvariant();

        AccelStruct& accelStruct = *commit.accelStruct;
        NothrowScopedLock lock(accelStruct.m_acceptedBuildSignatureMutex);
        commit.displacedRole = accelStruct.m_acceptedBuildSignatureRole;
        accelStruct.m_acceptedBuildSignatureRole = commit.preparedRole;
        commit.preparedRole = nullptr;
    }
}

void AccelStructBuildJournal::retainTopLevelAccelStruct(AccelStruct& accelStruct){
    for(const RayTracingAccelStructHandle& retained : m_referencedTopLevelAccelStructs){
        if(retained.get() == &accelStruct)
            return;
    }
    m_referencedTopLevelAccelStructs.emplace_back(&accelStruct, RayTracingAccelStructHandle::deleter_type(&m_owner.m_context.objectArena));
}

void AccelStructBuildJournal::retainBottomLevelReferenceSet(const AccelStructBottomLevelReferenceHandle& references){
    if(!references || references->resources.empty())
        return;
    for(const AccelStructBottomLevelReferenceHandle& retained : m_referencedBottomLevelReferenceSets){
        if(retained.get() == references.get())
            return;
    }
    m_referencedBottomLevelReferenceSets.push_back(references);
}

void AccelStructBuildJournal::retainAcceptedTopLevelAccelStructReferences(){
    for(const RayTracingAccelStructHandle& accelStruct : m_referencedTopLevelAccelStructs){
        ScopedLock lock(accelStruct->m_acceptedBuildSignatureMutex);

        if(accelStruct->m_acceptedBuildSignatureRole)
            retainBottomLevelReferenceSet(accelStruct->m_acceptedBuildSignatureRole->bottomLevelReferences);
    }
}

void AccelStructBuildJournal::retainPendingTopLevelAccelStructReferences(const AccelStructBuildJournal& buildJournal){
    for(const PendingAccelStructBuildCommit& commit : buildJournal.m_pendingAccelStructBuildCommits){
        if(!commit.preparedRole || !commit.preparedRole->bottomLevelReferences)
            continue;
        for(const RayTracingAccelStructHandle& accelStruct : m_referencedTopLevelAccelStructs){
            if(commit.accelStruct.get() == accelStruct.get()){
                retainBottomLevelReferenceSet(commit.preparedRole->bottomLevelReferences);
                break;
            }
        }
    }
}

void AccelStructBuildJournal::abandonPendingAccelStructBuildCommits()noexcept{
    static_assert(noexcept(m_pendingAccelStructBuildCommits.clear()), "abandoned acceleration-structure journal release must be non-throwing");
    static_assert(noexcept(DeclVal<AccelStructBottomLevelReferenceHandle&>() = nullptr), "abandoned build references must release without throwing");
    m_referencedBottomLevelReferenceSets.clear();
    for(PendingAccelStructBuildCommit& commit : m_pendingAccelStructBuildCommits){
        if(commit.preparedRole){
            commit.accelStruct->retireBuildSignatureRole(*commit.preparedRole);
            commit.preparedRole = nullptr;
        }
        if(commit.displacedRole){
            commit.accelStruct->retireBuildSignatureRole(*commit.displacedRole);
            commit.displacedRole = nullptr;
        }
    }
    m_pendingAccelStructBuildCommits.clear();
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

