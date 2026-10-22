// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TAsset>
struct AssetRef{
    Name virtualPath = s_NameNone;


public:
    [[nodiscard]] static Expected<AssetRef, NameTextError::Enum> FromText(const AStringView path){
        const auto name = Name::FromText(path);
        if(!name)
            return MakeUnexpected(name.error());
        AssetRef reference;
        reference.virtualPath = *name;
        return reference;
    }


public:
    constexpr AssetRef()noexcept = default;
    explicit constexpr AssetRef(const char* path)noexcept
        : virtualPath(path)
    {}
    explicit constexpr AssetRef(const NameText<char> path)
        : virtualPath(path)
    {}


public:
    [[nodiscard]] explicit operator bool()const noexcept{
        return valid();
    }


public:
    [[nodiscard]] bool valid()const noexcept{
        return static_cast<bool>(virtualPath);
    }


    void reset()noexcept{
        virtualPath = s_NameNone;
    }

    [[nodiscard]] const Name& name()const noexcept{
        return virtualPath;
    }
};


template<typename TAsset>
[[nodiscard]] inline bool operator==(const AssetRef<TAsset>& lhs, const AssetRef<TAsset>& rhs)noexcept{
    return lhs.virtualPath == rhs.virtualPath;
}

template<typename TAsset>
[[nodiscard]] inline bool operator!=(const AssetRef<TAsset>& lhs, const AssetRef<TAsset>& rhs)noexcept{
    return !(lhs == rhs);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

