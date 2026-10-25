// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Subsystem;


namespace BuiltInSubsystems{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_RendererText = "engine/subsystems/renderer";
inline constexpr AStringView s_SkinningText = "engine/subsystems/skinning";
inline constexpr AStringView s_TransparentText = "engine/subsystems/transparent";
inline constexpr AStringView s_RefractiveText = "engine/subsystems/refractive";
inline constexpr AStringView s_CsgText = "engine/subsystems/csg";
inline constexpr AStringView s_UiText = "engine/subsystems/ui";
inline constexpr AStringView s_DefaultText = "engine/subsystems/default";

inline constexpr Core::Assets::AssetRef<Subsystem> s_Renderer(s_RendererText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Skinning(s_SkinningText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Transparent(s_TransparentText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Refractive(s_RefractiveText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Csg(s_CsgText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Ui(s_UiText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Default(s_DefaultText);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Subsystem final : public Core::Assets::TypedAsset<Subsystem>{
public:
    using SubsystemRef = Core::Assets::AssetRef<Subsystem>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using AssetMap = HashMap<Name, AssetRef, Core::Assets::AssetArena, Hasher<Name>, EqualTo<Name>>;


public:
    NWB_DEFINE_ASSET_TYPE("subsystem")


public:
    explicit Subsystem(Core::Assets::AssetArena& arena)noexcept
        : m_subsystems(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}
    Subsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Subsystem>(virtualPath)
        , m_subsystems(arena)
        , m_assets(0, Hasher<Name>(), EqualTo<Name>(), arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(Span<const u8> binary);

    void setContents(SubsystemRefVector&& subsystems, AssetMap&& assets)noexcept{
        m_subsystems = Move(subsystems);
        m_assets = Move(assets);
    }

    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const AssetMap& assets()const noexcept{ return m_assets; }

    template<typename TAsset = Core::Assets::IAsset>
    [[nodiscard]] Core::Assets::AssetRef<TAsset> findAsset(const Name& nickname)const noexcept{
        if(!nickname)
            return {};
        const auto found = m_assets.find(nickname);
        if(found == m_assets.end())
            return {};
        Core::Assets::AssetRef<TAsset> result;
        result.virtualPath = found.value().name();
        return result;
    }


private:
    SubsystemRefVector m_subsystems;
    AssetMap m_assets;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SubsystemAssetCodec, Subsystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

