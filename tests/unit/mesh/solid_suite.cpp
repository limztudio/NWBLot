// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/unit/mesh/solid_test_types.adhd.h>

#include <impl/assets_mesh/cook.h>
#include <impl/assets_mesh/binary_payload_io.h>
#include <impl/assets_mesh/cook_meshlets.h>
#include <impl/assets_mesh/meshlet_triangle_visit.h>

#include <core/assets/binary_payload_io.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_solid_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Declarations for testing non-noexcept callback traits without calling them.
struct ThrowingPositionStreamGeometry{
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletDesc>& meshlets()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletBounds>& meshletBounds()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPositionRefDeltas()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletAttributeRefDeltas()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<Impl::MeshletLocalVertexRef>& meshletLocalVertexRefs()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const noexcept;
    [[nodiscard]] const Core::Assets::AssetVector<u32>& solidTriangleWords()const noexcept;
};

struct NoThrowCornerVisitor{
    [[nodiscard]] b8 operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const noexcept;
};

struct ThrowingCornerVisitor{
    [[nodiscard]] b8 operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const;
};

using OneMeshlet = Array<Impl::MeshletDesc, 1u>;
using OneLocalVertex = Array<Impl::MeshletLocalVertexRef, 1u>;
using OnePrimitive = Array<u8, 1u>;

static_assert(noexcept(Impl::MeshAssetBinaryPayload::AddMeshBaseReserveBytes(DeclVal<const Impl::Mesh&>(), DeclVal<usize&>())));
static_assert(!noexcept(Impl::MeshAssetBinaryPayload::AddMeshBaseReserveBytes(DeclVal<const ThrowingPositionStreamGeometry&>(), DeclVal<usize&>())));
static_assert(noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const OnePrimitive&>(), DeclVal<NoThrowCornerVisitor&>())));
static_assert(!noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const OnePrimitive&>(), DeclVal<ThrowingCornerVisitor&>())));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshName = NWB::Tests::s_OneOffNameTestsMeshSolidCodec;
inline constexpr AStringView s_MeshSource = R"(mesh asset;
asset.positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]];
asset.normals = [[0, 0, 1]];
asset.tangents = [[1, 0, 0, 1]];
asset.uv0 = [[0, 0]];
asset.colors = [[1, 1, 1, 1]];
asset.vertex_refs = [[0, 0, 0, 0, 0], [1, 0, 0, 0, 0], [2, 0, 0, 0, 0], [3, 0, 0, 0, 0]];
asset.indices = [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]];
)";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<Core::Assets::AssetBytes> CookMeshBinary(Core::Assets::AssetArena& arena){
    Core::Metascript::Document document(arena);
    if(!document.parse(s_MeshSource))
        return MakeUnexpected(Failure{});
    const Core::Metascript::Value* const asset = document.findVariable("asset");
    if(!asset)
        return MakeUnexpected(Failure{});
    Core::CpuTaskScheduler scheduler(1u);
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::s_SolidCaseMeshSolidCodecCook)};
    const Path sourcePath(arena, "tests/mesh/solid_codec.nwb");
    auto entry = Impl::ParseMeshCookMetadata(s_MeshName, sourcePath, *asset, arena, scheduler, scratch);
    if(!entry)
        return MakeUnexpected(Failure{});
    auto mesh = Impl::BuildMeshAsset(*entry, arena, scratch);
    if(!mesh)
        return MakeUnexpected(Failure{});
    Core::Assets::AssetBytes binary(arena);
    const Impl::MeshAssetCodec codec;
    if(!codec.serialize(*mesh, binary))
        return MakeUnexpected(Failure{});
    return binary;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace __hidden_mesh_solid_codec_tests;

TEST(MeshSolidCodec, MissingTruncatedOrExtraMembershipWordsAreRejected){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena{Name(NWB::Tests::s_SolidCaseMeshSolidCodecTruncated)};
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh control(arena, s_MeshName);
    ASSERT_TRUE(control.loadBinary(*cooked));
    ASSERT_EQ(control.solidTriangleWords().size(), 1u);
    ASSERT_EQ(control.solidTriangleWords()[0], 0xfu);

    Impl::Mesh rejected(arena, s_MeshName);
    for(usize removedBytes = 1u; removedBytes <= sizeof(u32); ++removedBytes){
        Core::Assets::AssetBytes malformed(*cooked);
        malformed.resize(malformed.size() - removedBytes);
        EXPECT_FALSE(rejected.loadBinary(malformed)) << "removed membership bytes: " << removedBytes;
    }
    Core::Assets::AssetBytes extraWord(*cooked);
    AppendPOD(extraWord, u32(0u));
    EXPECT_FALSE(rejected.loadBinary(extraWord));
    EXPECT_GT(logger.errorCount(), 0u);
}

TEST(MeshSolidCodec, UnaddressableBoundsCountIsRejectedBeforeStreamReadAndPreservesLoadedMesh){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena{Name(NWB::Tests::s_SolidCaseMeshSolidCodecUnaddressableBounds)};
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh loaded(arena, s_MeshName);
    ASSERT_TRUE(loaded.loadBinary(*cooked));
    const usize retainedMeshletCount = loaded.meshlets().size();
    const usize retainedPositionCount = loaded.positionStream().size();
    ASSERT_EQ(loaded.solidTriangleWords().size(), 1u);
    const u32 retainedSolidWord = loaded.solidTriangleWords()[0];

    Core::Assets::AssetBytes malformed(*cooked);
    constexpr u64 s_UnaddressableBoundsCount = static_cast<u64>(Limit<u32>::s_Max / Impl::s_MeshletBoundsStride) + 1u;
    static_assert((s_UnaddressableBoundsCount - 1u) * Impl::s_MeshletBoundsStride <= Limit<u32>::s_Max);
    static_assert(s_UnaddressableBoundsCount * Impl::s_MeshletBoundsStride > Limit<u32>::s_Max);
    NWB_MEMCPY(malformed.data() + offsetof(Impl::MeshBinaryHeader, meshletCount), sizeof(s_UnaddressableBoundsCount), &s_UnaddressableBoundsCount, sizeof(s_UnaddressableBoundsCount));
    NWB_MEMCPY(malformed.data() + offsetof(Impl::MeshBinaryHeader, meshletBoundCount), sizeof(s_UnaddressableBoundsCount), &s_UnaddressableBoundsCount, sizeof(s_UnaddressableBoundsCount));
    EXPECT_FALSE(loaded.loadBinary(malformed));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("mesh payload is incomplete")));
    EXPECT_EQ(loaded.meshlets().size(), retainedMeshletCount);
    EXPECT_EQ(loaded.positionStream().size(), retainedPositionCount);
    ASSERT_EQ(loaded.solidTriangleWords().size(), 1u);
    EXPECT_EQ(loaded.solidTriangleWords()[0], retainedSolidWord);
}

TEST(MeshSolidCodec, TailPaddingAndInconsistentPrimitiveCountsAreRejected){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena{Name(NWB::Tests::s_SolidCaseMeshSolidCodecPadding)};
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh control(arena, s_MeshName);
    ASSERT_TRUE(control.loadBinary(*cooked));
    ASSERT_EQ(control.solidTriangleWords().size(), 1u);

    Impl::Mesh rejected(arena, s_MeshName);
    for(const u32 invalidWord : { 0x1fu, 0x8000000fu }){
        Core::Assets::AssetBytes malformed(*cooked);
        NWB_MEMCPY(malformed.data() + malformed.size() - sizeof(u32), sizeof(invalidWord), &invalidWord, sizeof(invalidWord));
        EXPECT_FALSE(rejected.loadBinary(malformed));
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid solid triangle padding")));
    Core::Assets::AssetBytes wrongCount(*cooked);
    constexpr u64 s_InconsistentIndexCount = 99u;
    NWB_MEMCPY(wrongCount.data() + offsetof(Impl::MeshBinaryHeader, meshletPrimitiveIndexCount), sizeof(s_InconsistentIndexCount), &s_InconsistentIndexCount, sizeof(s_InconsistentIndexCount));
    EXPECT_FALSE(rejected.loadBinary(wrongCount));
}

TEST(MeshSolidCodec, AdjacentBoundsPreserveFloatBitsAndRejectATruncatedLastCone){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena{Name(NWB::Tests::s_SolidCaseMeshSolidCodecCompactBounds)};
    const Array<u32, 10u> words{ 0x80000000u, 0x00000001u, 0x7f7fffffu, 0x3f800000u, 0xff000001u, 0x00000000u, 0x80000001u, 0xff7fffffu, 0x00800000u, 0x80000001u };
    Core::Assets::AssetVector<Impl::MeshletBounds> bounds(arena);
    bounds.resize(2u);
    for(usize index = 0u; index < bounds.size(); ++index){
        for(usize component = 0u; component < 4u; ++component)
            bounds[index].sphere.raw[component] = BitCast<f32>(words[index * 5u + component]);
        bounds[index].conePacked = words[index * 5u + 4u];
    }
    Core::Assets::AssetBytes binary(arena);
    ASSERT_TRUE(Core::Assets::AppendVectorPayload(bounds, NWB_TEXT("compact bounds"), NWB_TEXT("bounds"), binary));
    ASSERT_EQ(binary.size(), sizeof(u32) * words.size());
    usize wordCursor = 0u;
    for(const u32 word : words){
        const auto stored = ReadPOD<u32>(binary, wordCursor);
        ASSERT_TRUE(stored);
        EXPECT_EQ(*stored, word);
    }
    Core::Assets::AssetVector<Impl::MeshletBounds> decoded(arena);
    usize cursor = 0u;
    ASSERT_TRUE(Core::Assets::ReadVectorPayload(binary, bounds.size(), NWB_TEXT("compact bounds"), NWB_TEXT("bounds"), cursor, decoded));
    ASSERT_EQ(decoded.size(), bounds.size());
    for(usize index = 0u; index < decoded.size(); ++index){
        for(usize component = 0u; component < 4u; ++component)
            EXPECT_EQ(BitCast<u32>(decoded[index].sphere.raw[component]), words[index * 5u + component]);
        EXPECT_EQ(decoded[index].conePacked, words[index * 5u + 4u]);
    }
    for(usize removedBytes = 1u; removedBytes <= sizeof(u32); ++removedBytes){
        Core::Assets::AssetBytes truncated(binary);
        truncated.resize(truncated.size() - removedBytes);
        cursor = 0u;
        EXPECT_FALSE(Core::Assets::ReadVectorPayload(truncated, bounds.size(), NWB_TEXT("compact bounds"), NWB_TEXT("bounds"), cursor, decoded)) << "missing final cone bytes: " << removedBytes;
        EXPECT_EQ(cursor, 0u);
        EXPECT_TRUE(decoded.empty());
    }
    EXPECT_EQ(logger.errorCount(), sizeof(u32));
}

TEST(MeshSolidCodec, PreviousMeshMagicCannotAdmitOtherwiseValidCurrentPayload){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena{Name(NWB::Tests::s_SolidCaseMeshSolidCodecPreviousEncoding)};
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh control(arena, s_MeshName);
    ASSERT_TRUE(control.loadBinary(*cooked));

    Core::Assets::AssetBytes previousEncoding(*cooked);
    constexpr u32 s_PreviousMeshMagic = 0x4d534836u;
    NWB_MEMCPY(previousEncoding.data(), sizeof(s_PreviousMeshMagic), &s_PreviousMeshMagic, sizeof(s_PreviousMeshMagic));
    Impl::Mesh rejected(arena, s_MeshName);
    EXPECT_FALSE(rejected.loadBinary(previousEncoding));
    EXPECT_GT(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/unit/mesh/solid_test_types.adhd.h>

#include <impl/assets_mesh/cook_solid_topology.adhd.h>

#include <global/simd/vector.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MeshSolidTopology, MixedClosedOpenAndInwardComponentsKeepIndependentMembership){
    const Array<Float3U, 11u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{2.f, 0.f, 0.f}, Float3U{3.f, 0.f, 0.f}, Float3U{2.f, 1.f, 0.f}, Float3U{2.f, 0.f, 1.f},
        Float3U{4.f, 0.f, 0.f}, Float3U{5.f, 0.f, 0.f}, Float3U{4.f, 1.f, 0.f},
    };
    const Array<u32, 27u> indices = {
        0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u,
        4u, 5u, 6u, 4u, 7u, 5u, 4u, 6u, 7u, 5u, 7u, 6u,
        8u, 9u, 10u,
    };
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0xffu);
}

TEST(MeshSolidTopology, BrokenWindingNonManifoldAndDegenerateFacesInvalidateTheirWholeComponent){
    const Array<Float3U, 5u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{0.5f, 0.f, 0.f},
    };
    const Array<u32, 12u> brokenWinding = {0u, 1u, 2u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const Array<u32, 15u> nonManifold = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u, 0u, 1u, 4u};
    const Array<u32, 12u> degenerate = {0u, 2u, 1u, 0u, 1u, 4u, 0u, 4u, 2u, 1u, 2u, 4u};
    for(const Span<const u32> indices : {Span<const u32>(brokenWinding), Span<const u32>(nonManifold), Span<const u32>(degenerate)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, CoincidentIndependentPositionsDoNotRepairAnOpenSeam){
    const Array<Float3U, 7u> positions = {
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{0.f, 0.f, 1.f},
        Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f},
    };
    const Array<u32, 12u> indices = {4u, 6u, 5u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 1u);
    EXPECT_EQ((*words)[0], 0u);
}

TEST(MeshSolidTopology, MembershipCrossesWordBoundariesWithoutSettingTailPadding){
    Core::Alloc::ScratchArena scratch{Name(NWB::Tests::s_SolidCaseAssetsMeshSolidMembershipWordBoundary)};
    Vector<Float3U, Core::Alloc::ScratchArena> positions{scratch};
    Vector<u32, Core::Alloc::ScratchArena> indices{scratch};
    constexpr Array<u32, 12u> s_Tetrahedron = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(u32 component = 0u; component < 9u; ++component){
        const f32 x = static_cast<f32>(component) * 2.f;
        positions.push_back({x, 0.f, 0.f});
        positions.push_back({x + 1.f, 0.f, 0.f});
        positions.push_back({x, 1.f, 0.f});
        positions.push_back({x, 0.f, 1.f});
        for(const u32 corner : s_Tetrahedron)
            indices.push_back(component * 4u + corner);
    }
    const auto words = Impl::BuildSolidTriangleWords(positions, indices);
    ASSERT_TRUE(words);
    ASSERT_EQ(words->size(), 2u);
    EXPECT_EQ((*words)[0], 0xffffffffu);
    EXPECT_EQ((*words)[1], 0xfu);
}

TEST(MeshSolidTopology, NonFinitePositionsAndInvalidCornerRangesCannotProduceMembership){
    Array<Float3U, 3u> positions = {Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}};
    const Array<u32, 3u> validCorners = {0u, 1u, 2u};
    const Array<u32, 2u> missingCorner = {0u, 1u};
    const Array<u32, 3u> outOfRange = {0u, 1u, 3u};
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, missingCorner));
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, outOfRange));
    positions[2].x = Limit<f32>::s_QuietNaN;
    EXPECT_FALSE(Impl::BuildSolidTriangleWords(positions, validCorners));
}

TEST(MeshSolidTopology, OpposedSheetsAndCoplanarClosedComponentsRemainSurfaces){
    const Array<Float3U, 4u> positions = {Float3U{0.f, 0.f, 0.f}, Float3U{1.f, 0.f, 0.f}, Float3U{0.f, 1.f, 0.f}, Float3U{1.f, 1.f, 0.f}};
    const Array<u32, 6u> sheet = {0u, 1u, 2u, 0u, 2u, 1u};
    const Array<u32, 12u> flatTetrahedron = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const Span<const u32> indices : {Span<const u32>(sheet), Span<const u32>(flatTetrahedron)}){
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0u);
    }
}

TEST(MeshSolidTopology, RelativeComponentVolumePreservesTinyAndTranslatedSolids){
    const Array<MeshSolidVolumeCase, 3u> cases = {{
        { .origin = Float3U{0.f, 0.f, 0.f}, .size = 0.001f },
        { .origin = Float3U{0.f, 0.f, 0.f}, .size = 1.e-30f },
        { .origin = Float3U{1.e20f, -1.e20f, 1.e20f}, .size = 1.e14f },
    }};
    const Array<u32, 12u> indices = {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u};
    for(const MeshSolidVolumeCase& volumeCase : cases){
        const Float3U& origin = volumeCase.origin;
        const SIMDVectorSingle shifted = VectorAdd(VectorSet(origin.x, origin.y, origin.z, 0.f), VectorReplicate(volumeCase.size));
        const Array<Float3U, 4u> positions = {
            origin,
            Float3U{VectorGetX(shifted), origin.y, origin.z},
            Float3U{origin.x, VectorGetY(shifted), origin.z},
            Float3U{origin.x, origin.y, VectorGetZ(shifted)},
        };
        const auto words = Impl::BuildSolidTriangleWords(positions, indices);
        ASSERT_TRUE(words);
        ASSERT_EQ(words->size(), 1u);
        EXPECT_EQ((*words)[0], 0xfu);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


