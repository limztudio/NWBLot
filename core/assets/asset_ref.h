// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
struct AssetRef{
    Name virtualPath = NAME_NONE;


public:
    constexpr AssetRef() = default;
    constexpr AssetRef(const Name& value)
        : virtualPath(value)
    {}

    explicit AssetRef(const CompactString& value){
        set(value);
    }


public:
    [[nodiscard]] bool valid()const{
        return static_cast<bool>(virtualPath);
    }

    [[nodiscard]] explicit operator bool()const{
        return valid();
    }

    void reset(){
        virtualPath = NAME_NONE;
    }

    AssetRef& set(const Name& value){
        virtualPath = value;
        return *this;
    }

    AssetRef& set(const CompactString& value){
        if(value.empty()){
            virtualPath = NAME_NONE;
            return *this;
        }

        virtualPath = Name(value.view());
        return *this;
    }

    [[nodiscard]] const Name& name()const{
        return virtualPath;
    }
};


template<typename TAsset>
[[nodiscard]] inline bool operator==(const AssetRef<TAsset>& lhs, const AssetRef<TAsset>& rhs){
    return lhs.virtualPath == rhs.virtualPath;
}
template<typename TAsset>
[[nodiscard]] inline bool operator!=(const AssetRef<TAsset>& lhs, const AssetRef<TAsset>& rhs){
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

