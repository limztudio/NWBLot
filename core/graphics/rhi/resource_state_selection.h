// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "foundation.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GraphicsBackend{
    class VulkanTestDispatchAccess;
};

// Operation-owned membership for whole-resource state filtering. Entries keep the first input ordinal per
// kind/pointer identity; resources stay caller-owned. Finish adding before other scratch allocations.
class CommandListResourceSelection final : NoCopy{
    friend class GraphicsBackend::VulkanTestDispatchAccess;

public:
    struct Entry{
        void* resource;
        usize inputIndex;
        bool texture;
    };


private:
    static constexpr usize s_InlineCapacity = 32u;
    static constexpr usize s_BytesPerEntry = sizeof(Entry) + sizeof(usize) * 2u;

    [[nodiscard]] static usize hashIdentity(void* resource, bool texture)noexcept;
    static void insertIndex(void* storage, usize capacity, usize entryIndex)noexcept;


public:
    explicit CommandListResourceSelection(Alloc::ScratchArena& scratchArena)
        : m_scratchArena(scratchArena)
    {}
    ~CommandListResourceSelection();
    CommandListResourceSelection(CommandListResourceSelection&&) = delete;


public:
    [[nodiscard]] bool addTexture(Texture* texture, usize inputIndex);
    [[nodiscard]] bool addBuffer(Buffer* buffer, usize inputIndex);
    [[nodiscard]] bool containsTexture(Texture* texture)const noexcept{ return texture && containsResource(texture, true); }
    [[nodiscard]] bool containsBuffer(Buffer* buffer)const noexcept{ return buffer && containsResource(buffer, false); }
    [[nodiscard]] bool valid()const noexcept{ return m_valid; }
    [[nodiscard]] const Entry* entries()const noexcept{ return m_storage ? static_cast<const Entry*>(m_storage) : m_inlineEntries; }
    [[nodiscard]] usize size()const noexcept{ return m_size; }
    [[nodiscard]] usize textureCount()const noexcept{ return m_textureCount; }
    [[nodiscard]] usize bufferCount()const noexcept{ return m_size - m_textureCount; }


private:
    [[nodiscard]] bool addResource(void* resource, usize inputIndex, bool texture);
    [[nodiscard]] bool containsResource(void* resource, bool texture)const noexcept{
        if(m_storage)
            return containsIndexedResource(resource, texture);
        const usize begin = texture ? 0u : m_textureCount;
        const usize end = texture ? m_textureCount : m_size;
        for(usize index = begin; index < end; ++index){
            if(m_inlineIdentities[index] == resource)
                return true;
        }
        return false;
    }
    [[nodiscard]] bool containsIndexedResource(void* resource, bool texture)const noexcept;
    [[nodiscard]] bool grow();


private:
    Alloc::ScratchArena& m_scratchArena;
    Entry m_inlineEntries[s_InlineCapacity];
    void* m_inlineIdentities[s_InlineCapacity];
    void* m_storage = nullptr;
    usize m_capacity = s_InlineCapacity;
    usize m_size = 0u;
    usize m_textureCount = 0u;
    bool m_valid = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

