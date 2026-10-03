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

[[nodiscard]] static bool PrepareFiles(MetadataTestArena& testArena, const Path& directory, const bool includeAtlas = true){
    ErrorCode error;
    if(!EnsureDirectories(directory, error) || error)
        return false;
    static constexpr AStringView s_Filenames[] = { "latin.font", "latin.atlas" };
    for(usize index = 0u; index < (includeAtlas ? 2u : 1u); ++index){
        Core::Assets::AssetBytes bytes(testArena.arena);
        error.clear();
        if(!ReadBinaryFile(FixtureRoot(testArena) / s_Filenames[index], bytes, error) || error)
            return false;
        if(!WriteBinaryFile(directory / s_Filenames[index], bytes))
            return false;
    }
    return true;
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
    EXPECT_EQ(registry.find(Name("font_bundle")), nullptr);
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

TEST(AssetsFontMetadata, StandaloneFontNeedsNoAtlasAndAtlasUsesExplicitSourceReference){
    using namespace __hidden_font_metadata_tests;
    MetadataTestArena testArena;
    const Path fontDirectory = ScratchRoot(testArena) / "standalone_font";
    const Path atlasDirectory = ScratchRoot(testArena) / "standalone_atlas";
    ASSERT_TRUE(PrepareFiles(testArena, fontDirectory, false));
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

TEST(AssetsFontMetadata, VisibleMetadataMismatchPreservesPreviouslyParsedEntries){
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
    static constexpr AStringView s_FontFields[] = { "schema_version", "face_index", "units_per_em", "glyph_count" };
    for(const AStringView field : s_FontFields){
        Core::Metascript::Value wrong(face);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrong, fontEntry));
    }
    Core::Metascript::Value wrongHash(face);
    wrongHash.field("source_sha256").setString("invalid");
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrongHash, fontEntry));
    static constexpr AStringView s_AtlasFields[] = {
        "schema_version", "face_index", "units_per_em", "glyph_count", "bake_ppem", "spread_pixels", "guard_texels"
    };
    for(const AStringView field : s_AtlasFields){
        Core::Metascript::Value wrong(atlas);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry, scratchArena));
    }
    Core::Metascript::Value wrongGroup(atlas);
    wrongGroup.field("groups").asList()[0u].field("channels").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongGroup, atlasEntry, scratchArena));
    Core::Metascript::Value wrongMetric(atlas);
    wrongMetric.field("descender_units").setDouble(1.0);
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
    Core::Metascript::Value unsupported(atlas);
    unsupported.field("source").setString("arbitrary.atlas");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unsupported, atlasEntry, scratchArena));
    EXPECT_EQ(fontEntry.virtualPath, Name("project/fonts/latin/face"));
    EXPECT_EQ(fontEntry.fontBytes.size(), originalFontBytes);
    EXPECT_EQ(atlasEntry.virtualPath, Name("project/fonts/latin/atlas"));
    EXPECT_EQ(atlasEntry.payload.fontSha256, originalHash);
}

TEST(AssetsFontMetadata, MissingTruncatedAndMismatchedPairedFilesPreservePriorAtlas){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "source_rejection";
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
    const Sha256Digest originalHash = entry.payload.fontSha256;
    const Path atlasPath = directory / "latin.atlas";
    Core::Assets::AssetBytes originalAtlas(testArena.arena);
    ErrorCode error;
    ASSERT_TRUE(ReadBinaryFile(atlasPath, originalAtlas, error));
    Core::Assets::AssetBytes truncated(originalAtlas);
    truncated.pop_back();
    ASSERT_TRUE(WriteBinaryFile(atlasPath, truncated));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    FontAtlasPayload wrongMarker(testArena.arena);
    ASSERT_TRUE(DeserializeFontAtlasPayload(originalAtlas, wrongMarker));
    wrongMarker.font.virtualPath = Name("other");
    Core::Assets::AssetBytes mismatched(testArena.arena);
    ASSERT_TRUE(SerializeFontAtlasPayload(wrongMarker, mismatched));
    ASSERT_TRUE(WriteBinaryFile(atlasPath, mismatched));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    ASSERT_TRUE(WriteBinaryFile(atlasPath, originalAtlas));
    Core::Assets::AssetBytes originalFont(testArena.arena);
    Core::Assets::AssetBytes otherFont(testArena.arena);
    error.clear();
    ASSERT_TRUE(ReadBinaryFile(directory / "latin.font", originalFont, error));
    error.clear();
    ASSERT_TRUE(ReadBinaryFile(FixtureRoot(testArena) / "korean.font", otherFont, error));
    ASSERT_TRUE(WriteBinaryFile(directory / "latin.font", otherFont));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry, scratchArena));
    ASSERT_TRUE(WriteBinaryFile(directory / "latin.font", originalFont));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(
        Name("project/other"), directory / "missing.nwb", metadata, entry, scratchArena
    ));
    EXPECT_EQ(entry.virtualPath, Name("project/fonts/latin/atlas"));
    EXPECT_EQ(entry.payload.fontSha256, originalHash);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

