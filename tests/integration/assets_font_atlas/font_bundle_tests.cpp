// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/bundle_cook.h>
#include <impl/assets_font_atlas/binary_payload.h>

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


namespace __hidden_font_bundle_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct FontBundleTestArenaTag{};
using BundleTestArena = TestArena<FontBundleTestArenaTag>;
static constexpr Name s_ScratchArena("tests/integration/assets_font_atlas/bundle_scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Path BundleFixtureRoot(BundleTestArena& testArena){
    return Path(testArena.arena, NWB_REPO_ROOT) / "impl" / "assets" / "ui" / "fonts" / "default";
}

[[nodiscard]] static Path BundleScratchRoot(BundleTestArena& testArena){
    return Path(testArena.arena, NWB_REPO_ROOT) / "__artifacts" / "font_bundle_tests";
}

[[nodiscard]] static bool PrepareBundleFiles(BundleTestArena& testArena, const Path& directory){
    ErrorCode error;
    if(!EnsureDirectories(directory, error) || error)
        return false;
    const Path fixtureRoot = BundleFixtureRoot(testArena);
    static constexpr const char* s_Filenames[] = { "latin.font", "latin.atlas" };
    for(const char* filename : s_Filenames){
        const Path source = fixtureRoot / filename;
        const Path destination = directory / filename;
        Core::Assets::AssetBytes bytes(testArena.arena);
        error.clear();
        if(!ReadBinaryFile(source, bytes, error) || error || !WriteBinaryFile(destination, bytes))
            return false;
    }
    return true;
}

[[nodiscard]] static bool ParseBundle(
    BundleTestArena& testArena,
    const Path& directory,
    const AStringView metadata,
    FontBundleCookEntry& entry){
    Core::Metascript::Document document(testArena.arena);
    if(!document.parse(metadata))
        return false;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    return ParseFontBundleCookMetadata(directory, "project/fonts", directory / "latin.nwb", document, entry, scratchArena);
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
        NotNull<const tchar*>,
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


TEST(AssetsFontBundle, OneDocumentPublishesPairedFontAndAtlasWithDerivedIdentities){
    using namespace __hidden_font_bundle_tests;
    BundleTestArena testArena;
    const Path directory = BundleScratchRoot(testArena) / "valid";
    ASSERT_TRUE(PrepareBundleFiles(testArena, directory));
    Core::Common::InitializerGuard initializers;
    ASSERT_TRUE(initializers.initialize());
    Core::Assets::CookEntryRegistry registry(testArena.arena);
    ASSERT_TRUE(Core::Assets::RegisterAutoCollectedCookEntryTypes(registry));
    ASSERT_NE(registry.find(Name("font_bundle")), nullptr);
    EXPECT_EQ(registry.find(Font::AssetTypeName()), nullptr);
    EXPECT_EQ(registry.find(FontAtlas::AssetTypeName()), nullptr);
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse("font_bundle asset; asset.schema_version = 1;"));
    Core::CpuTaskScheduler scheduler(1u);
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    Core::Assets::CookEntryPathHashSet parsedPaths(0, Hasher<NameHash>(), EqualTo<NameHash>(), testArena.arena);
    Core::Assets::CookEntryParseContext parseContext{ testArena.arena, scheduler, scratchArena, parsedPaths };
    ASSERT_TRUE(registry.parseDocument(Name("font_bundle"), directory, "project/fonts", directory / "latin.nwb", document, parseContext));
    EXPECT_EQ(registry.entryCount(), 2u);
    EXPECT_EQ(parsedPaths.size(), 2u);

    CaptureWriter writer(testArena.arena);
    Core::Assets::CookEntryPathHashSet cookedPaths(0, Hasher<NameHash>(), EqualTo<NameHash>(), testArena.arena);
    Core::Assets::CookEntryWriteContext writeContext{ writer, cookedPaths };
    for(usize index = 0u; index < registry.bucketCount(); ++index)
        ASSERT_TRUE(registry.writeBucket(index, writeContext));
    EXPECT_EQ(writer.writes, 2u);
    EXPECT_EQ(cookedPaths.size(), 2u);
    EXPECT_EQ(writer.fontPath, Name("project/fonts/latin"));
    EXPECT_EQ(writer.atlasPath, Name("project/fonts/latin_atlas"));
    Font font(testArena.arena, writer.fontPath);
    FontAtlas atlas(testArena.arena, writer.atlasPath);
    ASSERT_TRUE(font.loadBinary(writer.fontBinary));
    ASSERT_TRUE(atlas.loadBinary(writer.atlasBinary));
    EXPECT_EQ(atlas.payload().font.name(), font.virtualPath());
    EXPECT_TRUE(ValidateFontAtlasSourceMatch(atlas.payload(), font));
}

TEST(AssetsFontBundle, MissingCorruptOrMismatchedCompanionPreservesPriorEntry){
    using namespace __hidden_font_bundle_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    BundleTestArena testArena;
    const Path directory = BundleScratchRoot(testArena) / "rejection";
    ASSERT_TRUE(PrepareBundleFiles(testArena, directory));
    FontBundleCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 1;", entry));
    const Name originalFont = entry.fontVirtualPath;
    const Name originalAtlas = entry.atlasVirtualPath;
    const Sha256Digest originalHash = entry.atlasPayload.fontSha256;
    const usize originalFontBytes = entry.fontBytes.size();
    const Path atlasPath = directory / "latin.atlas";
    Core::Assets::AssetBytes originalAtlasBytes(testArena.arena);
    ErrorCode error;
    ASSERT_TRUE(ReadBinaryFile(atlasPath, originalAtlasBytes, error));

    EXPECT_FALSE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 1; asset.font = \"other\";", entry));
    EXPECT_FALSE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 2;", entry));
    Core::Assets::AssetBytes truncated(originalAtlasBytes);
    truncated.pop_back();
    ASSERT_TRUE(WriteBinaryFile(atlasPath, truncated));
    EXPECT_FALSE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 1;", entry));
    FontAtlasPayload wrongMarker(testArena.arena);
    ASSERT_TRUE(DeserializeFontAtlasPayload(originalAtlasBytes, wrongMarker));
    wrongMarker.font.virtualPath = Name("other");
    Core::Assets::AssetBytes mismatched(testArena.arena);
    ASSERT_TRUE(SerializeFontAtlasPayload(wrongMarker, mismatched));
    ASSERT_TRUE(WriteBinaryFile(atlasPath, mismatched));
    EXPECT_FALSE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 1;", entry));
    ASSERT_TRUE(WriteBinaryFile(atlasPath, originalAtlasBytes));
    const Path fontPath = directory / "latin.font";
    Core::Assets::AssetBytes originalFontBinary(testArena.arena);
    Core::Assets::AssetBytes otherFontBinary(testArena.arena);
    error.clear();
    ASSERT_TRUE(ReadBinaryFile(fontPath, originalFontBinary, error));
    error.clear();
    ASSERT_TRUE(ReadBinaryFile(BundleFixtureRoot(testArena) / "korean.font", otherFontBinary, error));
    ASSERT_TRUE(WriteBinaryFile(fontPath, otherFontBinary));
    EXPECT_FALSE(ParseBundle(testArena, directory, "font_bundle asset; asset.schema_version = 1;", entry));
    ASSERT_TRUE(WriteBinaryFile(fontPath, originalFontBinary));
    EXPECT_EQ(entry.fontVirtualPath, originalFont);
    EXPECT_EQ(entry.atlasVirtualPath, originalAtlas);
    EXPECT_EQ(entry.atlasPayload.fontSha256, originalHash);
    EXPECT_EQ(entry.fontBytes.size(), originalFontBytes);
    EXPECT_FALSE(ParseBundle(testArena, directory / "missing", "font_bundle asset; asset.schema_version = 1;", entry));
    EXPECT_EQ(entry.fontVirtualPath, originalFont);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

