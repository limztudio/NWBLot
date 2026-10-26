// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <impl/assets_font_atlas/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSubsystem final : public TypedSubsystem<UiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("ui_subsystem")


public:
    explicit UiSubsystem(Core::Assets::AssetArena& arena)noexcept
        : TypedSubsystem(arena)
        , basicFonts(arena)
    {}
    UiSubsystem(Core::Assets::AssetArena& arena, const Name& virtualPath)noexcept
        : TypedSubsystem(arena, virtualPath)
        , basicFonts(arena)
    {}


public:
    SubsystemAssetRefVector<FontAtlas> basicFonts;
    Core::Assets::AssetRef<UiSkin> basicSkin;
    Core::Assets::AssetRef<PixelShader> outputPs;
    Core::Assets::AssetRef<VertexShader> outputVs;
    Core::Assets::AssetRef<PixelShader> ps;
    Core::Assets::AssetRef<VertexShader> vs;


public:
    static constexpr SubsystemFieldDescriptor<UiSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::basicFonts>("basic_fonts"),
        MakeSubsystemFieldDescriptor<UiSubsystem, UiSkin, &UiSubsystem::basicSkin>("basic_skin"),
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::outputPs>("output_ps"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::outputVs>("output_vs"),
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::ps>("ps"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::vs>("vs"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

