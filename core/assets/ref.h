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
    constexpr AssetRef() = default;
    explicit constexpr AssetRef(const char* path)
        : virtualPath(path)
    {}
    explicit constexpr AssetRef(const AStringView path)
        : virtualPath(path)
    {}


public:
    [[nodiscard]] bool valid()const noexcept{
        return static_cast<bool>(virtualPath);
    }

    [[nodiscard]] explicit operator bool()const noexcept{
        return valid();
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

