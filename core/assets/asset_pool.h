// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset_handle.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
class AssetPool final : NoCopy{
public:
    using AssetType = TAsset;
    using HandleType = AssetHandle<AssetType>;


private:
    struct Slot{
        UniquePtr<AssetType> asset;
        u32 generation = 1;
        bool occupied = false;
    };

    [[nodiscard]] bool validHandle(HandleType handle)const;


public:
    [[nodiscard]] HandleType insert(UniquePtr<AssetType>&& asset);

    template<typename... Args>
    [[nodiscard]] HandleType emplace(Args&&... args);

    bool remove(HandleType handle);
    void clear();

    [[nodiscard]] AssetType* find(HandleType handle);
    [[nodiscard]] const AssetType* find(HandleType handle)const;
    [[nodiscard]] bool contains(HandleType handle)const;

    [[nodiscard]] u32 liveCount()const;
    [[nodiscard]] bool empty()const;


private:
    Vector<Slot> m_slots;
    Vector<u32> m_freeIndices;
    u32 m_liveCount = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
bool AssetPool<TAsset>::validHandle(const HandleType handle)const{
    if(!handle.valid())
        return false;
    if(handle.index >= m_slots.size())
        return false;

    const Slot& slot = m_slots[handle.index];
    if(!slot.occupied)
        return false;
    if(slot.generation != handle.generation)
        return false;

    return slot.asset != nullptr;
}


template<typename TAsset>
typename AssetPool<TAsset>::HandleType AssetPool<TAsset>::insert(UniquePtr<AssetType>&& asset){
    if(!asset)
        return HandleType{};

    u32 index = HandleType::s_InvalidIndex;
    if(!m_freeIndices.empty()){
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
    }
    else{
        if(m_slots.size() >= static_cast<usize>(HandleType::s_InvalidIndex)){
            NWB_ASSERT_MSG(false, NWB_TEXT("AssetPool exceeded maximum handle count"));
            return HandleType{};
        }
        m_slots.push_back(Slot{});
        index = static_cast<u32>(m_slots.size() - 1);
    }

    Slot& slot = m_slots[index];
    slot.asset = Move(asset);
    slot.occupied = true;
    if(slot.generation == 0)
        slot.generation = 1;

    ++m_liveCount;
    return HandleType{
        index,
        slot.generation
    };
}


template<typename TAsset>
template<typename... Args>
typename AssetPool<TAsset>::HandleType AssetPool<TAsset>::emplace(Args&&... args){
    return insert(MakeUnique<AssetType>(Forward<Args>(args)...));
}


template<typename TAsset>
bool AssetPool<TAsset>::remove(const HandleType handle){
    if(!validHandle(handle))
        return false;

    Slot& slot = m_slots[handle.index];
    slot.asset.reset();
    slot.occupied = false;

    ++slot.generation;
    if(slot.generation == 0)
        slot.generation = 1;

    m_freeIndices.push_back(handle.index);
    --m_liveCount;
    return true;
}


template<typename TAsset>
void AssetPool<TAsset>::clear(){
    m_slots.clear();
    m_freeIndices.clear();
    m_liveCount = 0;
}


template<typename TAsset>
typename AssetPool<TAsset>::AssetType* AssetPool<TAsset>::find(const HandleType handle){
    if(!validHandle(handle))
        return nullptr;

    return m_slots[handle.index].asset.get();
}


template<typename TAsset>
const typename AssetPool<TAsset>::AssetType* AssetPool<TAsset>::find(const HandleType handle)const{
    if(!validHandle(handle))
        return nullptr;

    return m_slots[handle.index].asset.get();
}


template<typename TAsset>
bool AssetPool<TAsset>::contains(const HandleType handle)const{
    return validHandle(handle);
}


template<typename TAsset>
u32 AssetPool<TAsset>::liveCount()const{
    return m_liveCount;
}


template<typename TAsset>
bool AssetPool<TAsset>::empty()const{
    return m_liveCount == 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

