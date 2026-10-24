// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BuiltInSubsystemFeatures{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_SkinningText = "skinning";
inline constexpr AStringView s_TransparentText = "transparent";
inline constexpr AStringView s_RefractiveText = "refractive";
inline constexpr AStringView s_CsgText = "csg";

inline constexpr Name s_Skinning(s_SkinningText);
inline constexpr Name s_Transparent(s_TransparentText);
inline constexpr Name s_Refractive(s_RefractiveText);
inline constexpr Name s_Csg(s_CsgText);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Subsystem;


namespace BuiltInSubsystems{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr AStringView s_RendererText = "engine/subsystems/renderer";
inline constexpr AStringView s_UiText = "engine/subsystems/ui";
inline constexpr AStringView s_DefaultText = "engine/subsystems/default";

inline constexpr Core::Assets::AssetRef<Subsystem> s_Renderer(s_RendererText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Ui(s_UiText);
inline constexpr Core::Assets::AssetRef<Subsystem> s_Default(s_DefaultText);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SubsystemFeature{
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using AssetRefVector = Core::Assets::AssetVector<AssetRef>;


    Name name = s_NameNone;
    AssetRefVector assets;


public:
    explicit SubsystemFeature(Core::Assets::AssetArena& arena)noexcept
        : assets(arena)
    {}
    SubsystemFeature(const Name& inName, AssetRefVector&& inAssets)noexcept
        : name(inName)
        , assets(Move(inAssets))
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Subsystem final : public Core::Assets::TypedAsset<Subsystem>{
public:
    using SubsystemRef = Core::Assets::AssetRef<Subsystem>;
    using AssetRef = Core::Assets::AssetRef<Core::Assets::IAsset>;
    using SubsystemRefVector = Core::Assets::AssetVector<SubsystemRef>;
    using AssetRefVector = Core::Assets::AssetVector<AssetRef>;
    using FeatureVector = Core::Assets::AssetVector<SubsystemFeature>;


public:
    NWB_DEFINE_ASSET_TYPE("subsystem")


public:
    explicit Subsystem(Core::Assets::AssetArena& arena)noexcept
        : m_subsystems(arena)
        , m_assets(arena)
        , m_features(arena)
    {}
    Subsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : Core::Assets::TypedAsset<Subsystem>(virtualPath)
        , m_subsystems(arena)
        , m_assets(arena)
        , m_features(arena)
    {}


public:
    [[nodiscard]] bool validatePayload(Core::Alloc::ScratchArena& scratchArena)const;
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setContents(SubsystemRefVector&& subsystems, AssetRefVector&& assets, FeatureVector&& features)noexcept{
        m_subsystems = Move(subsystems);
        m_assets = Move(assets);
        m_features = Move(features);
    }

public:
    [[nodiscard]] const SubsystemRefVector& subsystems()const noexcept{ return m_subsystems; }
    [[nodiscard]] const AssetRefVector& assets()const noexcept{ return m_assets; }
    [[nodiscard]] const FeatureVector& features()const noexcept{ return m_features; }

    [[nodiscard]] const SubsystemFeature* findFeature(const Name& featureName)const noexcept{
        if(!featureName)
            return nullptr;
        for(const SubsystemFeature& feature : m_features){
            if(feature.name == featureName)
                return &feature;
        }
        return nullptr;
    }


private:
    SubsystemRefVector m_subsystems;
    AssetRefVector m_assets;
    FeatureVector m_features;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(SubsystemAssetCodec, Subsystem);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

