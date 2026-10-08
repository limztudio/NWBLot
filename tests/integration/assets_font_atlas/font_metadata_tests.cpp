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
    if(!EnsureDirectories(directory))
        return false;
    Core::Assets::AssetBytes bytes(testArena.arena);
    if(!ReadBinaryFile(FixtureRoot(testArena) / "latin.font", bytes))
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
    {
        auto expandedResult = Core::Assets::AssetsBunchCook::ExpandAssetBunch(directory, "project/fonts", filePath, document, scratchArena);
        ASSERT_TRUE(expandedResult);
        expanded = Move(*expandedResult);
    }
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
    auto entryParseResult = ParseFontCookMetadata(directory, "project/fonts", path, document, entry.arena, scratchArena);
    ASSERT_TRUE(entryParseResult);
    entry.fontBytes = Move(entryParseResult->fontBytes);
    entry.virtualPath = Move(entryParseResult->virtualPath);
    entry.faceIndex = Move(entryParseResult->faceIndex);
    const Sha256Digest originalHash = ComputeSha256({ entry.fontBytes.data(), entry.fontBytes.size() });
    const usize originalFontBytes = entry.fontBytes.size();
    document.asset().makeMap();
    ASSERT_TRUE(document.asset().asMap().empty());
    auto entryParseResult2 = ParseFontCookMetadata(directory, "project/fonts", path, document, entry.arena, scratchArena);
    ASSERT_TRUE(entryParseResult2);
    entry.fontBytes = Move(entryParseResult2->fontBytes);
    entry.virtualPath = Move(entryParseResult2->virtualPath);
    entry.faceIndex = Move(entryParseResult2->faceIndex);
    for(u32 caseIndex = 0u; caseIndex < 5u; ++caseIndex){
        Core::Metascript::Value invalid(testArena.arena);
        switch(caseIndex){
        case 0u: invalid.setInteger(0); break;
        case 1u: invalid.setDouble(0.0); break;
        case 2u: invalid.setString("font"); break;
        case 3u: invalid.setReference("asset"); break;
        case 4u: invalid.makeList(); break;
        }
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, invalid, entry.arena));
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
    auto fontEntryParseResult = ParseFontCookMetadataValue(Name("project/fonts/latin/face"), path, face, fontEntry.arena);
    ASSERT_TRUE(fontEntryParseResult);
    fontEntry.fontBytes = Move(fontEntryParseResult->fontBytes);
    fontEntry.virtualPath = Move(fontEntryParseResult->virtualPath);
    fontEntry.faceIndex = Move(fontEntryParseResult->faceIndex);
    auto atlasEntryParseResult = ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, atlas, atlasEntry.arena, scratchArena);
    ASSERT_TRUE(atlasEntryParseResult);
    atlasEntry.payload = Move(atlasEntryParseResult->payload);
    atlasEntry.virtualPath = Move(atlasEntryParseResult->virtualPath);
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
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrong, fontEntry.arena));
    }
    static constexpr AStringView s_UnsupportedFields[] = { "schema_version", "source_sha256", "source", "atlas" };
    for(const AStringView field : s_UnsupportedFields){
        EXPECT_EQ(atlas.findField(field), nullptr);
        Core::Metascript::Value wrongFace(face);
        wrongFace.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, wrongFace, fontEntry.arena));
        Core::Metascript::Value wrongAtlas(atlas);
        wrongAtlas.field(field).setString("obsolete");
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongAtlas, atlasEntry.arena, scratchArena));
    }
    static constexpr AStringView s_RetiredAtlasFields[] = { "face_index", "units_per_em", "glyph_count", "guard_texels" };
    for(const AStringView field : s_RetiredAtlasFields){
        EXPECT_EQ(atlas.findField(field), nullptr);
        Core::Metascript::Value wrong(atlas);
        const u32 value = field == "face_index" ? atlasEntry.payload.faceIndex
            : field == "units_per_em" ? atlasEntry.payload.unitsPerEm
            : field == "glyph_count" ? atlasEntry.payload.sourceGlyphCount : atlasEntry.payload.guardTexels;
        wrong.field(field).setInteger(value);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry.arena, scratchArena));
    }
    static constexpr AStringView s_AtlasSettings[] = { "bake_ppem", "spread_pixels" };
    for(const AStringView field : s_AtlasSettings){
        Core::Metascript::Value wrong(atlas);
        wrong.field(field).setInteger(-1);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, atlasEntry.arena, scratchArena));
    }
    EXPECT_EQ(atlas.findField("groups"), nullptr);
    Core::Metascript::Value obsoleteGroups(atlas);
    obsoleteGroups.field("groups").setString("obsolete");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsoleteGroups, atlasEntry.arena, scratchArena));
    Core::Metascript::Value wrongMetric(atlas);
    wrongMetric.field("descender_units").setDouble(Limit<f64>::s_QuietNaN);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongMetric, atlasEntry.arena, scratchArena));
    Core::Metascript::Value wrongRaster(atlas);
    wrongRaster.field("raster_mode").setString("automatic");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongRaster, atlasEntry.arena, scratchArena));
    Core::Metascript::Value emptyReference(atlas);
    emptyReference.field("font").setString("");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, emptyReference, atlasEntry.arena, scratchArena));
    Core::Metascript::Value unresolvedReference(atlas);
    unresolvedReference.field("font").setReference("face");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unresolvedReference, atlasEntry.arena, scratchArena));
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
    auto entryParseResult3 = ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, metadata, entry.arena, scratchArena);
    ASSERT_TRUE(entryParseResult3);
    entry.payload = Move(entryParseResult3->payload);
    entry.virtualPath = Move(entryParseResult3->virtualPath);
    const usize originalGlyphCount = entry.payload.glyphs.size();
    const Sha256Digest originalHash = entry.payload.fontSha256;
    ASSERT_GT(originalGlyphCount, 1u);
    Core::Metascript::Value incomplete(metadata);
    incomplete.field("glyphs").asList().pop_back();
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, incomplete, entry.arena, scratchArena));
    Core::Metascript::Value obsoleteId(metadata);
    EXPECT_EQ(obsoleteId.field("glyphs").asList()[0u].findField("id"), nullptr);
    obsoleteId.field("glyphs").asList()[0u].field("id").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsoleteId, entry.arena, scratchArena));
    static constexpr AStringView s_IntegerFields[] = { "group", "channel", "x", "y", "width", "height" };
    for(const AStringView field : s_IntegerFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setInteger(s_FontAtlasMaxExtent + 1u);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry.arena, scratchArena));
    }
    static constexpr AStringView s_FloatFields[] = { "plane_left", "plane_top", "advance_units" };
    for(const AStringView field : s_FloatFields){
        Core::Metascript::Value wrong(metadata);
        wrong.field("glyphs").asList()[0u].field(field).setDouble(Limit<f64>::s_Infinity);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrong, entry.arena, scratchArena));
    }
    static constexpr AStringView s_DerivedGlyphFields[] = { "drawable", "plane_right", "plane_bottom" };
    for(const AStringView field : s_DerivedGlyphFields){
        Core::Metascript::Value obsolete(metadata);
        obsolete.field("glyphs").asList()[0u].field(field).setInteger(0);
        EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, obsolete, entry.arena, scratchArena));
    }
    Core::Metascript::Value unknownGlyphField(metadata);
    unknownGlyphField.field("glyphs").asList()[0u].field("sha256").setString("obsolete");
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, unknownGlyphField, entry.arena, scratchArena));
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
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, wrongChannel, entry.arena, scratchArena));
    Core::Metascript::Value croppedGlyph(metadata);
    croppedGlyph.field("glyphs").asList()[drawableIndex].field("width").setInteger(entry.payload.groups[drawable.group].width);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, croppedGlyph, entry.arena, scratchArena));
    Core::Metascript::Value zeroDimension(metadata);
    zeroDimension.field("glyphs").asList()[drawableIndex].field("height").setInteger(0);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, zeroDimension, entry.arena, scratchArena));
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
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, emptyWithGeometry, entry.arena, scratchArena));
    Core::Metascript::Value missingWidth(metadata);
    ASSERT_EQ(missingWidth.field("glyphs").asList()[drawableIndex].asMap().erase(AStringView("width")), 1u);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, missingWidth, entry.arena, scratchArena));
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
    auto fontEntryParseResult2 = ParseFontCookMetadataValue(Name("project/fonts/latin/face"), path, face, fontEntry.arena);
    ASSERT_TRUE(fontEntryParseResult2);
    fontEntry.fontBytes = Move(fontEntryParseResult2->fontBytes);
    fontEntry.virtualPath = Move(fontEntryParseResult2->virtualPath);
    fontEntry.faceIndex = Move(fontEntryParseResult2->faceIndex);
    auto entryParseResult4 = ParseFontAtlasCookMetadataValue(Name("project/fonts/latin/atlas"), path, metadata, entry.arena, scratchArena);
    ASSERT_TRUE(entryParseResult4);
    entry.payload = Move(entryParseResult4->payload);
    entry.virtualPath = Move(entryParseResult4->virtualPath);
    const Sha256Digest originalHash = entry.payload.fontSha256;
    const usize originalFontBytes = fontEntry.fontBytes.size();
    const Path fontPath = directory / "latin.font";
    Core::Assets::AssetBytes originalFont(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(fontPath, originalFont));
    ASSERT_FALSE(originalFont.empty());
    Core::Assets::AssetBytes truncated(originalFont);
    truncated.pop_back();
    ASSERT_TRUE(WriteBinaryFile(fontPath, truncated));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, face, fontEntry.arena));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry.arena, scratchArena));
    Core::Assets::AssetBytes corrupted(originalFont);
    corrupted.back() ^= 1u;
    ASSERT_TRUE(WriteBinaryFile(fontPath, corrupted));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry.arena, scratchArena));
    corrupted = originalFont;
    ASSERT_GT(corrupted.size(), 24u);
    corrupted[24u] ^= 1u; // The SFNT digest follows the six u32 prepared-source header fields.
    ASSERT_TRUE(WriteBinaryFile(fontPath, corrupted));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), path, face, fontEntry.arena));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry.arena, scratchArena));
    Core::Assets::AssetBytes otherFont(testArena.arena);
    ASSERT_TRUE(ReadBinaryFile(FixtureRoot(testArena) / "korean.font", otherFont));
    ASSERT_TRUE(WriteBinaryFile(fontPath, otherFont));
    FontCookEntry replacementFont(testArena.arena);
    auto replacementFontParseResult = ParseFontCookMetadataValue(Name("project/other"), path, face, replacementFont.arena);
    ASSERT_TRUE(replacementFontParseResult);
    replacementFont.fontBytes = Move(replacementFontParseResult->fontBytes);
    replacementFont.virtualPath = Move(replacementFontParseResult->virtualPath);
    replacementFont.faceIndex = Move(replacementFontParseResult->faceIndex);
    EXPECT_NE(ComputeSha256({ replacementFont.fontBytes.data(), replacementFont.fontBytes.size() }), originalHash);
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), path, metadata, entry.arena, scratchArena));
    ASSERT_TRUE(WriteBinaryFile(fontPath, originalFont));
    EXPECT_FALSE(ParseFontCookMetadataValue(Name("project/other"), directory / "missing.nwb", face, fontEntry.arena));
    EXPECT_FALSE(ParseFontAtlasCookMetadataValue(Name("project/other"), directory / "missing.nwb", metadata, entry.arena, scratchArena));
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

