// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <impl/assets_font/asset.h>
#include <impl/assets_font_atlas/asset.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_subsystem/asset.h>
#include <impl/assets_texture/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSubsystem final : public TypedSubsystem<UiSubsystem>{
public:
    NWB_DEFINE_ASSET_TYPE("ui_subsystem")


public:
    using TypedSubsystem<UiSubsystem>::TypedSubsystem;


public:
    Core::Assets::AssetRef<PixelShader> outputPs;
    Core::Assets::AssetRef<VertexShader> outputVs;
    Core::Assets::AssetRef<PixelShader> ps;
    Core::Assets::AssetRef<VertexShader> vs;
    Core::Assets::AssetRef<FontAtlas> defaultKoreanAtlas;
    Core::Assets::AssetRef<Font> defaultKoreanFace;
    Core::Assets::AssetRef<FontAtlas> defaultLatinAtlas;
    Core::Assets::AssetRef<Font> defaultLatinFace;
    Core::Assets::AssetRef<UiSkin> defaultSkin;
    Core::Assets::AssetRef<Texture> defaultSkinTexture;


public:
    static constexpr SubsystemFieldDescriptor<UiSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::outputPs>("output_ps"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::outputVs>("output_vs"),
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::ps>("ps"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::vs>("vs"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultKoreanAtlas>("default_korean_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultKoreanFace>("default_korean_face"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultLatinAtlas>("default_latin_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultLatinFace>("default_latin_face"),
        MakeSubsystemFieldDescriptor<UiSubsystem, UiSkin, &UiSubsystem::defaultSkin>("default_skin"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Texture, &UiSubsystem::defaultSkinTexture>("default_skin_texture"),
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

