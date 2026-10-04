// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_ui_skin/binary_payload.h>
#include <impl/assets_ui_skin/cook.h>
#include <impl/assets_texture/cook.h>

#include <core/assets/auto_registration.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_ui_skin_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct UiSkinTestArenaTag{};
using SkinTestArena = TestArena<UiSkinTestArenaTag>;

static constexpr Name s_ScratchArena("tests/integration/assets_ui_skin/cook");
static constexpr AStringView s_Metadata =
    "ui_skin asset;\r\n"
    "asset.texture = \"project/ui/texture\";\r\n"
    "asset.atlas_extent = [64, 32];\r\n"
    "asset.reference_density = 2.0;\r\n"
    "asset.regions = [\r\n"
    "  { \"name\": \"panel.normal\", \"rect\": [2, 4, 24, 20], \"draw_mode\": \"nine_slice\",\r\n"
    "    \"slice\": [3, 4, 5, 6], \"padding\": [1.0, 2.0, 3.0, 4.0], \"minimum_size\": [8.0, 10.0] },\r\n"
    "  { \"name\": \"combo.arrow\", \"rect\": [28, 4, 12, 8] },\r\n"
    "];\r\n"
;
static constexpr AStringView s_PaletteRoleNames[] = {
    "text.normal", "text.disabled", "text.tooltip", "edit.background", "edit.selection",
    "edit.inactive_selection", "edit.caret", "edit.preedit", "scrollbar.track", "scrollbar.thumb",
    "scrollbar.disabled", "popup.backdrop", "control.hover_tint", "control.pressed_tint",
    "control.disabled_tint", "progress.track_tint", "progress.fill_tint",
};
static_assert(LengthOf(s_PaletteRoleNames) == UiSkinColorRole::Count, "UI skin palette role fixture drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ParseMetadata(SkinTestArena& testArena, const AStringView metadata, UiSkinCookEntry& entry){
    Core::Metascript::Document document(testArena.arena);
    if(!document.parse(metadata))
        return false;
    const Path assetRoot(testArena.arena, "C:/ui_skin_tests/assets");
    const Path metadataPath = assetRoot / "ui" / "atlas.nwb";
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    return ParseUiSkinCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena);
}

[[nodiscard]] static Core::Assets::AssetBytes MakeBinary(
    SkinTestArena& testArena,
    const UiSkinBinaryPayload::HeaderBinary& header,
    const UiSkinBinaryPayload::RegionBinary& region){
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header) + sizeof(region)
        + UiSkinColorRole::Count * sizeof(UiSkinBinaryPayload::ColorBinary) + sizeof(UiSkinBinaryPayload::TypographyBinary));
    AppendPOD(binary, header);
    AppendPOD(binary, region);
    for(const UiSkinColor& color : UiSkinPalette{}.colors)
        AppendPOD(binary, UiSkinBinaryPayload::ColorBinary{ color.r, color.g, color.b, color.a });
    AppendPOD(binary, UiSkinBinaryPayload::TypographyBinary{});
    return binary;
}

[[nodiscard]] static UiSkinBinaryPayload::HeaderBinary ValidHeader(){
    UiSkinBinaryPayload::HeaderBinary header;
    header.textureNameHash = Name("project/ui/texture").hash();
    header.atlasWidth = 64u;
    header.atlasHeight = 32u;
    header.referenceDensity = 2.0f;
    header.regionCount = 1u;
    return header;
}

[[nodiscard]] static UiSkinBinaryPayload::RegionBinary ValidRegion(){
    UiSkinBinaryPayload::RegionBinary region;
    region.nameHash = Name("panel.normal").hash();
    region.x = 2u;
    region.y = 4u;
    region.width = 24u;
    region.height = 20u;
    region.sliceLeft = 3u;
    region.sliceTop = 4u;
    region.sliceRight = 5u;
    region.sliceBottom = 6u;
    region.drawMode = UiSkinDrawMode::NineSlice;
    return region;
}

[[nodiscard]] static TestAString PaletteMetadata(const u32 variant = 0u, const bool includeTypography = true){
    TestAString metadata(s_Metadata);
    if(includeTypography)
        metadata.append("asset.typography = {\"default_font_size\": 16.0};\r\n");
    if(variant == 1u)
        return metadata;
    metadata.append("asset.colors = [\r\n");
    const usize count = variant == 2u ? UiSkinColorRole::Count - 1u : UiSkinColorRole::Count;
    for(usize index = 0u; index < count; ++index){
        const AStringView name = variant == 3u && index == count - 1u
            ? s_PaletteRoleNames[0u]
            : variant == 4u && index == count - 1u ? AStringView("unknown.role") : s_PaletteRoleNames[index];
        metadata.append("  { \"name\": \"");
        metadata.append(name);
        metadata.append("\", \"rgba\": ");
        metadata.append(variant == 5u && index == 0u
            ? "[16.1, 0.5, 0.75, 1.0]"
            : variant == 6u && index == 0u ? "[0.25, 0.5, 0.75, 1.1]"
            : index == UiSkinColorRole::ControlHoverTint ? "[1.08, 1.08, 1.08, 1.0]" : "[0.25, 0.5, 0.75, 1.0]");
        metadata.append(" },\r\n");
    }
    metadata.append("];\r\n");
    return metadata;
}

[[nodiscard]] static TestAString TypographyMetadata(const AStringView typography = "{\"default_font_size\": 21.5}"){
    TestAString metadata = PaletteMetadata(0u, false);
    if(!typography.empty()){
        metadata.append("asset.typography = ");
        metadata.append(typography);
        metadata.append(";\r\n");
    }
    return metadata;
}

[[nodiscard]] static Core::Assets::AssetBytes MakeTypographyBinary(
    SkinTestArena& testArena,
    const UiSkinBinaryPayload::ColorBinary& color,
    const f32 defaultFontSize){
    UiSkinBinaryPayload::HeaderBinary header = ValidHeader();
    Core::Assets::AssetBytes binary = MakeBinary(testArena, header, ValidRegion());
    binary.resize(sizeof(header) + sizeof(UiSkinBinaryPayload::RegionBinary));
    for(u32 index = 0u; index < UiSkinColorRole::Count; ++index)
        AppendPOD(binary, color);
    UiSkinBinaryPayload::TypographyBinary typography;
    typography.defaultFontSize = defaultFontSize;
    AppendPOD(binary, typography);
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsUiSkin, CookAndCodecRoundTripPreservesAtlasAndMetrics){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, PaletteMetadata(), entry));
    EXPECT_EQ(entry.virtualPath, Name("project/ui/atlas"));
    UiSkin skin(testArena.arena);
    ASSERT_TRUE(BuildUiSkinAsset(entry, skin));
    EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 16.0f);

    UiSkinAssetCodec codec;
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(skin, binary));
    EXPECT_EQ(binary.size(), sizeof(UiSkinBinaryPayload::HeaderBinary) + 2u * sizeof(UiSkinBinaryPayload::RegionBinary)
        + UiSkinColorRole::Count * sizeof(UiSkinBinaryPayload::ColorBinary) + sizeof(UiSkinBinaryPayload::TypographyBinary));
    UiSkinBinaryPayload::HeaderBinary serializedHeader;
    usize cursor = 0u;
    ASSERT_TRUE(ReadPOD(binary, cursor, serializedHeader));
    EXPECT_EQ(serializedHeader.version, UiSkinBinaryPayload::s_UiSkinVersion);
    UniquePtr<Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(codec.deserialize(testArena.arena, skin.virtualPath(), binary, loadedAsset));
    const UiSkin* loaded = Core::Assets::CastAsset<UiSkin>(loadedAsset.get());
    ASSERT_NE(loaded, nullptr);
    EXPECT_FLOAT_EQ(loaded->typography().defaultFontSize, 16.0f);
    EXPECT_EQ(loaded->texture().name(), Name("project/ui/texture"));
    EXPECT_EQ(loaded->atlasWidth(), 64u);
    EXPECT_EQ(loaded->atlasHeight(), 32u);
    EXPECT_FLOAT_EQ(loaded->referenceDensity(), 2.0f);
    ASSERT_EQ(loaded->regions().size(), 2u);
    const UiSkinRegion* panel = loaded->findRegion(Name("PANEL.NORMAL"));
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->rectangle.x, 2u);
    EXPECT_EQ(panel->rectangle.y, 4u);
    EXPECT_EQ(panel->rectangle.width, 24u);
    EXPECT_EQ(panel->rectangle.height, 20u);
    EXPECT_EQ(panel->sliceInsets.left, 3u);
    EXPECT_EQ(panel->sliceInsets.top, 4u);
    EXPECT_EQ(panel->sliceInsets.right, 5u);
    EXPECT_EQ(panel->sliceInsets.bottom, 6u);
    EXPECT_FLOAT_EQ(panel->padding.left, 1.0f);
    EXPECT_FLOAT_EQ(panel->padding.top, 2.0f);
    EXPECT_FLOAT_EQ(panel->padding.right, 3.0f);
    EXPECT_FLOAT_EQ(panel->padding.bottom, 4.0f);
    EXPECT_FLOAT_EQ(panel->minimumWidth, 8.0f);
    EXPECT_FLOAT_EQ(panel->minimumHeight, 10.0f);
    EXPECT_EQ(panel->drawMode, UiSkinDrawMode::NineSlice);
    EXPECT_EQ(loaded->findRegion(Name("missing")), nullptr);
    const UiSkinRegion* arrow = loaded->findRegion(Name("combo.arrow"));
    ASSERT_NE(arrow, nullptr);
    EXPECT_EQ(arrow->drawMode, UiSkinDrawMode::Sprite);
    EXPECT_EQ(arrow->sliceInsets.left, 0u);
    EXPECT_FLOAT_EQ(arrow->padding.left, 0.0f);
    EXPECT_FLOAT_EQ(arrow->minimumWidth, 0.0f);
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsUiSkin, VersionlessMetadataCooksAndRoundTripsPaletteAndTypography){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, TypographyMetadata(), entry));
    UiSkin skin(testArena.arena);
    ASSERT_TRUE(BuildUiSkinAsset(entry, skin));
    EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 21.5f);

    UiSkinAssetCodec codec;
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(codec.serialize(skin, binary));
    EXPECT_EQ(binary.size(), sizeof(UiSkinBinaryPayload::HeaderBinary)
        + 2u * sizeof(UiSkinBinaryPayload::RegionBinary)
        + UiSkinColorRole::Count * sizeof(UiSkinBinaryPayload::ColorBinary) + sizeof(UiSkinBinaryPayload::TypographyBinary));
    UiSkinBinaryPayload::HeaderBinary header;
    usize cursor = 0u;
    ASSERT_TRUE(ReadPOD(binary, cursor, header));
    EXPECT_EQ(header.version, UiSkinBinaryPayload::s_UiSkinVersion);
    UiSkin loaded(testArena.arena, skin.virtualPath());
    ASSERT_TRUE(loaded.loadBinary(binary));
    EXPECT_FLOAT_EQ(loaded.typography().defaultFontSize, 21.5f);
    ASSERT_EQ(loaded.regions().size(), 2u);
    for(usize index = 0u; index < UiSkinColorRole::Count; ++index){
        const UiSkinColor& color = loaded.palette().colors[index];
        EXPECT_FLOAT_EQ(color.r, index == UiSkinColorRole::ControlHoverTint ? 1.08f : 0.25f);
        EXPECT_FLOAT_EQ(color.g, index == UiSkinColorRole::ControlHoverTint ? 1.08f : 0.5f);
        EXPECT_FLOAT_EQ(color.b, index == UiSkinColorRole::ControlHoverTint ? 1.08f : 0.75f);
        EXPECT_FLOAT_EQ(color.a, 1.0f);
    }
    UiSkin::RegionVector replacementRegions(loaded.regions().begin(), loaded.regions().end(), testArena.arena);
    loaded.setAtlas(loaded.texture(), loaded.atlasWidth(), loaded.atlasHeight(), loaded.referenceDensity(), Move(replacementRegions));
    ASSERT_TRUE(codec.serialize(loaded, binary));
    cursor = 0u;
    ASSERT_TRUE(ReadPOD(binary, cursor, header));
    EXPECT_EQ(header.version, UiSkinBinaryPayload::s_UiSkinVersion);
    EXPECT_FLOAT_EQ(loaded.palette().colors[UiSkinColorRole::TextNormal].r, UiSkinPalette{}.colors[UiSkinColorRole::TextNormal].r);
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsUiSkin, TypographyRejectsMalformedFooterWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinBinaryPayload::ColorBinary color{ 0.25f, 0.5f, 0.75f, 1.0f };
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeTypographyBinary(testArena, color, 21.5f)));

    for(u32 variant = 0u; variant < 5u; ++variant){
        const f32 fontSize = variant == 2u ? 0.0f : variant == 3u ? 2049.0f
            : variant == 4u ? Limit<f32>::s_QuietNaN : 21.5f;
        Core::Assets::AssetBytes binary = MakeTypographyBinary(testArena, color, fontSize);
        if(variant == 0u)
            binary.pop_back();
        if(variant == 1u)
            binary.push_back(0u);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 21.5f);
    }
}

TEST(AssetsUiSkin, TypographyMetadataRequiresValidFontSize){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, TypographyMetadata(), entry));
    const AStringView malformed[]{ "", "{}", "{\"default_font_size\": 0.0}",
        "{\"default_font_size\": 2049.0}", "{\"default_font_size\": \"large\"}",
        "{\"default_font_size\": 16.0, \"body\": 18.0}" };
    for(u32 index = 0u; index < LengthOf(malformed); ++index){
        EXPECT_FALSE(ParseMetadata(testArena, TypographyMetadata(malformed[index]), entry)) << index;
        EXPECT_FLOAT_EQ(entry.typography.defaultFontSize, 21.5f);
    }
}

TEST(AssetsUiSkin, PaletteRejectsMalformedFooterWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinBinaryPayload::ColorBinary color;
    color.r = 0.25f;
    color.g = 0.5f;
    color.b = 0.75f;
    color.a = 1.0f;
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeTypographyBinary(testArena, color, 16.0f)));

    for(u32 variant = 0u; variant < 6u; ++variant){
        UiSkinBinaryPayload::ColorBinary candidateColor = color;
        if(variant == 2u)
            candidateColor.r = Limit<f32>::s_QuietNaN;
        if(variant == 3u)
            candidateColor.g = -0.1f;
        if(variant == 4u)
            candidateColor.b = 16.1f;
        if(variant == 5u)
            candidateColor.a = 1.1f;
        Core::Assets::AssetBytes binary = MakeTypographyBinary(testArena, candidateColor, 16.0f);
        if(variant == 0u)
            binary.resize(binary.size() - sizeof(UiSkinBinaryPayload::TypographyBinary) - 1u);
        if(variant == 1u)
            binary.push_back(0u);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        ASSERT_EQ(skin.regions().size(), 1u);
        EXPECT_EQ(skin.regions().front().name, Name("panel.normal"));
        EXPECT_FLOAT_EQ(skin.palette().colors[UiSkinColorRole::TextNormal].r, 0.25f);
    }
}

TEST(AssetsUiSkin, PaletteMetadataRequiresEveryKnownRoleAndValidRgba){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, PaletteMetadata(), entry));
    for(u32 variant = 1u; variant <= 6u; ++variant){
        EXPECT_FALSE(ParseMetadata(testArena, PaletteMetadata(variant), entry)) << variant;
        EXPECT_EQ(entry.virtualPath, Name("project/ui/atlas"));
        EXPECT_FLOAT_EQ(entry.palette.colors[UiSkinColorRole::TextNormal].r, 0.25f);
    }
}

TEST(AssetsUiSkin, RuntimeAndCookRegistrarsProvideTypedAsset){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    Core::Assets::CookEntryRegistry cookRegistry(testArena.arena);
    ASSERT_TRUE(Core::Assets::RegisterAutoCollectedCookEntryTypes(cookRegistry));
    EXPECT_TRUE(cookRegistry.has(UiSkin::AssetTypeName()));
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, PaletteMetadata(), entry));
    UiSkin skin(testArena.arena);
    ASSERT_TRUE(BuildUiSkinAsset(entry, skin));
    Core::Assets::AssetBytes binary(testArena.arena);
    UiSkinAssetCodec codec;
    ASSERT_TRUE(codec.serialize(skin, binary));
    Core::Assets::AssetRegistry runtimeRegistry(testArena.arena);
    Core::Assets::RegisterAutoCollectedAssetCodecs(runtimeRegistry);
    UniquePtr<Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(runtimeRegistry.deserializeAsset(UiSkin::AssetTypeName(), skin.virtualPath(), binary, loadedAsset));
    EXPECT_NE(Core::Assets::CastAsset<UiSkin>(loadedAsset.get()), nullptr);
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsUiSkin, EngineDefaultAtlasAndTextureCookAndLoadTogether){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const Path assetRoot = Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets";
    const Path skinPath = assetRoot / "ui" / "skins" / "default" / "atlas.nwb";
    const Path texturePath = assetRoot / "ui" / "skins" / "default" / "texture.nwb";
    Core::Assets::AssetString skinMetadata(testArena.arena);
    Core::Assets::AssetString textureMetadata(testArena.arena);
    ASSERT_TRUE(ReadTextFile(skinPath, skinMetadata));
    ASSERT_TRUE(ReadTextFile(texturePath, textureMetadata));
    Core::Metascript::Document skinDocument(testArena.arena);
    Core::Metascript::Document textureDocument(testArena.arena);
    ASSERT_TRUE(skinDocument.parse(skinMetadata));
    ASSERT_TRUE(textureDocument.parse(textureMetadata));
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    UiSkinCookEntry skinEntry(testArena.arena);
    TextureCookEntry textureEntry(testArena.arena);
    ASSERT_TRUE(ParseUiSkinCookMetadata(assetRoot, "engine", skinPath, skinDocument, skinEntry, scratchArena));
    ASSERT_TRUE(ParseTextureCookMetadata(assetRoot, "engine", texturePath, textureDocument, textureEntry, scratchArena));
    UiSkin skin(testArena.arena);
    Texture texture(testArena.arena);
    ASSERT_TRUE(BuildUiSkinAsset(skinEntry, skin));
    ASSERT_TRUE(BuildTextureAsset(textureEntry, texture));
    ASSERT_TRUE(skin.validateTexture(texture));

    UiSkinAssetCodec skinCodec;
    TextureAssetCodec textureCodec;
    Core::Assets::AssetBytes skinBinary(testArena.arena);
    Core::Assets::AssetBytes textureBinary(testArena.arena);
    ASSERT_TRUE(skinCodec.serialize(skin, skinBinary));
    ASSERT_TRUE(textureCodec.serialize(texture, textureBinary));
    UiSkin loadedSkin(testArena.arena, skin.virtualPath());
    Texture loadedTexture(testArena.arena, texture.virtualPath());
    ASSERT_TRUE(loadedSkin.loadBinary(skinBinary));
    ASSERT_TRUE(loadedTexture.loadBinary(textureBinary));
    ASSERT_TRUE(loadedSkin.validateTexture(loadedTexture));
    EXPECT_FLOAT_EQ(loadedSkin.typography().defaultFontSize, 16.0f);
    EXPECT_EQ(loadedSkin.virtualPath(), Name("engine/ui/skins/default/atlas"));
    EXPECT_EQ(loadedSkin.texture().name(), Name("engine/ui/skins/default/texture"));
    EXPECT_EQ(loadedSkin.atlasWidth(), 256u);
    EXPECT_EQ(loadedSkin.atlasHeight(), 256u);
    EXPECT_FLOAT_EQ(loadedSkin.referenceDensity(), 1.0f);
    ASSERT_EQ(loadedSkin.regions().size(), skin.regions().size());
    const UiSkinRegion* panel = loadedSkin.findRegion(Name("panel.normal"));
    ASSERT_NE(panel, nullptr);
    EXPECT_EQ(panel->rectangle.x, 4u);
    EXPECT_EQ(panel->rectangle.y, 4u);
    EXPECT_EQ(panel->rectangle.width, 24u);
    EXPECT_EQ(panel->rectangle.height, 24u);
    EXPECT_EQ(panel->sliceInsets.left, 6u);
    EXPECT_EQ(panel->sliceInsets.top, 6u);
    EXPECT_EQ(panel->sliceInsets.right, 6u);
    EXPECT_EQ(panel->sliceInsets.bottom, 6u);
    EXPECT_FLOAT_EQ(panel->padding.left, 8.0f);
    const UiSkinRegion* arrow = loadedSkin.findRegion(Name("combo.arrow"));
    ASSERT_NE(arrow, nullptr);
    EXPECT_EQ(arrow->drawMode, UiSkinDrawMode::Sprite);
    EXPECT_EQ(arrow->rectangle.x, 36u);
    EXPECT_EQ(arrow->rectangle.y, 132u);
    EXPECT_EQ(loadedTexture.colorSpace(), TextureColorSpace::Srgb);
    EXPECT_TRUE(loadedTexture.hasAlpha());
    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsUiSkin, RejectsObsoleteBinaryVersionsWithoutReplacingCurrentSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(testArena, ValidHeader(), ValidRegion())));
    for(const u32 version : { 1u, 2u }){
        UiSkinBinaryPayload::HeaderBinary header = ValidHeader();
        header.version = version;
        Core::Assets::AssetBytes binary = MakeBinary(testArena, header, ValidRegion());
        EXPECT_FALSE(skin.loadBinary(binary)) << version;
        binary.resize(sizeof(header) + sizeof(UiSkinBinaryPayload::RegionBinary)
            + (version == 2u ? UiSkinColorRole::Count * sizeof(UiSkinBinaryPayload::ColorBinary) : 0u));
        EXPECT_FALSE(skin.loadBinary(binary)) << version;
        EXPECT_EQ(skin.regions().size(), 1u);
        EXPECT_FLOAT_EQ(skin.typography().defaultFontSize, 16.0f);
    }
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("recook required")));
}

TEST(AssetsUiSkin, LoadRejectsMalformedHeaderAndCountWithoutReplacingSkin){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinBinaryPayload::HeaderBinary validHeader = ValidHeader();
    const UiSkinBinaryPayload::RegionBinary validRegion = ValidRegion();
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(testArena, validHeader, validRegion)));

    for(u32 variant = 0u; variant < 10u; ++variant){
        UiSkinBinaryPayload::HeaderBinary header = validHeader;
        switch(variant){
        case 0u: header.magic = 0u; break;
        case 1u: header.version = 4u; break;
        case 2u: header.reserved0 = 1u; break;
        case 3u: header.reserved1 = 1u; break;
        case 4u: header.regionCount = 0u; break;
        case 5u: header.regionCount = Limit<u32>::s_Max; break;
        case 6u: header.textureNameHash = {}; break;
        case 7u: header.atlasWidth = 0u; break;
        case 8u: header.referenceDensity = 0.0f; break;
        case 9u: header.referenceDensity = Limit<f32>::s_QuietNaN; break;
        }
        const Core::Assets::AssetBytes binary = MakeBinary(testArena, header, validRegion);
        EXPECT_FALSE(skin.loadBinary(binary)) << variant;
        ASSERT_EQ(skin.regions().size(), 1u);
        EXPECT_EQ(skin.atlasWidth(), validHeader.atlasWidth);
    }

    Core::Assets::AssetBytes binary = MakeBinary(testArena, validHeader, validRegion);
    binary.pop_back();
    EXPECT_FALSE(skin.loadBinary(binary));
    binary.resize(sizeof(UiSkinBinaryPayload::HeaderBinary) - 1u);
    EXPECT_FALSE(skin.loadBinary(binary));
    binary = MakeBinary(testArena, validHeader, validRegion);
    binary.push_back(0u);
    EXPECT_FALSE(skin.loadBinary(binary));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("trailing bytes")));
}

TEST(AssetsUiSkin, LoadRejectsInvalidRegionBoundsMetricsAndFlags){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    const UiSkinBinaryPayload::HeaderBinary header = ValidHeader();
    for(u32 variant = 0u; variant < 14u; ++variant){
        UiSkinBinaryPayload::RegionBinary region = ValidRegion();
        switch(variant){
        case 0u: region.nameHash = {}; break;
        case 1u: region.nameHash = Name("").hash(); break;
        case 2u: region.width = 0u; break;
        case 3u: region.x = Limit<u32>::s_Max; break;
        case 4u: region.y = 32u; break;
        case 5u: region.width = Limit<u32>::s_Max; break;
        case 6u: region.sliceLeft = 25u; break;
        case 7u: region.sliceTop = Limit<u32>::s_Max; break;
        case 8u: region.drawMode = 256u; break;
        case 9u: region.reserved = 1u; break;
        case 10u: region.paddingLeft = -1.0f; break;
        case 11u: region.minimumWidth = Limit<f32>::s_QuietNaN; break;
        case 12u: region.minimumHeight = Limit<f32>::s_Infinity; break;
        case 13u: region.drawMode = UiSkinDrawMode::Sprite; break;
        }
        UiSkin skin(testArena.arena, Name("project/ui/atlas"));
        EXPECT_FALSE(skin.loadBinary(MakeBinary(testArena, header, region))) << variant;
        EXPECT_TRUE(skin.regions().empty());
    }
}

TEST(AssetsUiSkin, RejectsOverLimitCountsBeforeReadingOrCopyingRegions){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(testArena, ValidHeader(), ValidRegion())));

    UiSkinBinaryPayload::HeaderBinary header = ValidHeader();
    header.regionCount = s_UiSkinMaxRegionCount + 1u;
    Core::Assets::AssetBytes binary(testArena.arena);
    binary.reserve(sizeof(header));
    AppendPOD(binary, header);
    EXPECT_FALSE(skin.loadBinary(binary));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("UiSkin::loadBinary failed: region count 4097 exceeds schema limit 4096")));
    ASSERT_EQ(skin.regions().size(), 1u);
    EXPECT_EQ(skin.regions().front().name, Name("panel.normal"));

    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse(PaletteMetadata()));
    Core::Metascript::Value& regions = document.asset().field("regions");
    const Core::Metascript::Value regionTemplate(regions.asList().front());
    regions.asList().clear();
    regions.asList().reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        regions.asList().push_back(regionTemplate);
    UiSkinCookEntry entry(testArena.arena);
    const Path assetRoot(testArena.arena, "C:/ui_skin_tests/assets");
    const Path metadataPath = assetRoot / "ui" / "atlas.nwb";
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    EXPECT_FALSE(ParseUiSkinCookMetadata(assetRoot, "project", metadataPath, document, entry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("regions exceed schema limit 4096")));
    EXPECT_TRUE(entry.regions.empty());
    EXPECT_EQ(entry.virtualPath, NAME_NONE);

    UiSkin::RegionVector oversized(testArena.arena);
    oversized.reserve(s_UiSkinMaxRegionCount + 1u);
    for(u32 index = 0u; index <= s_UiSkinMaxRegionCount; ++index)
        oversized.push_back(skin.regions().front());
    UiSkin constructed(testArena.arena, skin.virtualPath());
    constructed.setAtlas(skin.texture(), skin.atlasWidth(), skin.atlasHeight(), skin.referenceDensity(), Move(oversized));
    EXPECT_FALSE(constructed.validatePayload());
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("UiSkin::validatePayload failed: region count 4097 exceeds schema limit 4096")));
}

TEST(AssetsUiSkin, CookRejectsObsoleteMetadataUnknownFieldsAndMalformedArrays){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    static constexpr AStringView s_Overrides[] = {
        "asset.schema_version = 1;\r\n",
        "asset.schema_version = 2;\r\n",
        "asset.schema_version = 3;\r\n",
        "asset.schema_version = 4;\r\n",
        "asset.version = 3;\r\n",
        "asset.revision = 1;\r\n",
        "asset.toolkit_contract = \"widgets_v1\";\r\n",
        "asset.unknown_field = 1;\r\n",
        "asset.texture = \"\";\r\n",
        "asset.atlas_extent = [64];\r\n",
        "asset.atlas_extent = [64.0, 32];\r\n",
        "asset.atlas_extent = [4294967296, 32];\r\n",
        "asset.atlas_extent = [0, 32];\r\n",
        "asset.reference_density = -1.0;\r\n",
        "asset.regions = [];\r\n",
        "asset.regions = [1];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"unknown\": 1 }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"tile\" }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"nine_slice\" }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"padding\": [0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"minimum_size\": [\"bad\", 1] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append(overrideText);
        UiSkinCookEntry entry(testArena.arena);
        EXPECT_FALSE(ParseMetadata(testArena, metadata, entry)) << overrideText;
        EXPECT_TRUE(entry.regions.empty());
        EXPECT_EQ(entry.virtualPath, NAME_NONE);
    }
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported asset field")));
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("toolkit_contract must be 'widgets'")));
}

TEST(AssetsUiSkin, SpriteSlicesAreDerivedAndRetiredFieldsPreservePriorCookEntry){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkinCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, PaletteMetadata(), entry));
    const Name originalPath = entry.virtualPath;
    const Name originalTexture = entry.texture.name();
    const usize originalRegionCount = entry.regions.size();
    static constexpr AStringView s_Overrides[] = {
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"slice\": [0, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\", \"slice\": [0, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"replacement\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\", \"slice\": [1, 0, 0, 0] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append("asset.texture = \"project/other_texture\";\r\n");
        metadata.append(overrideText);
        EXPECT_FALSE(ParseMetadata(testArena, metadata, entry));
        EXPECT_EQ(entry.virtualPath, originalPath);
        EXPECT_EQ(entry.texture.name(), originalTexture);
        ASSERT_EQ(entry.regions.size(), originalRegionCount);
        EXPECT_EQ(entry.regions.front().name, Name("panel.normal"));
        EXPECT_EQ(entry.regions.front().drawMode, UiSkinDrawMode::NineSlice);
        EXPECT_EQ(entry.regions.front().sliceInsets.left, 3u);
        EXPECT_EQ(entry.regions.front().sliceInsets.top, 4u);
        EXPECT_EQ(entry.regions.front().sliceInsets.right, 5u);
        EXPECT_EQ(entry.regions.front().sliceInsets.bottom, 6u);
    }
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported asset field 'slice'")));
    TestAString spriteMetadata(PaletteMetadata());
    spriteMetadata.append("asset.regions = [{ \"name\": \"sprite\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"sprite\" }];\r\n");
    UiSkinCookEntry spriteEntry(testArena.arena);
    ASSERT_TRUE(ParseMetadata(testArena, spriteMetadata, spriteEntry));
    ASSERT_EQ(spriteEntry.regions.size(), 1u);
    const UiSkinSliceInsets& slice = spriteEntry.regions.front().sliceInsets;
    EXPECT_EQ(slice.left, 0u);
    EXPECT_EQ(slice.top, 0u);
    EXPECT_EQ(slice.right, 0u);
    EXPECT_EQ(slice.bottom, 0u);
}

TEST(AssetsUiSkin, CookRejectsDuplicateNamesAndAtlasOrSliceOverflow){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    static constexpr AStringView s_Overrides[] = {
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4] }, { \"name\": \"A\", \"rect\": [4, 0, 4, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [63, 0, 2, 4] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 31, 4, 2] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"draw_mode\": \"nine_slice\", \"slice\": [3, 0, 2, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"padding\": [-1, 0, 0, 0] }];\r\n",
        "asset.regions = [{ \"name\": \"a\", \"rect\": [0, 0, 4, 4], \"minimum_size\": [1, -1] }];\r\n",
    };
    for(const AStringView overrideText : s_Overrides){
        TestAString metadata(PaletteMetadata());
        metadata.append(overrideText);
        UiSkinCookEntry entry(testArena.arena);
        EXPECT_FALSE(ParseMetadata(testArena, metadata, entry)) << overrideText;
    }
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("duplicate region")));
}

TEST(AssetsUiSkin, TextureValidationRejectsIdentityDimensionAndExtentMismatch){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SkinTestArena testArena;
    UiSkin skin(testArena.arena, Name("project/ui/atlas"));
    ASSERT_TRUE(skin.loadBinary(MakeBinary(testArena, ValidHeader(), ValidRegion())));
    for(u32 variant = 0u; variant < 4u; ++variant){
        Texture texture(testArena.arena, variant == 0u ? Name("project/other") : Name("project/ui/texture"));
        Texture::MipLevelVector mips(testArena.arena);
        Core::Assets::AssetBytes payload(testArena.arena);
        texture.setPayload(
            TextureColorSpace::Srgb,
            true,
            variant == 1u ? 63u : 64u,
            variant == 2u ? 31u : 32u,
            Move(mips),
            Move(payload),
            variant == 3u ? TextureDimension::TextureCube : TextureDimension::Texture2D,
            1u,
            TexturePayloadFormat::UastcLdr4x4,
            TextureAlphaMode::EmbeddedLdr,
            TextureFormat::s_OpaqueAlphaUnorm8
        );
        EXPECT_FALSE(skin.validateTexture(texture)) << variant;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

