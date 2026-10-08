// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_mesh/cook.h>
#include <impl/assets_mesh/binary_payload.h>

#include <tests/common/capturing_logger.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_solid_codec_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshName("tests/mesh/solid_codec");
inline constexpr AStringView s_MeshSource = R"(mesh fixture;
fixture.positions = [[0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1]];
fixture.normals = [[0, 0, 1]];
fixture.tangents = [[1, 0, 0, 1]];
fixture.uv0 = [[0, 0]];
fixture.colors = [[1, 1, 1, 1]];
fixture.vertex_refs = [[0, 0, 0, 0, 0], [1, 0, 0, 0, 0], [2, 0, 0, 0, 0], [3, 0, 0, 0, 0]];
fixture.indices = [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]];
)";

[[nodiscard]] static Expected<Core::Assets::AssetBytes> CookMeshBinary(Core::Assets::AssetArena& arena){
    Core::Metascript::Document document(arena);
    if(!document.parse(s_MeshSource))
        return MakeUnexpected(Failure{});
    Core::CpuTaskScheduler scheduler(1u);
    Core::Alloc::ScratchArena scratch(Name("tests/mesh/solid_codec/cook"));
    const Path sourcePath(arena, "tests/mesh/solid_codec.nwb");
    auto entry = Impl::ParseMeshCookMetadata(s_MeshName, sourcePath, document.asset(), arena, scheduler, scratch);
    if(!entry)
        return MakeUnexpected(Failure{});
    auto mesh = Impl::BuildMeshAsset(*entry, arena);
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
    NWB_MEMCPY(
        wrongCount.data() + offsetof(Impl::MeshBinaryPayload::MeshHeaderBinary, meshletPrimitiveIndexCount),
        sizeof(s_InconsistentIndexCount), &s_InconsistentIndexCount, sizeof(s_InconsistentIndexCount)
    );
    EXPECT_FALSE(rejected.loadBinary(wrongCount));
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
    constexpr u32 s_PreviousMeshMagic = 0x4d534835u;
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

