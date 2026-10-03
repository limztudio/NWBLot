// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font/cook.h>
#include <impl/assets_font_atlas/cook.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_entry_registry.h>
#include <core/common/module.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_metadata_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;
struct FontMetadataTestArenaTag{};
using MetadataTestArena = TestArena<FontMetadataTestArenaTag>;
static constexpr Name s_ScratchArena("tests/integration/assets_font_atlas/metadata_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Path FixtureRoot(MetadataTestArena& testArena){
    return Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default";
}

[[nodiscard]] static Path ScratchRoot(MetadataTestArena& testArena){
    return Path(testArena.arena, NWB_REPO_ROOT) / "__artifacts" / "font_metadata_tests";
}

[[nodiscard]] static bool PrepareFiles(MetadataTestArena& testArena, const Path& directory){
    ErrorCode error;
    if(!EnsureDirectories(directory, error) || error)
        return false;
    Core::Assets::AssetBytes bytes(testArena.arena);
    error.clear();
    if(!ReadBinaryFile(FixtureRoot(testArena) / "latin.font", bytes, error) || error)
        return false;
    return WriteBinaryFile(directory / "latin.font", bytes);
}

[[nodiscard]] static bool LoadMetadata(MetadataTestArena& testArena, Core::Metascript::Document& document){
    AString<Core::Assets::AssetArena> text(testArena.arena);
    return ReadTextFile(FixtureRoot(testArena) / "latin.nwb", text) && document.parse(AStringView(text));
}

struct CaptureWriter final : Core::Assets::ICookedAssetWriter{
    Core::Assets::AssetBytes fontBinary;
    Core::Assets::AssetBytes atlasBinary;
    Name fontPath = NAME_NONE;
    Name atlasPath = NAME_NONE;
    u32 writes = 0u;

    explicit CaptureWriter(Core::Assets::AssetArena& arena)
        : fontBinary(arena)
        , atlasBinary(arena)
    {}

    virtual bool writeCookedAsset(
        TStringView,
        const Name& virtualPath,
        const Core::Assets::IAsset& asset,
        const Core::Assets::IAssetCodec& codec
    )override{
        if(codec.assetType() == Font::AssetTypeName()){
            fontPath = virtualPath;
            ++writes;
            return codec.serialize(asset, fontBinary);
        }
        if(codec.assetType() == FontAtlas::AssetTypeName()){
            atlasPath = virtualPath;
            ++writes;
            return codec.serialize(asset, atlasBinary);
        }
        return false;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFontMetadata, BunchPublishesTypedLocalReferenceAndRejectsDuplicateChild){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "bunch";
    ASSERT_TRUE(PrepareFiles(testArena, directory));
    Core::Common::InitializerGuard initializers;
    ASSERT_TRUE(initializers.initialize());
    Core::Assets::CookEntryRegistry registry(testArena.arena);
    ASSERT_TRUE(Core::Assets::RegisterAutoCollectedCookEntryTypes(registry));
    ASSERT_NE(registry.find(Font::AssetTypeName()), nullptr);
    ASSERT_NE(registry.find(FontAtlas::AssetTypeName()), nullptr);
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(LoadMetadata(testArena, document));
    Core::CpuTaskScheduler scheduler(1u);
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    Core::Assets::ExpandedAssetMetadataVector expanded(scratchArena);
    const Path filePath = directory / "latin.nwb";
    ASSERT_TRUE(Core::Assets::AssetsBunchCook::ExpandAssetBunch(directory, "project/fonts", filePath, document, expanded, scratchArena));
    ASSERT_EQ(expanded.size(), 2u);
    Core::Assets::CookEntryPathHashSet parsedPaths(0, Hasher<NameHash>(), EqualTo<NameHash>(), testArena.arena);
    Core::Assets::CookEntryParseContext parseContext{ testArena.arena, scheduler, scratchArena, parsedPaths };
    const Core::Assets::ExpandedAssetMetadata* atlasChild = nullptr;
    for(const Core::Assets::ExpandedAssetMetadata& child : expanded){
        if(child.assetType == FontAtlas::AssetTypeName())
            atlasChild = &child;
    }
    ASSERT_NE(atlasChild, nullptr);
    Core::Metascript::Value invalidAtlas(atlasChild->value);
    invalidAtlas.field("glyphs").asList().pop_back();
    EXPECT_FALSE(registry.parseValue(atlasChild->assetType, atlasChild->virtualPath, filePath, invalidAtlas, parseContext));
    EXPECT_EQ(registry.entryCount(), 0u);
    EXPECT_TRUE(parsedPaths.empty());
    for(const Core::Assets::ExpandedAssetMetadata& child : expanded)
        ASSERT_TRUE(registry.parseValue(child.assetType, child.virtualPath, filePath, child.value, parseContext));
    EXPECT_EQ(registry.entryCount(), 2u);
    EXPECT_FALSE(registry.parseValue(expanded[0u].assetType, expanded[0u].virtualPath, filePath, expanded[0u].value, parseContext));
    EXPECT_EQ(registry.entryCount(), 2u);

    CaptureWriter writer(testArena.arena);
    Core::Assets::CookEntryPathHashSet cookedPaths(0, Hasher<NameHash>(), EqualTo<NameHash>(), testArena.arena);
    Core::Assets::CookEntryWriteContext writeContext{ writer, cookedPaths };
    for(usize index = 0u; index < registry.bucketCount(); ++index)
        ASSERT_TRUE(registry.writeBucket(index, writeContext));
    EXPECT_EQ(writer.writes, 2u);
    EXPECT_EQ(writer.fontPath, Name("project/fonts/latin/face"));
    EXPECT_EQ(writer.atlasPath, Name("project/fonts/latin/atlas"));
    Font font(testArena.arena, writer.fontPath);
    FontAtlas atlas(testArena.arena, writer.atlasPath);
    ASSERT_TRUE(font.loadBinary(writer.fontBinary));
    ASSERT_TRUE(atlas.loadBinary(writer.atlasBinary));
    EXPECT_EQ(atlas.payload().font.name(), font.virtualPath());
    EXPECT_TRUE(ValidateFontAtlasSourceMatch(atlas.payload(), font));
}

TEST(AssetsFontMetadata, StandaloneImportUsesOnlyTheSameStemFontSourceAndExplicitReference){
    using namespace __hidden_font_metadata_tests;
    MetadataTestArena testArena;
    const Path fontDirectory = ScratchRoot(testArena) / "standalone_font";
    const Path atlasDirectory = ScratchRoot(testArena) / "standalone_atlas";
    ASSERT_TRUE(PrepareFiles(testArena, fontDirectory));
    ASSERT_TRUE(PrepareFiles(testArena, atlasDirectory));
    Core::Metascript::Document fixture(testArena.arena);
    ASSERT_TRUE(LoadMetadata(testArena, fixture));
    const Core::Metascript::Value* faceValue = fixture.findVariable("face");
    const Core::Metascript::Value* atlasValue = fixture.findVariable("atlas");
    ASSERT_NE(faceValue, nullptr);
    ASSERT_NE(atlasValue, nullptr);
    Core::Metascript::Document fontDocument(testArena.arena);
    ASSERT_TRUE(fontDocument.parse("font asset;"));
    fontDocument.asset() = *faceValue;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    FontCookEntry fontEntry(testArena.arena);
    ASSERT_TRUE(ParseFontCookMetadata(fontDirectory, "project/fonts", fontDirectory / "latin.nwb", fontDocument, fontEntry, scratchArena));
    EXPECT_EQ(fontEntry.virtualPath, Name("project/fonts/latin"));
    Core::Metascript::Document atlasDocument(testArena.arena);
    ASSERT_TRUE(atlasDocument.parse("font_atlas asset;"));
    atlasDocument.asset() = *atlasValue;
    atlasDocument.asset().field("font").setString("project/fonts/latin");
    FontAtlasCookEntry atlasEntry(testArena.arena);
    ASSERT_TRUE(ParseFontAtlasCookMetadata(
        atlasDirectory, "project/atlases", atlasDirectory / "latin.nwb", atlasDocument, atlasEntry, scratchArena
    ));
    EXPECT_EQ(atlasEntry.virtualPath, Name("project/atlases/latin"));
    EXPECT_EQ(atlasEntry.payload.font.name(), fontEntry.virtualPath);
}

TEST(AssetsFontMetadata, UnsupportedAndCorruptVisibleMetadataPreservesPreviouslyParsedEntries){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "metadata_rejection";
    ASSERT_TRUE(PrepareFiles(testArena, directory));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(LoadMetadata(testArena, document));
    ASSERT_NE(document.findVariable("face"), nullptr);
    ASSERT_NE(document.findVariable("atlas"), nullptr);
    Core::Metascript::Value face(*document.findVariable("face"));
    Core::Metascript::Value atlas(*document.findVariable("atlas"));
    atlas.field("font").setString("project/fonts/latin/face");
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    FontCookEntry fontEntry(testArena.arena);
    FontAtlasCookEntry atlasEntry(testArena.arena);
    const Path path = directory / "latin.nwb";
    ASSERT_TRUE(ParseFontCookMetadataValue(Name("project/fonts/latin/face"), path, face, fontEntry));
    ASSERT_TRUE(ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, atlas, atlasEntry, scratchArena));
    const Sha256Digest originalHash = atlasEntry.payload.fontSha256;
    const usize originalFontBytes = fontEntry.fontBytes.size();
    const usize originalGlyphCount = atlasEntry.payload.glyphs.size();
    static constexpr AStringView s_FontFields[] = { "face_index", "units_per_em", "glyph_count" };
    for(const AStringView field : s_FontFields){
        Core::Metascript::Value wrong(face);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrong, fontEntry));
    }
    static constexpr AStringView s_UnsupportedFields[] = { "schema_version", "source_sha256", "source", "atlas" };
    for(const AStringView field : s_UnsupportedFields){
        EXPECT_EQ(face.findField(field), nullptr);
        EXPECT_EQ(atlas.findField(field), nullptr);
        Core::Metascript::Value wrongFace(face);
        wrongFace.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrongFace, fontEntry));
        Core::Metascript::Value wrongAtlas(atlas);
        wrongAtlas.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongAtlas, atlasEntry, scratchArena));
    }
    static constexpr AStringView s_AtlasFields[] = {
        "face_index", "units_per_em", "glyph_count", "bake_ppem", "spread_pixels", "guard_texels"
    };
    for(const AStringView field : s_AtlasFields){
        Core::Metascript::Value wrong(atlas);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry, scratchArena));
    }
    ASSERT_FALSE(atlas.field("groups").asList().empty());
    EXPECT_EQ(atlas.field("groups").asList()[0u].findField("sha256"), nullptr);
    Core::Metascript::Value unsupportedGroup(atlas);
    unsupportedGroup.field("groups").asList()[0u].field("sha256").setString("obsolete");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unsupportedGroup, atlasEntry, scratchArena));
    static constexpr AStringView s_GroupFields[] = { "width", "height", "channels" };
    for(const AStringView field : s_GroupFields){
        Core::Metascript::Value wrong(atlas);
        wrong.field("groups").asList()[0u].field(field).setInteger(0);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry, scratchArena));
    }
    Core::Metascript::Value wrongMetric(atlas);
    wrongMetric.field("descender_units").setDouble(Limit<f64>::s_QuietNaN);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongMetric, atlasEntry, scratchArena));
    Core::Metascript::Value wrongRaster(atlas);
    wrongRaster.field("raster_mode").setString("automatic");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongRaster, atlasEntry, scratchArena));
    Core::Metascript::Value emptyReference(atlas);
    emptyReference.field("font").setString("");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, emptyReference, atlasEntry, scratchArena));
    Core::Metascript::Value unresolvedReference(atlas);
    unresolvedReference.field("font").setReference("face");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unresolvedReference, atlasEntry, scratchArena));
    EXPECT_EQ(fontEntry.virtualPath, Name("project/fonts/latin/face"));
    EXPECT_EQ(fontEntry.fontBytes.size(), originalFontBytes);
    EXPECT_EQ(ComputeSha256({ fontEntry.fontBytes.data(), fontEntry.fontBytes.size() }), originalHash);
    EXPECT_EQ(atlasEntry.virtualPath, Name("project/fonts/latin/atlas"));
    EXPECT_EQ(atlasEntry.payload.fontSha256, originalHash);
    EXPECT_EQ(atlasEntry.payload.glyphs.size(), originalGlyphCount);
}

TEST(AssetsFontMetadata, IncompleteOutOfOrderAndInvalidGlyphMappingsPreservePriorAtlas){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "glyph_rejection";
    ASSERT_TRUE(PrepareFiles(testArena, directory));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(LoadMetadata(testArena, document));
    ASSERT_NE(document.findVariable("atlas"), nullptr);
    Core::Metascript::Value metadata(*document.findVariable("atlas"));
    metadata.field("font").setString("project/fonts/latin/face");
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    FontAtlasCookEntry entry(testArena.arena);
    const Path path = directory / "latin.nwb";
    ASSERT_TRUE(ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, metadata, entry, scratchArena));
    const usize originalGlyphCount = entry.payload.glyphs.size();
    const Sha256Digest originalHash = entry.payload.fontSha256;
    ASSERT_GT(originalGlyphCount, 1u);
    Core::Metascript::Value incomplete(metadata);
    incomplete.field("glyphs").asList().pop_back();
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, incomplete, entry, scratchArena));
    Core::Metascript::Value outOfOrder(metadata);
    outOfOrder.field("glyphs").asList()[0u].field("id").setInteger(1);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, outOfOrder, entry, scratchArena));
    static constexpr AStringView s_IntegerFields[] = { "group", "channel", "x", "y", "width", "height", "drawable" };
    for(const AStringView field : s_IntegerFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setInteger(s_FontAtlasMaxExtent + 1u);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry, scratchArena));
    }
    static constexpr AStringView s_FloatFields[] = { "plane_left", "plane_top", "plane_right", "plane_bottom", "advance_units" };
    for(const AStringView field : s_FloatFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setDouble(Limit<f64>::s_Infinity);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry, scratchArena));
    }
    Core::Metascript::Value nonBoolean(metadata);
    nonBoolean.field("glyphs").asList()[0u].field("drawable").setDouble(0.5);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, nonBoolean, entry, scratchArena));
    Core::Metascript::Value unknownGlyphField(metadata);
    unknownGlyphField.field("glyphs").asList()[0u].field("sha256").setString("obsolete");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unknownGlyphField, entry, scratchArena));
    usize drawableIndex = originalGlyphCount;
    for(usize index = 0u; index < originalGlyphCount; ++index){
        if(entry.payload.glyphs[index].drawable != 0u){
            drawableIndex = index;
            break;
        }
    }
    ASSERT_LT(drawableIndex, originalGlyphCount);
    Core::Metascript::Value wrongChannel(metadata);
    const FontAtlasGlyph& drawable = entry.payload.glyphs[drawableIndex];
    wrongChannel.field("glyphs").asList()[drawableIndex].field("channel").setInteger(entry.payload.groups[drawable.group].channelCount);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongChannel, entry, scratchArena));
    Core::Metascript::Value croppedGlyph(metadata);
    croppedGlyph.field("glyphs").asList()[drawableIndex].field("width").setInteger(drawable.width + 1u);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, croppedGlyph, entry, scratchArena));
    EXPECT_EQ(entry.virtualPath, Name("project/fonts/latin/atlas"));
    EXPECT_EQ(entry.payload.fontSha256, originalHash);
    EXPECT_EQ(entry.payload.glyphs.size(), originalGlyphCount);
}

TEST(AssetsFontMetadata, MissingTruncatedCorruptAndMismatchedFontSourcesPreservePriorEntries){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "source_rejection";
    ASSERT_TRUE(PrepareFiles(testArena, directory));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(LoadMetadata(testArena, document));
    ASSERT_NE(document.findVariable("face"), nullptr);
    ASSERT_NE(document.findVariable("atlas"), nullptr);
    Core::Metascript::Value face(*document.findVariable("face"));
    Core::Metascript::Value metadata(*document.findVariable("atlas"));
    metadata.field("font").setString("project/fonts/latin/face");
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    FontCookEntry fontEntry(testArena.arena);
    FontAtlasCookEntry entry(testArena.arena);
    const Path path = directory / "latin.nwb";
    ASSERT_TRUE(ParseFontCookMetadataValue(Name("project/fonts/latin/face"), path, face, fontEntry));
    ASSERT_TRUE(ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, metadata, entry, scratchArena));
    const Sha256Digest originalHash = entry.payload.fontSha256;
    const usize originalFontBytes = fontEntry.fontBytes.size();
    const Path fontPath = directory / "latin.font";
    Core::Assets::AssetBytes originalFont(testArena.arena);
    ErrorCode error;
    ASSERT_TRUE(ReadBinaryFile(fontPath, originalFont, error));
    ASSERT_FALSE(error);
    ASSERT_FALSE(originalFont.empty());
    Core::Assets::AssetBytes truncated(originalFont);
    truncated.pop_back();
    ASSERT_TRUE(WriteBinaryFile(fontPath, truncated));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, face, fontEntry));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    Core::Assets::AssetBytes corrupted(originalFont);
    corrupted.back() ^= 1u;
    ASSERT_TRUE(WriteBinaryFile(fontPath, corrupted));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    corrupted = originalFont;
    ASSERT_GT(corrupted.size(), 24u);
    corrupted[24u] ^= 1u; // The SFNT digest follows the six u32 prepared-source header fields.
    ASSERT_TRUE(WriteBinaryFile(fontPath, corrupted));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, face, fontEntry));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    Core::Assets::AssetBytes otherFont(testArena.arena);
    error.clear();
    ASSERT_TRUE(ReadBinaryFile(FixtureRoot(testArena) / "korean.font", otherFont, error));
    ASSERT_FALSE(error);
    ASSERT_TRUE(WriteBinaryFile(fontPath, otherFont));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, face, fontEntry));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    ASSERT_TRUE(WriteBinaryFile(fontPath, originalFont));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), directory / "missing.nwb", face, fontEntry));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(
        Name("project/other"), directory / "missing.nwb", metadata, entry, scratchArena
    ));
    EXPECT_EQ(fontEntry.virtualPath, Name("project/fonts/latin/face"));
    EXPECT_EQ(fontEntry.fontBytes.size(), originalFontBytes);
    EXPECT_EQ(ComputeSha256({ fontEntry.fontBytes.data(), fontEntry.fontBytes.size() }), originalHash);
    EXPECT_EQ(entry.virtualPath, Name("project/fonts/latin/atlas"));
    EXPECT_EQ(entry.payload.fontSha256, originalHash);
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

