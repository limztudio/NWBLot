// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "resource_state_selection.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize CommandListResourceSelection::hashIdentity(void* const resource, const bool texture)noexcept{
    return Hasher<void*>()(resource) ^ (texture ? static_cast<usize>(0x9e3779b97f4a7c15ull) : 0u);
}

void CommandListResourceSelection::insertIndex(void* const storage, const usize capacity, const usize entryIndex)noexcept{
    const Entry* const entries = static_cast<const Entry*>(storage);
    usize* const buckets = reinterpret_cast<usize*>(static_cast<u8*>(storage) + capacity * sizeof(Entry));
    const usize bucketCount = capacity * 2u;
    const usize mask = bucketCount - 1u;
    usize bucket = hashIdentity(entries[entryIndex].resource, entries[entryIndex].texture) & mask;
    for(usize probe = 0u; probe < bucketCount; ++probe){
        if(buckets[bucket] == 0u){
            buckets[bucket] = entryIndex + 1u;
            return;
        }
        bucket = (bucket + 1u) & mask;
    }
    NWB_ASSERT_MSG(false, NWB_TEXT("Resource selection index exceeded its reserved load bound"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CommandListResourceSelection::~CommandListResourceSelection(){
    if(m_storage)
        m_scratchArena.deallocate(m_storage, alignof(Entry), m_capacity * s_BytesPerEntry);
}

bool CommandListResourceSelection::addTexture(Texture* const texture, const usize inputIndex){
    return addResource(texture, inputIndex, true);
}

bool CommandListResourceSelection::addBuffer(Buffer* const buffer, const usize inputIndex){
    return addResource(buffer, inputIndex, false);
}

bool CommandListResourceSelection::addResource(void* const resource, const usize inputIndex, const bool texture){
    if(!m_valid)
        return false;
    if(!resource || containsResource(resource, texture))
        return true;
    if(m_size == m_capacity && !grow()){
        m_valid = false;
        return false;
    }
    Entry* const entries = m_storage ? static_cast<Entry*>(m_storage) : m_inlineEntries;
    new(entries + m_size) Entry{ resource, inputIndex, texture };
    if(m_storage)
        insertIndex(m_storage, m_capacity, m_size);
    else if(texture){
        // Keep each kind contiguous for small lookups without changing the first-occurrence entry order.
        for(usize index = m_size; index > m_textureCount; --index)
            m_inlineIdentities[index] = m_inlineIdentities[index - 1u];
        m_inlineIdentities[m_textureCount] = resource;
    }
    else
        m_inlineIdentities[m_size] = resource;
    ++m_size;
    if(texture)
        ++m_textureCount;
    return true;
}

bool CommandListResourceSelection::containsIndexedResource(void* const resource, const bool texture)const noexcept{
    const Entry* const values = static_cast<const Entry*>(m_storage);
    const usize* const buckets = reinterpret_cast<const usize*>(static_cast<const u8*>(m_storage) + m_capacity * sizeof(Entry));
    const usize bucketCount = m_capacity * 2u;
    const usize mask = bucketCount - 1u;
    usize bucket = hashIdentity(resource, texture) & mask;
    for(usize probe = 0u; probe < bucketCount; ++probe){
        const usize encodedIndex = buckets[bucket];
        if(encodedIndex == 0u)
            return false;
        const Entry& entry = values[encodedIndex - 1u];
        if(entry.resource == resource && entry.texture == texture)
            return true;
        bucket = (bucket + 1u) & mask;
    }
    return false;
}

bool CommandListResourceSelection::grow(){
    static_assert(IsTriviallyCopyable_V<Entry> && IsStandardLayout_V<Entry>);
    static_assert(alignof(Entry) >= alignof(usize) && sizeof(Entry) % alignof(usize) == 0u);
    if(MultiplyOverflows<usize>(m_capacity, 2u))
        return false;
    const usize capacity = m_capacity * 2u;
    if(MultiplyOverflows<usize>(capacity, s_BytesPerEntry))
        return false;
    const usize bytes = capacity * s_BytesPerEntry;
    void* const storage = m_storage
        ? m_scratchArena.reallocate(m_storage, alignof(Entry), bytes)
        : m_scratchArena.allocate(alignof(Entry), bytes)
    ;
    if(!storage)
        return false;

    // Entries occupy the prefix and survive trivial relocation. Growing moves only the bucket region, which is
    // rebuilt before publication. Reallocate releases its former top block even when it creates a new arena chunk.
    for(usize index = 0u; index < m_size; ++index){
        Entry entry;
        const void* const source = m_storage
            ? static_cast<const void*>(static_cast<const u8*>(storage) + index * sizeof(Entry))
            : &m_inlineEntries[index]
        ;
        NWB_MEMCPY(&entry, sizeof(entry), source, sizeof(entry));
        new(static_cast<Entry*>(storage) + index) Entry(entry);
    }
    usize* const buckets = reinterpret_cast<usize*>(static_cast<u8*>(storage) + capacity * sizeof(Entry));
    new(buckets) usize[capacity * 2u]{};
    for(usize index = 0u; index < m_size; ++index)
        insertIndex(storage, capacity, index);
    m_storage = storage;
    m_capacity = capacity;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

