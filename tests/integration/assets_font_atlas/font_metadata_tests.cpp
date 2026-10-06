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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFontMetadata, BunchRejectsInvalidAndDuplicateChildrenWithoutPublication){
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
}


TEST(AssetsFontMetadata, FieldlessAndEmptyFontDeclarationsRejectNonmapValuesWithoutReplacingPriorEntry){
    using namespace __hidden_font_metadata_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    MetadataTestArena testArena;
    const Path directory = ScratchRoot(testArena) / "declaration_shape";
    ASSERT_TRUE(PrepareFiles(testArena, directory));
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(document.parse("font asset;"));
    ASSERT_TRUE(document.asset().isNull());
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    FontCookEntry entry(testArena.arena);
    const Path path = directory / "latin.nwb";
    ASSERT_TRUE(ParseFontCookMetadata(directory, "project/fonts", path, document, entry, scratchArena));
    const Sha256Digest originalHash = ComputeSha256({ entry.fontBytes.data(), entry.fontBytes.size() });
    const usize originalFontBytes = entry.fontBytes.size();
    document.asset().makeMap();
    ASSERT_TRUE(document.asset().asMap().empty());
    ASSERT_TRUE(ParseFontCookMetadata(directory, "project/fonts", path, document, entry, scratchArena));
    for(u32 caseIndex = 0u; caseIndex < 5u; ++caseIndex){
        Core::Metascript::Value invalid(testArena.arena);
        switch(caseIndex){
        case 0u: invalid.setInteger(0); break;
        case 1u: invalid.setDouble(0.0); break;
        case 2u: invalid.setString("font"); break;
        case 3u: invalid.setReference("asset"); break;
        case 4u: invalid.makeList(); break;
        }
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, invalid, entry));
    }
    EXPECT_EQ(entry.virtualPath, Name("project/fonts/latin"));
    EXPECT_EQ(entry.fontBytes.size(), originalFontBytes);
    EXPECT_EQ(ComputeSha256({ entry.fontBytes.data(), entry.fontBytes.size() }), originalHash);
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
    ASSERT_TRUE(face.isNull());
    static constexpr AStringView s_FontFields[] = { "face_index", "units_per_em", "glyph_count" };
    for(const AStringView field : s_FontFields){
        Core::Metascript::Value wrong(face);
        const u32 value = field == "face_index" ? fontEntry.faceIndex
            : field == "units_per_em" ? atlasEntry.payload.unitsPerEm : atlasEntry.payload.sourceGlyphCount;
        wrong.field(field).setInteger(value);
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrong, fontEntry));
    }
    static constexpr AStringView s_UnsupportedFields[] = { "schema_version", "source_sha256", "source", "atlas" };
    for(const AStringView field : s_UnsupportedFields){
        EXPECT_EQ(atlas.findField(field), nullptr);
        Core::Metascript::Value wrongFace(face);
        wrongFace.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrongFace, fontEntry));
        Core::Metascript::Value wrongAtlas(atlas);
        wrongAtlas.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongAtlas, atlasEntry, scratchArena));
    }
    static constexpr AStringView s_RetiredAtlasFields[] = { "face_index", "units_per_em", "glyph_count", "guard_texels" };
    for(const AStringView field : s_RetiredAtlasFields){
        EXPECT_EQ(atlas.findField(field), nullptr);
        Core::Metascript::Value wrong(atlas);
        const u32 value = field == "face_index" ? atlasEntry.payload.faceIndex
            : field == "units_per_em" ? atlasEntry.payload.unitsPerEm
            : field == "glyph_count" ? atlasEntry.payload.sourceGlyphCount : atlasEntry.payload.guardTexels;
        wrong.field(field).setInteger(value);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry, scratchArena));
    }
    static constexpr AStringView s_AtlasSettings[] = { "bake_ppem", "spread_pixels" };
    for(const AStringView field : s_AtlasSettings){
        Core::Metascript::Value wrong(atlas);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry, scratchArena));
    }
    EXPECT_EQ(atlas.findField("groups"), nullptr);
    Core::Metascript::Value obsoleteGroups(atlas);
    obsoleteGroups.field("groups").setString("obsolete");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsoleteGroups, atlasEntry, scratchArena));
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

TEST(AssetsFontMetadata, IncompleteAndInvalidGlyphMappingsPreservePriorAtlas){
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
    Core::Metascript::Value obsoleteId(metadata);
    EXPECT_EQ(obsoleteId.field("glyphs").asList()[0u].findField("id"), nullptr);
    obsoleteId.field("glyphs").asList()[0u].field("id").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsoleteId, entry, scratchArena));
    static constexpr AStringView s_IntegerFields[] = { "group", "channel", "x", "y", "width", "height" };
    for(const AStringView field : s_IntegerFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setInteger(s_FontAtlasMaxExtent + 1u);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry, scratchArena));
    }
    static constexpr AStringView s_FloatFields[] = { "plane_left", "plane_top", "advance_units" };
    for(const AStringView field : s_FloatFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setDouble(Limit<f64>::s_Infinity);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry, scratchArena));
    }
    static constexpr AStringView s_DerivedGlyphFields[] = { "drawable", "plane_right", "plane_bottom" };
    for(const AStringView field : s_DerivedGlyphFields){
        Core::Metascript::Value obsolete(metadata);
        obsolete.field("glyphs").asList()[0u].field(field).setInteger(0);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsolete, entry, scratchArena));
    }
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
    croppedGlyph.field("glyphs").asList()[drawableIndex].field("width").setInteger(entry.payload.groups[drawable.group].width);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, croppedGlyph, entry, scratchArena));
    Core::Metascript::Value zeroDimension(metadata);
    zeroDimension.field("glyphs").asList()[drawableIndex].field("height").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, zeroDimension, entry, scratchArena));
    usize emptyIndex = originalGlyphCount;
    for(usize index = 0u; index < originalGlyphCount; ++index){
        if(entry.payload.glyphs[index].drawable == 0u){
            emptyIndex = index;
            break;
        }
    }
    ASSERT_LT(emptyIndex, originalGlyphCount);
    Core::Metascript::Value emptyWithGeometry(metadata);
    emptyWithGeometry.field("glyphs").asList()[emptyIndex].field("x").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, emptyWithGeometry, entry, scratchArena));
    Core::Metascript::Value missingWidth(metadata);
    ASSERT_EQ(missingWidth.field("glyphs").asList()[drawableIndex].asMap().erase(AStringView("width")), 1u);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, missingWidth, entry, scratchArena));
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
    FontCookEntry replacementFont(testArena.arena);
    ASSERT_TRUE(ParseFontCookMetadataValue(Name("project/other"), path, face, replacementFont));
    EXPECT_NE(ComputeSha256({ replacementFont.fontBytes.data(), replacementFont.fontBytes.size() }), originalHash);
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

