// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/cook.h>
#include <impl/assets_mesh/binary_payload.h>
#include <impl/assets_mesh/cook_meshlets.h>
#include <impl/assets_mesh/meshlet_triangle_visit.h>

#include <core/assets/binary_payload_io.h>

#include <tests/common/capturing_logger.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_solid_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// These interfaces deliberately model throwing supplied operations; no runtime implementation is invoked.
struct ThrowingPositionStreamGeometry : Impl::MeshGeometryPayload{
    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const;
};

struct NoThrowCornerVisitor{
    [[nodiscard]] bool operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const noexcept;
};

struct ThrowingCornerVisitor{
    [[nodiscard]] bool operator()(const Impl::MeshletDesc&, usize, const Impl::MeshletLocalVertexRef&)const;
};

struct ThrowingU32Conversion{
    operator u32()const;
};

struct ThrowingPrimitiveConversion{
    [[nodiscard]] usize size()const noexcept;
    [[nodiscard]] ThrowingU32Conversion operator[](const usize&)const noexcept;
};

struct ThrowingPrimitiveSize{
    [[nodiscard]] usize size()const;
    [[nodiscard]] const u8& operator[](const usize&)const noexcept;
};

struct ThrowingIncrementIterator{
    [[nodiscard]] const u32& operator*()const noexcept;
    ThrowingIncrementIterator& operator++();
    [[nodiscard]] bool operator!=(const ThrowingIncrementIterator&)const noexcept;
};

struct ThrowingIncrementRange{
    [[nodiscard]] ThrowingIncrementIterator begin()const noexcept;
    [[nodiscard]] ThrowingIncrementIterator end()const noexcept;
};

struct NoThrowTriangleNormal{
    [[nodiscard]] SIMDVectorSingle operator()(const u32&)const noexcept;
};

struct ThrowingTriangleNormal{
    [[nodiscard]] SIMDVectorSingle operator()(const u32&)const;
};

using OneMeshlet = Array<Impl::MeshletDesc, 1u>;
using OneLocalVertex = Array<Impl::MeshletLocalVertexRef, 1u>;
using OnePrimitive = Array<u8, 1u>;
using OneTriangle = Array<u32, 1u>;

static_assert(noexcept(Impl::MeshAssetBinaryPayload::AddMeshBaseReserveBytes(DeclVal<usize&>(), DeclVal<const Impl::Mesh&>())));
static_assert(!noexcept(Impl::MeshAssetBinaryPayload::AddMeshBaseReserveBytes(DeclVal<usize&>(), DeclVal<const ThrowingPositionStreamGeometry&>())));
static_assert(noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const OnePrimitive&>(), DeclVal<NoThrowCornerVisitor&>())));
static_assert(!noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const OnePrimitive&>(), DeclVal<ThrowingCornerVisitor&>())));
static_assert(!noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const ThrowingPrimitiveSize&>(), DeclVal<NoThrowCornerVisitor&>())));
static_assert(!noexcept(Impl::ForEachMeshletTriangleCorner(DeclVal<const OneMeshlet&>(), DeclVal<const OneLocalVertex&>(), DeclVal<const ThrowingPrimitiveConversion&>(), DeclVal<NoThrowCornerVisitor&>())));
static_assert(noexcept(Impl::MeshletCandidateSearch::ComputeMeshletScoreConeCutoff(DeclVal<const OneTriangle&>(), VectorZero(), 0u, false, DeclVal<const NoThrowTriangleNormal&>())));
static_assert(!noexcept(Impl::MeshletCandidateSearch::ComputeMeshletScoreConeCutoff(DeclVal<const OneTriangle&>(), VectorZero(), 0u, false, DeclVal<const ThrowingTriangleNormal&>())));
static_assert(!noexcept(Impl::MeshletCandidateSearch::ComputeMeshletScoreConeCutoff(DeclVal<const ThrowingIncrementRange&>(), VectorZero(), 0u, false, DeclVal<const NoThrowTriangleNormal&>())));
static_assert(noexcept(Impl::MeshletCandidateSearch::PredictMeshletScoreConeWidening(DeclVal<const OneTriangle&>(), DeclVal<const Impl::MeshletScoreState&>(), 0u, VectorZero(), DeclVal<const NoThrowTriangleNormal&>())));
static_assert(!noexcept(Impl::MeshletCandidateSearch::PredictMeshletScoreConeWidening(DeclVal<const OneTriangle&>(), DeclVal<const Impl::MeshletScoreState&>(), 0u, VectorZero(), DeclVal<const ThrowingTriangleNormal&>())));
static_assert(!noexcept(Impl::MeshletCandidateSearch::PredictMeshletScoreConeWidening(DeclVal<const ThrowingIncrementRange&>(), DeclVal<const Impl::MeshletScoreState&>(), 0u, VectorZero(), DeclVal<const NoThrowTriangleNormal&>())));


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshName("tests/mesh/solid_codec");
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
    Core::Alloc::ScratchArena scratch(Name("tests/mesh/solid_codec/cook"));
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
    Core::Assets::AssetArena arena(Name("tests/mesh/solid_codec/truncated"));
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
    Core::Assets::AssetArena arena(Name("tests/mesh/solid_codec/unaddressable_bounds"));
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh loaded(arena, s_MeshName);
    ASSERT_TRUE(loaded.loadBinary(*cooked));
    const usize retainedMeshletCount = loaded.meshlets().size();
    const usize retainedPositionCount = loaded.positionStream().size();
    ASSERT_EQ(loaded.solidTriangleWords().size(), 1u);
    const u32 retainedSolidWord = loaded.solidTriangleWords()[0];

    Core::Assets::AssetBytes malformed(*cooked);
    constexpr u64 s_UnaddressableBoundsCount = static_cast<u64>(Limit<u32>::s_Max / NWB_MESHLET_BOUNDS_STRIDE) + 1u;
    static_assert((s_UnaddressableBoundsCount - 1u) * NWB_MESHLET_BOUNDS_STRIDE <= Limit<u32>::s_Max);
    static_assert(s_UnaddressableBoundsCount * NWB_MESHLET_BOUNDS_STRIDE > Limit<u32>::s_Max);
    NWB_MEMCPY(malformed.data() + offsetof(Impl::MeshBinaryPayload::MeshHeaderBinary, meshletCount), sizeof(s_UnaddressableBoundsCount), &s_UnaddressableBoundsCount, sizeof(s_UnaddressableBoundsCount));
    NWB_MEMCPY(malformed.data() + offsetof(Impl::MeshBinaryPayload::MeshHeaderBinary, meshletBoundCount), sizeof(s_UnaddressableBoundsCount), &s_UnaddressableBoundsCount, sizeof(s_UnaddressableBoundsCount));
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
    Core::Assets::AssetArena arena(Name("tests/mesh/solid_codec/padding"));
    const auto cooked = CookMeshBinary(arena);
    ASSERT_TRUE(cooked);
    Impl::Mesh control(arena, s_MeshName);
    ASSERT_TRUE(control.loadBinary(*cooked));
    ASSERT_EQ(control.solidTriangleWords().size(), 1u);

    Impl::Mesh rejected(arena, s_MeshName);
    for(const u32 invalidWord : {0x1fu, 0x8000000fu}){
        Core::Assets::AssetBytes malformed(*cooked);
        NWB_MEMCPY(malformed.data() + malformed.size() - sizeof(u32), sizeof(invalidWord), &invalidWord, sizeof(invalidWord));
        EXPECT_FALSE(rejected.loadBinary(malformed));
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("invalid solid triangle padding")));
    Core::Assets::AssetBytes wrongCount(*cooked);
    constexpr u64 s_InconsistentIndexCount = 99u;
    NWB_MEMCPY(wrongCount.data() + offsetof(Impl::MeshBinaryPayload::MeshHeaderBinary, meshletPrimitiveIndexCount), sizeof(s_InconsistentIndexCount), &s_InconsistentIndexCount, sizeof(s_InconsistentIndexCount));
    EXPECT_FALSE(rejected.loadBinary(wrongCount));
}

TEST(MeshSolidCodec, AdjacentBoundsPreserveFloatBitsAndRejectATruncatedLastCone){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena(Name("tests/mesh/solid_codec/compact_bounds"));
    const Array<u32, 10u> words{ 0x80000000u, 0x00000001u, 0x7f7fffffu, 0x3f800000u, 0xff000001u, 0x00000000u, 0x80000001u, 0xff7fffffu, 0x00800000u, 0x80000001u, };
    Core::Assets::AssetVector<Impl::MeshletBounds> bounds(arena);
    bounds.resize(2u);
    for(usize index = 0u; index < bounds.size(); ++index){
        for(usize component = 0u; component < 4u; ++component)
            bounds[index].sphere.raw[component] = BitCast<f32>(words[index * 5u + component]);
        bounds[index].conePacked = words[index * 5u + 4u];
    }
    Core::Assets::AssetBytes binary(arena);
    ASSERT_TRUE(Core::Assets::AppendVectorPayload(binary, bounds, NWB_TEXT("compact bounds"), NWB_TEXT("bounds")));
    ASSERT_EQ(binary.size(), sizeof(u32) * words.size());
    usize wordCursor = 0u;
    for(const u32 word : words){
        const auto stored = ReadPOD<u32>(binary, wordCursor);
        ASSERT_TRUE(stored);
        EXPECT_EQ(*stored, word);
    }
    Core::Assets::AssetVector<Impl::MeshletBounds> decoded(arena);
    usize cursor = 0u;
    ASSERT_TRUE(Core::Assets::ReadVectorPayload(binary, cursor, bounds.size(), decoded, NWB_TEXT("compact bounds"), NWB_TEXT("bounds")));
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
        EXPECT_FALSE(Core::Assets::ReadVectorPayload(truncated, cursor, bounds.size(), decoded, NWB_TEXT("compact bounds"), NWB_TEXT("bounds"))) << "missing final cone bytes: " << removedBytes;
        EXPECT_EQ(cursor, 0u);
        EXPECT_TRUE(decoded.empty());
    }
    EXPECT_EQ(logger.errorCount(), sizeof(u32));
}

TEST(MeshSolidCodec, PreviousMeshMagicCannotAdmitOtherwiseValidCurrentPayload){
    CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::ReportOnly);
    Core::Assets::AssetArena arena(Name("tests/mesh/solid_codec/previous_encoding"));
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

