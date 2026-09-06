// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TrackedCommandBuffer;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SubmittedCommandBufferOwnerLookup final : NoCopy{
private:
    struct OwnerEntry{
        TrackedCommandBuffer* owner = nullptr;
        u64 nativeRecordingID = 0u;
    };
    using OwnerEntries = Vector<OwnerEntry, Alloc::GlobalArena>;


private:
    static constexpr usize s_LinearSearchThreshold = 8u;
    static constexpr usize s_MinimumTableCapacity = 16u;

    [[nodiscard]] static usize hashOwner(const TrackedCommandBuffer& owner)noexcept{
        u64 value = static_cast<u64>(reinterpret_cast<usize>(&owner));
        value ^= value >> 33u;
        value *= 0xff51afd7ed558ccdull;
        value ^= value >> 33u;
        value *= 0xc4ceb9fe1a85ec53ull;
        value ^= value >> 33u;
        return static_cast<usize>(value);
    }
    [[nodiscard]] static usize resolveTableCapacity(const usize ownerCount)noexcept{
        if(ownerCount <= s_LinearSearchThreshold || ownerCount > Limit<usize>::s_Max / 2u)
            return 0u;

        const usize minimumCapacity = ownerCount * 2u;
        usize tableCapacity = s_MinimumTableCapacity;
        while(tableCapacity < minimumCapacity){
            if(tableCapacity > Limit<usize>::s_Max / 2u)
                return 0u;
            tableCapacity *= 2u;
        }
        return tableCapacity;
    }


public:
    explicit SubmittedCommandBufferOwnerLookup(Alloc::GlobalArena& arena)
        : m_ownerEntries(arena)
    {}


public:
    void prepare(const usize ownerCount){
        m_ownerEntries.clear();
        m_ownerEntriesData = nullptr;
        m_ownerEntryCapacity = 0u;
        m_ownerEntryMask = 0u;
        m_indexed = false;

        const usize tableCapacity = resolveTableCapacity(ownerCount);
        if(tableCapacity == 0u)
            return;

        m_ownerEntries.resize(tableCapacity);
        m_ownerEntriesData = m_ownerEntries.data();
        m_ownerEntryCapacity = tableCapacity;
        m_ownerEntryMask = tableCapacity - 1u;
        m_indexed = true;
    }
    void add(TrackedCommandBuffer& owner, const u64 nativeRecordingID){
        if(!m_indexed || nativeRecordingID == 0u)
            return;

        usize index = hashOwner(owner) & m_ownerEntryMask;
        for(usize probe = 0u; probe < m_ownerEntryCapacity; ++probe){
            OwnerEntry& entry = m_ownerEntriesData[index];
            if(!entry.owner || entry.owner == &owner){
                entry.owner = &owner;
                entry.nativeRecordingID = nativeRecordingID;
                return;
            }
            index = (index + 1u) & m_ownerEntryMask;
        }
        m_indexed = false;
    }
    [[nodiscard]] bool indexed()const noexcept{ return m_indexed; }
    [[nodiscard]] bool contains(TrackedCommandBuffer& owner, const u64 nativeRecordingID)const noexcept{
        if(!m_indexed || !m_ownerEntriesData || nativeRecordingID == 0u)
            return false;

        usize index = hashOwner(owner) & m_ownerEntryMask;
        for(usize probe = 0u; probe < m_ownerEntryCapacity; ++probe){
            const OwnerEntry& entry = m_ownerEntriesData[index];
            if(!entry.owner)
                return false;
            if(entry.owner == &owner)
                return entry.nativeRecordingID == nativeRecordingID;
            index = (index + 1u) & m_ownerEntryMask;
        }
        return false;
    }


private:
    OwnerEntries m_ownerEntries;
    OwnerEntry* m_ownerEntriesData = nullptr;
    usize m_ownerEntryCapacity = 0u;
    usize m_ownerEntryMask = 0u;
    bool m_indexed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

