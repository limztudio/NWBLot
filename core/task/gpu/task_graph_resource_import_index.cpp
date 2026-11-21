// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_resource_import_index.h"

#include <global/allocation_size.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


[[nodiscard]] static constexpr b8 ExceedsImportIndexLoadFactor(const usize count, const usize capacity)noexcept{
    if(capacity == 0u)
        return true;
    const usize maxLoad = (capacity / 10u) * 7u + ((capacity % 10u) * 7u) / 10u;
    return count > maxLoad;
}


template<typename KeyT, typename HashFn, typename EqualFn>
u32 GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::find(const KeyT& key)const noexcept{
    if(m_count == 0u || m_slots.empty())
        return s_InvalidImportIndex;

    const usize mask = m_slots.size() - 1u;
    usize index = m_hash(key) & mask;
    const u32 currentStamp = m_stamp;
    for(;;){
        const Slot& slot = m_slots[index];
        if(slot.stamp != currentStamp)
            return s_InvalidImportIndex;
        if(m_equal(slot.key, key))
            return slot.value;
        index = (index + 1u) & mask;
    }
}

template<typename KeyT, typename HashFn, typename EqualFn>
typename GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::ProbeResult GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::probe(const KeyT& key)const noexcept{
    if(m_slots.empty())
        return {};

    const usize capacity = m_slots.size();
    const usize mask = capacity - 1u;
    usize index = m_hash(key) & mask;
    const u32 currentStamp = m_stamp;
    for(;;){
        const Slot& slot = m_slots[index];
        if(slot.stamp != currentStamp)
            return { s_InvalidImportIndex, index, capacity, currentStamp };
        if(m_equal(slot.key, key))
            return { slot.value, index, capacity, currentStamp };
        index = (index + 1u) & mask;
    }
}

template<typename KeyT, typename HashFn, typename EqualFn>
void GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::reserveForCount(const usize targetCount){
    const usize minSlots = Max<usize>(s_MinTableSlotCount, AddSize(targetCount, AddSize(targetCount >> 1u, 8u)));
    if(m_slots.size() >= minSlots && !ExceedsImportIndexLoadFactor(targetCount, m_slots.size()))
        return;

    usize newCapacity = m_slots.empty() ? s_MinTableSlotCount : m_slots.size();
    while(newCapacity < minSlots || ExceedsImportIndexLoadFactor(targetCount, newCapacity)){
        if(newCapacity > (Limit<usize>::s_Max >> 1u))
            break;
        newCapacity <<= 1u;
    }

    GraphicsVector<Slot> rehashed(m_arena);
    rehashed.resize(newCapacity);
    const usize mask = newCapacity - 1u;
    const u32 currentStamp = m_stamp;
    for(const Slot& slot : m_slots){
        if(slot.stamp != currentStamp)
            continue;
        usize index = m_hash(slot.key) & mask;
        while(rehashed[index].stamp == currentStamp)
            index = (index + 1u) & mask;
        rehashed[index] = slot;
    }
    m_slots = Move(rehashed);
}

template<typename KeyT, typename HashFn, typename EqualFn>
b8 GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::emplace(const KeyT& key, const u32 value){
    if(m_slots.empty() || ExceedsImportIndexLoadFactor(AddSize(m_count, 1u), m_slots.size()))
        reserveForCount(AddSize(m_count, 1u));

    const usize mask = m_slots.size() - 1u;
    usize index = m_hash(key) & mask;
    const u32 currentStamp = m_stamp;
    for(;;){
        Slot& slot = m_slots[index];
        if(slot.stamp != currentStamp){
            slot.key = key;
            slot.value = value;
            slot.stamp = currentStamp;
            ++m_count;
            return true;
        }
        if(m_equal(slot.key, key))
            return false;
        index = (index + 1u) & mask;
    }
}

template<typename KeyT, typename HashFn, typename EqualFn>
b8 GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::emplaceAtHint(const KeyT& key, const u32 value, const ProbeResult& hint){
    if(
        hint.slotIndex < m_slots.size()
        && hint.slotCapacity == m_slots.size()
        && hint.stamp == m_stamp
        && !ExceedsImportIndexLoadFactor(AddSize(m_count, 1u), m_slots.size())
    ){
        Slot& slot = m_slots[hint.slotIndex];
        if(slot.stamp != m_stamp){
            slot.key = key;
            slot.value = value;
            slot.stamp = m_stamp;
            ++m_count;
            return true;
        }
    }
    return emplace(key, value);
}

template<typename KeyT, typename HashFn, typename EqualFn>
void GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::erase(const KeyT& key)noexcept{
    if(m_count == 0u || m_slots.empty())
        return;

    const usize mask = m_slots.size() - 1u;
    usize hole = m_hash(key) & mask;
    const u32 currentStamp = m_stamp;
    for(;;){
        const Slot& slot = m_slots[hole];
        if(slot.stamp != currentStamp)
            return;
        if(m_equal(slot.key, key))
            break;
        hole = (hole + 1u) & mask;
    }

    usize scan = (hole + 1u) & mask;
    for(;;){
        Slot& candidate = m_slots[scan];
        if(candidate.stamp != currentStamp){
            m_slots[hole].stamp = 0u;
            --m_count;
            return;
        }
        const usize home = m_hash(candidate.key) & mask;
        if(((scan - home) & mask) >= ((scan - hole) & mask)){
            m_slots[hole] = candidate;
            hole = scan;
        }
        scan = (scan + 1u) & mask;
    }
}

template<typename KeyT, typename HashFn, typename EqualFn>
void GpuGraphResourceImportIndex::FlatIndexTable<KeyT, HashFn, EqualFn>::clear()noexcept{
    m_count = 0u;
    if(m_slots.empty())
        return;
    ++m_stamp;
    if(m_stamp == 0u){
        m_stamp = 1u;
        for(Slot& slot : m_slots)
            slot.stamp = 0u;
    }
}


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

u32 GpuGraphResourceImportIndex::findResourceIdentity(const Span<const ResourceNode> resources, const Name& identity, ResourceLookupHint* const outHint)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceIdentityIndex.active()){
        if(!outHint)
            return m_resourceIdentityIndex.find(key);
        const IdentityIndex::ProbeResult probed = m_resourceIdentityIndex.probe(key);
        if(probed.value == s_InvalidImportIndex)
            outHint->identity = probed;
        return probed.value;
    }
    for(usize index = 0u; index < resources.size(); ++index){
        if(resources[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

GpuGraphResourceImportIndex::ResourceImportMatch GpuGraphResourceImportIndex::findResourceImportMatch(const Span<const ResourceNode> resources, const NameHash* const identity, const ResourcePointerKey& pointer, ResourceLookupHint* const outHint)const noexcept{
    if(!pointer.pointer)
        return {};
    if(m_resourceIdentityIndex.active()){
        ResourceImportMatch match;
        PointerIndex::ProbeResult pointerProbe;
        if(m_resourcePointerIndex.active()){
            pointerProbe = outHint ? m_resourcePointerIndex.probe(pointer) : PointerIndex::ProbeResult{ m_resourcePointerIndex.find(pointer) };
            if(pointerProbe.value != s_InvalidImportIndex)
                match = { pointerProbe.value, true };
        }
        IdentityIndex::ProbeResult identityProbe;
        if(identity){
            identityProbe = outHint ? m_resourceIdentityIndex.probe(*identity) : IdentityIndex::ProbeResult{ m_resourceIdentityIndex.find(*identity) };
            if(identityProbe.value != s_InvalidImportIndex && identityProbe.value < match.index)
                return { identityProbe.value, false };
        }
        if(outHint && match.index == s_InvalidImportIndex){
            outHint->identity = identityProbe;
            outHint->pointer = pointerProbe;
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

u32 GpuGraphResourceImportIndex::findResourceSetIdentity(const Span<const ResourceSetNode> sets, const Name& identity, ResourceSetLookupHint* const outHint)const noexcept{
    const NameHash& key = identity.identityHash();
    if(m_resourceSetIdentityIndex.active()){
        if(!outHint)
            return m_resourceSetIdentityIndex.find(key);
        const IdentityIndex::ProbeResult probed = m_resourceSetIdentityIndex.probe(key);
        if(probed.value == s_InvalidImportIndex)
            *outHint = probed;
        return probed.value;
    }
    for(usize index = 0u; index < sets.size(); ++index){
        if(sets[index].identity.identityHash() == key)
            return static_cast<u32>(index);
    }
    return s_InvalidImportIndex;
}

void GpuGraphResourceImportIndex::prepareResourceIndexes(const Span<const ResourceNode> resources, const ResourcePointerKey& pendingPointer){
    const usize identityCount = AddSize(resources.size(), 1u);
    if(!m_resourceIdentityIndex.active()){
        if(resources.size() < s_InlineImportIndexCount)
            return;

        m_resourceIdentityIndex.reserveForCount(identityCount);
        usize pointerCount = pendingPointer.pointer ? 1u : 0u;
        for(usize index = 0u; index < resources.size(); ++index){
            const ResourceNode& resource = resources[index];
            (void)m_resourceIdentityIndex.emplace(resource.identity.identityHash(), static_cast<u32>(index));
            if(MakeResourcePointerKey(resource).pointer)
                ++pointerCount;
        }
        if(pointerCount != 0u){
            m_resourcePointerIndex.reserveForCount(pointerCount);
            for(usize index = 0u; index < resources.size(); ++index){
                const ResourcePointerKey key = MakeResourcePointerKey(resources[index]);
                if(key.pointer)
                    (void)m_resourcePointerIndex.emplace(key, static_cast<u32>(index));
            }
        }
    }
    else{
        m_resourceIdentityIndex.reserveForCount(identityCount);
        if(pendingPointer.pointer)
            m_resourcePointerIndex.reserveForCount(AddSize(m_resourcePointerIndex.size(), 1u));
    }
}

void GpuGraphResourceImportIndex::prepareResourceSetIndex(const Span<const ResourceSetNode> sets){
    const usize identityCount = AddSize(sets.size(), 1u);
    if(!m_resourceSetIdentityIndex.active()){
        if(sets.size() < s_InlineImportIndexCount)
            return;

        m_resourceSetIdentityIndex.reserveForCount(identityCount);
        for(usize index = 0u; index < sets.size(); ++index)
            (void)m_resourceSetIdentityIndex.emplace(sets[index].identity.identityHash(), static_cast<u32>(index));
        return;
    }
    m_resourceSetIdentityIndex.reserveForCount(identityCount);
}


b8 GpuGraphResourceImportIndex::publishResource(const NameHash& identity, const ResourcePointerKey& pointer, const u32 index, Publication& publication, const ResourceLookupHint* const hint){
    if(!m_resourceIdentityIndex.active())
        return true;
    publication.identity = hint
        ? m_resourceIdentityIndex.emplaceAtHint(identity, index, hint->identity)
        : m_resourceIdentityIndex.emplace(identity, index)
    ;
    if(!publication.identity)
        return false;
    if(pointer.pointer){
        publication.pointer = hint
            ? m_resourcePointerIndex.emplaceAtHint(pointer, index, hint->pointer)
            : m_resourcePointerIndex.emplace(pointer, index)
        ;
        if(!publication.pointer)
            return false;
    }
    return true;
}

void GpuGraphResourceImportIndex::rollbackResource(const NameHash& identity, const ResourcePointerKey& pointer, const Publication& publication)noexcept{
    if(publication.pointer)
        m_resourcePointerIndex.erase(pointer);
    if(publication.identity)
        m_resourceIdentityIndex.erase(identity);
}

b8 GpuGraphResourceImportIndex::publishResourceSet(const NameHash& identity, const u32 index, b8& inserted, const ResourceSetLookupHint* const hint){
    if(!m_resourceSetIdentityIndex.active())
        return true;
    inserted = hint
        ? m_resourceSetIdentityIndex.emplaceAtHint(identity, index, *hint)
        : m_resourceSetIdentityIndex.emplace(identity, index)
    ;
    return inserted;
}

void GpuGraphResourceImportIndex::rollbackResourceSet(const NameHash& identity, const b8 inserted)noexcept{
    if(inserted)
        m_resourceSetIdentityIndex.erase(identity);
}

void GpuGraphResourceImportIndex::clearResources(){
    m_resourceIdentityIndex.clear();
    m_resourcePointerIndex.clear();
}

void GpuGraphResourceImportIndex::clearResourceSets(){
    m_resourceSetIdentityIndex.clear();
}


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

