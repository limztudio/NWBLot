// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_font_atlas/binary_payload.h>
#include <impl/assets_font_atlas/cook_metadata.h>
#include <impl/assets_font_atlas/source_payload.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/base64.h>
#include <global/filesystem.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>
#include <zstd/zstd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_font_atlas_source_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl;

struct FontAtlasSourceTestArenaTag{};
using SourceTestArena = TestArena<FontAtlasSourceTestArenaTag>;

static constexpr Name s_ScratchArena("tests/integration/assets_font_atlas/source");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::Assets::AssetBytes MakeNoise(SourceTestArena& testArena, const usize count){
    Core::Assets::AssetBytes bytes(testArena.arena);
    bytes.resize(count);
    u32 state = 0x148fb782u;
    for(u8& value : bytes){
        state ^= state << 13u;
        state ^= state >> 17u;
        state ^= state << 5u;
        value = static_cast<u8>(state);
    }
    return bytes;
}

[[nodiscard]] static bool AddEmbeddedFields(
    SourceTestArena& testArena,
    Core::Metascript::Value& record,
    const Core::Assets::AssetBytes& bytes){
    Core::Assets::AssetBytes packed(testArena.arena);
    AString<Core::Assets::AssetArena> encoded(testArena.arena);
    if(!FontAtlasSource::EncodePayload(bytes, packed) || !EncodeBase64({ packed.data(), packed.size() }, encoded))
        return false;
    record.field("packed_byte_count").setInteger(static_cast<i64>(packed.size()));
    Core::Metascript::Value& chunks = record.field("data_base64");
    chunks.makeList();
    for(usize begin = 0u; begin < encoded.size(); begin += FontAtlasSource::s_Base64ChunkChars){
        const usize count = Min(FontAtlasSource::s_Base64ChunkChars, encoded.size() - begin);
        chunks.append(Core::Metascript::Value(AStringView(encoded.data() + begin, count), testArena.arena));
    }
    return true;
}

[[nodiscard]] static bool MakeEmbeddedDocument(SourceTestArena& testArena, Core::Metascript::Document& document){
    const Path directory = Path(testArena.arena, NWB_REPO_ROOT) / "tests" / "integration" / "assets_font_atlas" / "fixtures";
    AString<Core::Assets::AssetArena> metadata(testArena.arena);
    if(!ReadTextFile(directory / "atlas.nwb", metadata) || !document.parse(metadata))
        return false;
    Core::Metascript::Value& asset = document.asset();
    asset.field("schema_version").setInteger(FontAtlasSource::s_SchemaVersion);
    asset.field("payload_encoding").setString("zstd_base64");
    static constexpr AStringView s_Sections[] = { "groups", "positioning_tables" };
    for(const AStringView section : s_Sections){
        for(Core::Metascript::Value& record : asset.field(section).asList()){
            Core::Assets::AssetBytes bytes(testArena.arena);
            ErrorCode error;
            const Path source = directory / Path(testArena.arena, record.field("data").asString());
            if(!ReadBinaryFile(source, bytes, error) || error)
                return false;
            Core::Metascript::Value embedded(testArena.arena);
            embedded.makeMap();
            for(const auto& [key, value] : record.asMap()){
                if(key != "data")
                    embedded.field(key) = value;
            }
            if(!AddEmbeddedFields(testArena, embedded, bytes))
                return false;
            record = Move(embedded);
        }
    }
    return true;
}

[[nodiscard]] static bool ParseEmbeddedDocument(
    SourceTestArena& testArena,
    const Core::Metascript::Document& document,
    FontAtlasCookEntry& entry){
    const Path root = Path(testArena.arena, NWB_REPO_ROOT) / "tests" / "integration" / "assets_font_atlas" / "fixtures";
    const Path missingMetadata = root / "detached" / "inline.nwb";
    ErrorCode error;
    if(FileExists(missingMetadata, error) || error)
        return false;
    Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    return ParseFontAtlasCookMetadata(root, "project", missingMetadata, document, entry, scratchArena);
}

[[nodiscard]] static bool MakeFrame(
    const Core::Assets::AssetBytes& raw,
    const int contentSize,
    const int checksum,
    Core::Assets::AssetBytes& packed){
    ZSTD_CCtx* context = ZSTD_createCCtx();
    if(!context)
        return false;
    ScopeExit cleanup([context]()noexcept{
        if(ZSTD_isError(ZSTD_freeCCtx(context)))
            NWB_ASSERT(false);
    });

    if(
        ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_contentSizeFlag, contentSize))
        || ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, checksum))
    )
        return false;
    packed.resize(ZSTD_compressBound(raw.size()));
    const usize size = ZSTD_compress2(context, packed.data(), packed.size(), raw.data(), raw.size());
    if(ZSTD_isError(size))
        return false;
    packed.resize(size);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsFontAtlasSource, DeterministicCompressionPreservesAllBytesAndIndependentSchemaVersions){
    using namespace __hidden_font_atlas_source_tests;
    SourceTestArena testArena;
    const Core::Assets::AssetBytes bytes = MakeNoise(testArena, 16384u);
    Core::Assets::AssetBytes packed(testArena.arena);
    Core::Assets::AssetBytes repeated(testArena.arena);
    Core::Assets::AssetBytes loaded(testArena.arena);
    ASSERT_TRUE(FontAtlasSource::EncodePayload(bytes, packed));
    ASSERT_TRUE(FontAtlasSource::EncodePayload(bytes, repeated));
    EXPECT_EQ(packed, repeated);
    EXPECT_LE(packed.size(), FontAtlasSource::MaximumPackedBytes(static_cast<u32>(bytes.size())));
    ASSERT_TRUE(FontAtlasSource::DecodePayload(packed, static_cast<u32>(bytes.size()), loaded));
    EXPECT_EQ(bytes, loaded);
    EXPECT_EQ(FontAtlasSource::s_SchemaVersion, 2u);
    EXPECT_EQ(FontAtlasBinaryPayload::s_Version, 1u);
    EXPECT_EQ(FontAtlasSource::MaximumPackedBytes(0u), 0u);
    EXPECT_EQ(FontAtlasSource::MaximumPackedBytes(FontAtlasSource::s_MaxPayloadBytes + 1u), 0u);
    Core::Assets::AssetBytes empty(testArena.arena);
    EXPECT_FALSE(FontAtlasSource::EncodePayload(empty, repeated));
    EXPECT_EQ(repeated, packed);
}

TEST(AssetsFontAtlasSource, RejectsCorruptTruncatedTrailingConcatenatedAndSkippableFramesAtomically){
    using namespace __hidden_font_atlas_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    const Core::Assets::AssetBytes bytes = MakeNoise(testArena, 1024u);
    Core::Assets::AssetBytes packed(testArena.arena);
    ASSERT_TRUE(FontAtlasSource::EncodePayload(bytes, packed));
    Core::Assets::AssetBytes loaded(testArena.arena);
    loaded.push_back(42u);
    const Core::Assets::AssetBytes original(loaded);
    for(u32 mutation = 0u; mutation < 5u; ++mutation){
        Core::Assets::AssetBytes invalid(packed);
        switch(mutation){
        case 0u: invalid.back() ^= 1u; break;
        case 1u: invalid.pop_back(); break;
        case 2u: invalid.push_back(0u); break;
        case 3u: invalid.insert(invalid.end(), packed.begin(), packed.end()); break;
        case 4u: invalid.assign({ 0x50u, 0x2au, 0x4du, 0x18u, 0u, 0u, 0u, 0u }); break;
        default: FAIL();
        }
        EXPECT_FALSE(FontAtlasSource::DecodePayload(invalid, static_cast<u32>(bytes.size()), loaded)) << mutation;
        EXPECT_EQ(loaded, original) << mutation;
    }
    EXPECT_FALSE(FontAtlasSource::DecodePayload(packed, static_cast<u32>(bytes.size() + 1u), loaded));
    EXPECT_EQ(loaded, original);
    EXPECT_FALSE(FontAtlasSource::DecodePayload(packed, FontAtlasSource::s_MaxPayloadBytes + 1u, loaded));
    EXPECT_EQ(loaded, original);
}

TEST(AssetsFontAtlasSource, RequiresDeclaredContentSizeAndChecksumEvenForOtherwiseValidFrames){
    using namespace __hidden_font_atlas_source_tests;
    SourceTestArena testArena;
    const Core::Assets::AssetBytes bytes = MakeNoise(testArena, 1024u);
    Core::Assets::AssetBytes packed(testArena.arena);
    Core::Assets::AssetBytes loaded(testArena.arena);
    ASSERT_TRUE(MakeFrame(bytes, 0, 1, packed));
    EXPECT_EQ(ZSTD_getFrameContentSize(packed.data(), packed.size()), ZSTD_CONTENTSIZE_UNKNOWN);
    EXPECT_FALSE(FontAtlasSource::DecodePayload(packed, static_cast<u32>(bytes.size()), loaded));
    ASSERT_TRUE(MakeFrame(bytes, 1, 0, packed));
    EXPECT_EQ(ZSTD_getFrameContentSize(packed.data(), packed.size()), bytes.size());
    EXPECT_FALSE(FontAtlasSource::DecodePayload(packed, static_cast<u32>(bytes.size()), loaded));
    ASSERT_TRUE(MakeFrame(bytes, 1, 1, packed));
    EXPECT_TRUE(FontAtlasSource::DecodePayload(packed, static_cast<u32>(bytes.size()), loaded));
    EXPECT_EQ(bytes, loaded);
}

TEST(AssetsFontAtlasSource, EmbeddedMetadataCooksWithoutAdjacentFilesAndKeepsExactRuntimePayload){
    using namespace __hidden_font_atlas_source_tests;
    SourceTestArena testArena;
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(MakeEmbeddedDocument(testArena, document));
    FontAtlasCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseEmbeddedDocument(testArena, document, entry));
    EXPECT_EQ(entry.virtualPath, Name("project/detached/inline"));
    ASSERT_EQ(entry.payload.groups.size(), 1u);
    ASSERT_EQ(entry.payload.groups[0u].pixels.size(), 256u);
    for(usize index = 0u; index < 256u; ++index)
        EXPECT_EQ(entry.payload.groups[0u].pixels[index], static_cast<u8>(index));
    ASSERT_EQ(entry.payload.positioningTables.size(), 1u);
    EXPECT_EQ(entry.payload.positioningTables[0u].tag, s_FontAtlasKernTag);
    const Path tablePath = Path(testArena.arena, NWB_REPO_ROOT) / "tests" / "integration" / "assets_font_atlas" / "fixtures" / "pairs.kern";
    Core::Assets::AssetBytes expectedTable(testArena.arena);
    ErrorCode error;
    ASSERT_TRUE(ReadBinaryFile(tablePath, expectedTable, error));
    EXPECT_EQ(entry.payload.positioningTables[0u].bytes, expectedTable);
    FontAtlas atlas(testArena.arena);
    ASSERT_TRUE(BuildFontAtlasAsset(entry, atlas));
    Core::Assets::AssetBytes binary(testArena.arena);
    ASSERT_TRUE(SerializeFontAtlasPayload(atlas.payload(), binary));
    EXPECT_EQ(binary[4u], 1u);
    FontAtlasPayload loaded(testArena.arena);
    ASSERT_TRUE(DeserializeFontAtlasPayload(binary, loaded));
    EXPECT_EQ(loaded.groups[0u].pixels, entry.payload.groups[0u].pixels);
    EXPECT_EQ(loaded.positioningTables[0u].bytes, expectedTable);
    EXPECT_EQ(loaded.positioningTables[0u].sha256, entry.payload.positioningTables[0u].sha256);
    EXPECT_EQ(loaded.fontSha256, entry.payload.fontSha256);
    EXPECT_EQ(loaded.glyphs[3u].channel, 3u);
    EXPECT_EQ(loaded.glyphs[4u].drawable, 0u);
}

TEST(AssetsFontAtlasSource, InvalidInlineMetadataNeverReplacesPreviouslyCookedEntry){
    using namespace __hidden_font_atlas_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    Core::Metascript::Document document(testArena.arena);
    ASSERT_TRUE(MakeEmbeddedDocument(testArena, document));
    const Core::Metascript::Value original(document.asset());
    FontAtlasCookEntry entry(testArena.arena);
    ASSERT_TRUE(ParseEmbeddedDocument(testArena, document, entry));
    const Sha256Digest originalHash = entry.payload.groups[0u].sha256;
    const Core::Assets::AssetBytes originalPixels(entry.payload.groups[0u].pixels);
    for(u32 mutation = 0u; mutation < 12u; ++mutation){
        document.asset() = original;
        Core::Metascript::Value& asset = document.asset();
        Core::Metascript::Value& group = asset.field("groups").asList()[0u];
        switch(mutation){
        case 0u: asset.field("payload_encoding").setString("base64"); break;
        case 1u: asset.field("schema_version").setInteger(1); break;
        case 2u: group.field("data").setString("pattern.rgba"); break;
        case 3u: group.field("packed_byte_count").setInteger(0); break;
        case 4u: group.field("packed_byte_count").setInteger(Limit<u32>::s_Max); break;
        case 5u: group.field("byte_count").setInteger(255); break;
        case 6u: group.field("data_base64").makeList(); break;
        case 7u: group.field("data_base64").asList()[0u].setInteger(4); break;
        case 8u: group.field("data_base64").asList()[0u].setString("AB=="); group.field("packed_byte_count").setInteger(1); break;
        case 9u: group.field("sha256").setString("0000000000000000000000000000000000000000000000000000000000000000"); break;
        case 10u: group.field("extent").asList()[0u].setInteger(2049); break;
        case 11u: asset.field("positioning_tables").asList()[0u].field("byte_count").setInteger(s_FontAtlasMaxPositioningBytes + 1u); break;
        default: FAIL();
        }
        EXPECT_FALSE(ParseEmbeddedDocument(testArena, document, entry)) << mutation;
        EXPECT_EQ(entry.virtualPath, Name("project/detached/inline")) << mutation;
        EXPECT_EQ(entry.payload.groups[0u].sha256, originalHash) << mutation;
        EXPECT_EQ(entry.payload.groups[0u].pixels, originalPixels) << mutation;
    }
}

TEST(AssetsFontAtlasSource, ChunkPreflightRequiresCanonicalBoundariesPaddingCountsAndDeclaredSizes){
    using namespace __hidden_font_atlas_source_tests;
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerGuard(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    SourceTestArena testArena;
    const Core::Assets::AssetBytes bytes = MakeNoise(testArena, 16384u);
    Core::Metascript::Value record(testArena.arena);
    record.makeMap();
    ASSERT_TRUE(AddEmbeddedFields(testArena, record, bytes));
    ASSERT_GT(record.field("data_base64").asList().size(), 1u);
    const Core::Metascript::Value original(record);
    const Path path(testArena.arena, "inline.nwb");
    Core::Assets::AssetBytes loaded(testArena.arena);
    ASSERT_TRUE(FontAtlasMetadata::ReadEmbeddedPayload(path, record, static_cast<u32>(bytes.size()), loaded));
    EXPECT_EQ(loaded, bytes);
    for(u32 mutation = 0u; mutation < 5u; ++mutation){
        record = original;
        Core::Metascript::Value::ListType& chunks = record.field("data_base64").asList();
        switch(mutation){
        case 0u: chunks.pop_back(); break;
        case 1u:{
            const AString<Core::Assets::AssetArena> shortened(chunks[0u].asString().substr(0u, FontAtlasSource::s_Base64ChunkChars - 4u), testArena.arena);
            chunks[0u].setString(shortened);
            break;
        }
        case 2u:{
            AString<Core::Assets::AssetArena> encoded(chunks[0u].asString(), testArena.arena);
            encoded.back() = '=';
            chunks[0u].setString(encoded);
            break;
        }
        case 3u: chunks.push_back(Core::Metascript::Value(AStringView("AAAA"), testArena.arena)); break;
        case 4u: record.field("packed_byte_count").setInteger(record.field("packed_byte_count").asInteger() + 4); break;
        default: FAIL();
        }
        EXPECT_FALSE(FontAtlasMetadata::ReadEmbeddedPayload(path, record, static_cast<u32>(bytes.size()), loaded)) << mutation;
        EXPECT_EQ(loaded, bytes) << mutation;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

