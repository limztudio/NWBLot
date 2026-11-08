// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_import_index.h"

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


GpuGraphResourceImportIndex::ResourcePointerKey GpuGraphResourceImportIndex::MakeResourcePointerKey(const ResourceNode& resource)noexcept{
    switch(resource.type){
    case GpuGraphResourceType::Texture:
        return { resource.texture.get(), resource.type };
    case GpuGraphResourceType::Buffer:
        return { resource.buffer.get(), resource.type };
    case GpuGraphResourceType::AccelStruct:
        return { resource.accelStruct.get(), resource.type };
    default:
        return {};
    }
}

u32 GpuGraphResourceImportIndex::findResourceIdentity(const Span<const ResourceNode> resources, const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceIdentityIndex){
        const auto found = m_resourceIdentityIndex->find(key);
        return found == m_resourceIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < resources.size(); ++index){
        if(resources[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

GpuGraphResourceImportIndex::ResourceImportMatch GpuGraphResourceImportIndex::findResourceImportMatch(const Span<const ResourceNode> resources, const NameHash* const identity, const ResourcePointerKey& pointer)const noexcept{
    if(!pointer.pointer)
        return {};
    if(m_resourceIdentityIndex){
        ResourceImportMatch match;
        if(m_resourcePointerIndex){
            const auto found = m_resourcePointerIndex->find(pointer);
            if(found != m_resourcePointerIndex->end())
                match = { found.value(), true };
        }
        if(identity){
            const auto found = m_resourceIdentityIndex->find(*identity);
            // At the same ordinal the typed pointer wins.
            // An earlier identity conflict still rejects before a later pointer match, matching the original ordered import validation.
            if(found != m_resourceIdentityIndex->end() && found.value() < match.index)
                return { found.value(), false };
        }
        return match;
    }
    for(usize index = 0u; index < resources.size(); ++index){
        const ResourceNode& resource = resources[index];
        if(resource.type == pointer.type){
            const void* typedPointer = nullptr;
            switch(resource.type){
            case GpuGraphResourceType::Texture:
                typedPointer = resource.texture.get();
                break;
            case GpuGraphResourceType::Buffer:
                typedPointer = resource.buffer.get();
                break;
            case GpuGraphResourceType::AccelStruct:
                typedPointer = resource.accelStruct.get();
                break;
            default:
                break;
            }
            if(typedPointer == pointer.pointer)
                return { static_cast<u32>(index), true };
        }
        if(identity && resource.identity.identityHash() == *identity)
            return { static_cast<u32>(index), false };
    }
    return {};
}

u32 GpuGraphResourceImportIndex::findResourceSetIdentity(const Span<const ResourceSetNode> sets, const Name& identity)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceSetIdentityIndex){
        const auto found = m_resourceSetIdentityIndex->find(key);
        return found == m_resourceSetIdentityIndex->end() ? s_InvalidImportIndex : found.value();
    }
    for(usize index = 0u; index < sets.size(); ++index){
        if(sets[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

void GpuGraphResourceImportIndex::prepareResourceIndexes(const Span<const ResourceNode> resources, const ResourcePointerKey& pendingPointer){
    if(!m_resourceIdentityIndex){
        if(resources.size() < s_InlineImportIndexCount)
            return;

        const usize identityCount = AddSize(resources.size(), 1u);
        IdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
        usize pointerCount = pendingPointer.pointer ? 1u : 0u;
        for(usize index = 0u; index < resources.size(); ++index){
            const ResourceNode& resource = resources[index];
            // Insertion retains the first ordinal; later declarations must never replace earlier lookup priority.
            identities.emplace(resource.identity.identityHash(), static_cast<u32>(index));
            if(MakeResourcePointerKey(resource).pointer)
                ++pointerCount;
        }
        Optional<PointerIndex> pointers;
        if(pointerCount != 0u){
            pointers.emplace(AddSize(pointerCount, pointerCount), m_arena);
            for(usize index = 0u; index < resources.size(); ++index){
                const ResourcePointerKey key = MakeResourcePointerKey(resources[index]);
                if(key.pointer)
                    pointers->emplace(key, static_cast<u32>(index));
            }
        }

        // Build both tables before publishing either. Both hold published ordinals only.
        static_assert(IsNothrowMoveConstructible_V<IdentityIndex>);
        static_assert(IsNothrowMoveConstructible_V<PointerIndex>);
        m_resourceIdentityIndex.emplace(Move(identities));
        if(pointers)
            m_resourcePointerIndex.emplace(Move(*pointers));
    }
    else if(pendingPointer.pointer && !m_resourcePointerIndex)
        m_resourcePointerIndex.emplace(s_InitialPointerIndexBucketCount, m_arena);
}

void GpuGraphResourceImportIndex::prepareResourceSetIndex(const Span<const ResourceSetNode> sets){
    if(m_resourceSetIdentityIndex || sets.size() < s_InlineImportIndexCount)
        return;
    const usize identityCount = AddSize(sets.size(), 1u);
    IdentityIndex identities(AddSize(identityCount, identityCount), m_arena);
    // emplace intentionally keeps the first ordinal for the exact identity.
    for(usize index = 0u; index < sets.size(); ++index)
        identities.emplace(sets[index].identity.identityHash(), static_cast<u32>(index));
    static_assert(IsNothrowMoveConstructible_V<IdentityIndex>);
    m_resourceSetIdentityIndex.emplace(Move(identities));
}


b8 GpuGraphResourceImportIndex::publishResource(const NameHash& identity, const ResourcePointerKey& pointer, const u32 index, Publication& publication){
    if(!m_resourceIdentityIndex)
        return true;
    publication.identity = m_resourceIdentityIndex->emplace(identity, index).second;
    if(!publication.identity)
        return false;
    if(pointer.pointer){
        publication.pointer = m_resourcePointerIndex->emplace(pointer, index).second;
        if(!publication.pointer)
            return false;
    }
    return true;
}

void GpuGraphResourceImportIndex::rollbackResource(const NameHash& identity, const ResourcePointerKey& pointer, const Publication& publication)noexcept{
    if(publication.pointer)
        m_resourcePointerIndex->erase(pointer);
    if(publication.identity)
        m_resourceIdentityIndex->erase(identity);
}

b8 GpuGraphResourceImportIndex::publishResourceSet(const NameHash& identity, const u32 index, b8& inserted){
    if(!m_resourceSetIdentityIndex)
        return true;
    inserted = m_resourceSetIdentityIndex->emplace(identity, index).second;
    return inserted;
}

void GpuGraphResourceImportIndex::rollbackResourceSet(const NameHash& identity, const b8 inserted)noexcept{
    if(inserted)
        m_resourceSetIdentityIndex->erase(identity);
}

void GpuGraphResourceImportIndex::clearResources(){
    if(m_resourceIdentityIndex)
        m_resourceIdentityIndex->clear();
    if(m_resourcePointerIndex)
        m_resourcePointerIndex->clear();
}

void GpuGraphResourceImportIndex::clearResourceSets(){
    if(m_resourceSetIdentityIndex)
        m_resourceSetIdentityIndex->clear();
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
