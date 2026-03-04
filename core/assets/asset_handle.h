// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
struct AssetHandle{
    static constexpr u32 s_InvalidIndex = static_cast<u32>(-1);

    u32 index = s_InvalidIndex;
    u32 generation = 0;


public:
    [[nodiscard]] bool valid()const{
        return index != s_InvalidIndex && generation != 0;
    }

    [[nodiscard]] explicit operator bool()const{
        return valid();
    }

    void reset(){
        index = s_InvalidIndex;
        generation = 0;
    }
};
template<typename TAsset>
[[nodiscard]] inline bool operator==(const AssetHandle<TAsset>& lhs, const AssetHandle<TAsset>& rhs){
    return lhs.index == rhs.index && lhs.generation == rhs.generation;
}
template<typename TAsset>
[[nodiscard]] inline bool operator!=(const AssetHandle<TAsset>& lhs, const AssetHandle<TAsset>& rhs){
    return !(lhs == rhs);
}


template<typename TAsset>
struct AssetHandleHasher{
    [[nodiscard]] usize operator()(const AssetHandle<TAsset>& handle)const{
        constexpr usize s_HashMixPrime = 16777619u;

        usize value = static_cast<usize>(handle.index);
        value = value * s_HashMixPrime + static_cast<usize>(handle.generation);
        return value;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

