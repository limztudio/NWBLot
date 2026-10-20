// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "containers.h"
#include "overflow.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One fixed allocation preserves LIFO reclamation when the caller uses a scratch arena.
template<typename Key, typename ArenaT, typename Hash = Hasher<Key>, typename Equal = EqualTo<Key>>
class FixedIndexMap final : NoCopy{
private:
    struct Slot{
        Key key{};
        usize value = 0u;
        bool occupied = false;
    };


public:
    FixedIndexMap(ArenaT& arena, const usize maximumEntryCount, const Hash& hash = Hash(), const Equal& equal = Equal())
        : m_slots(arena)
        , m_hash(hash)
        , m_equal(equal)
    {
        if(maximumEntryCount == 0u)
            return;

        if(AddOverflows<usize>(maximumEntryCount, maximumEntryCount)){
            m_valid = false;
            return;
        }
        const usize doubledEntryCount = maximumEntryCount + maximumEntryCount;
        if(AddOverflows<usize>(doubledEntryCount, 1u)){
            m_valid = false;
            return;
        }
        m_slots.resize(doubledEntryCount + 1u);
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_valid; }

    [[nodiscard]] const usize* find(const Key& key)const noexcept(noexcept(static_cast<usize>(m_hash(key))) && noexcept(static_cast<bool>(m_equal(key, key)))){
        if(m_slots.empty())
            return nullptr;

        usize slotIndex = m_hash(key) % m_slots.size();
        for(usize probedSlotCount = 0u; probedSlotCount < m_slots.size(); ++probedSlotCount){
            const Slot& slot = m_slots[slotIndex];
            if(!slot.occupied)
                return nullptr;
            if(m_equal(slot.key, key))
                return &slot.value;
            if(++slotIndex == m_slots.size())
                slotIndex = 0u;
        }
        return nullptr;
    }

    [[nodiscard]] bool insertOrAssign(const Key& key, const usize value)noexcept(noexcept(static_cast<usize>(m_hash(key))) && noexcept(static_cast<bool>(m_equal(key, key))) && noexcept(DeclVal<Key&>() = key)){
        if(m_slots.empty())
            return false;

        usize slotIndex = m_hash(key) % m_slots.size();
        for(usize probedSlotCount = 0u; probedSlotCount < m_slots.size(); ++probedSlotCount){
            Slot& slot = m_slots[slotIndex];
            if(!slot.occupied){
                slot.key = key;
                slot.value = value;
                slot.occupied = true;
                return true;
            }
            if(m_equal(slot.key, key)){
                slot.value = value;
                return true;
            }
            if(++slotIndex == m_slots.size())
                slotIndex = 0u;
        }
        return false;
    }


private:
    Vector<Slot, ArenaT> m_slots;
    Hash m_hash;
    Equal m_equal;
    bool m_valid = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

