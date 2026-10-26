// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "asset.h"

#include <impl/assets_subsystem/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VertexShader;
class PixelShader;
class Font;
class FontAtlas;


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

    static constexpr SubsystemFieldDescriptor<UiSubsystem> s_Fields[] = {
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::outputPs>("output_ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::outputVs>("output_vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, PixelShader, &UiSubsystem::ps>("ps", "pixel_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, VertexShader, &UiSubsystem::vs>("vs", "vertex_shader"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultKoreanAtlas>("default_korean_atlas", "font_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultKoreanFace>("default_korean_face", "font"),
        MakeSubsystemFieldDescriptor<UiSubsystem, FontAtlas, &UiSubsystem::defaultLatinAtlas>("default_latin_atlas", "font_atlas"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Font, &UiSubsystem::defaultLatinFace>("default_latin_face", "font"),
        MakeSubsystemFieldDescriptor<UiSubsystem, UiSkin, &UiSubsystem::defaultSkin>("default_skin", "ui_skin"),
        MakeSubsystemFieldDescriptor<UiSubsystem, Texture, &UiSubsystem::defaultSkinTexture>("default_skin_texture", "texture"),
    };
};

using UiSubsystemAssetCodec = TypedSubsystemAssetCodec<UiSubsystem>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
