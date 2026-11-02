// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "assets_graphics_material_sources.h"
#include "assets_graphics_mesh_sources.h"
#include "assets_graphics_shader_sources.h"

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <pipeline/asset_builder/build.h>
#include <pipeline/asset_gatherer/gather.h>

#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_encode.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_graphics/gather.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets_texture/asset.h>
#include <impl/assets_texture/cook.h>
#include <impl/assets_sampler/asset.h>
#include <impl/assets_sampler/cook.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_csg/shape_registry.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>
#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/common/module.h>
#include <core/metascript/parser.h>
#include <core/filesystem/factory.h>
#include <core/graphics/api.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/spirv_entry_point.h>

#include <global/assert.h>
#include <global/binary.h>
#include <global/compile.h>
#include <global/cpu_topology.h>
#include <global/filesystem.h>
#include <global/hash_utils.h>
#include <global/simd/convert.h>
#include <global/simdmath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsFixture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AssetsGraphicsTestArenaTag{};

using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestArenaTag>;

struct CookCase{
    Path root;
    Path outputDirectory;
};

struct MaterialBinaryTypedLayoutOffsets{
    usize layoutHashOffset = 0u;
    usize blockByteCountOffset = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MaterialScratchArena = Name("tests/integration/assets_graphics/material");
inline constexpr Name s_ShaderScratchArena = Name("tests/integration/assets_graphics/shader");
inline constexpr Name s_CodecScratchArena = Name("tests/integration/assets_graphics/codec");
inline constexpr Name s_BuildScratchArena = Name("tests/integration/assets_graphics/build");
inline constexpr Name s_GatherScratchArena = Name("tests/integration/assets_graphics/gather");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
NWB::Core::Assets::AssetVector<T> MakeAssetVector(TestArena& testArena)noexcept(noexcept(NWB::Core::Assets::AssetVector<T>(testArena.arena))){
    return NWB::Core::Assets::AssetVector<T>(testArena.arena);
}

inline NWB::Core::Assets::AssetBytes MakeAssetBytes(TestArena& testArena)noexcept(noexcept(NWB::Core::Assets::AssetBytes(testArena.arena))){
    return NWB::Core::Assets::AssetBytes(testArena.arena);
}

inline void AppendTestMeta(const AStringView text, TestAString& inOutMeta){
    inOutMeta.append(text.data(), text.size());
}

#if defined(NWB_FINAL)
TestAString BuildTriangleMeta(const AStringView assetHeader, const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField, const AStringView suffix);
inline TestAString BuildMeshTriangleMeta(const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField){
    return BuildTriangleMeta("mesh asset;\n\n", normalField, tangentField, vertexRefsField, "");
}
#endif
b8 PrepareCleanDirectory(const Path& directory);
b8 WriteTextFile(const Path& filePath, const AStringView text);
AStringView AssetsGraphicsTestConfigurationName()noexcept;
inline Path AssetsGraphicsTestRepoRoot(TestArena& testArena){
    return NWB::Tests::RepoRootOf(testArena.arena, __FILE__);
}
Path AssetsGraphicsTestCaseRoot(TestArena& testArena, const AStringView caseName);
Expected<Path> PrepareAssetsGraphicsCaseRoot(TestArena& testArena, const AStringView caseName);
Expected<CookCase> PrepareAssetsGraphicsCookCase(TestArena& testArena, const AStringView caseName);
b8 BuildPreparedGraphicsAssetRoots(TestArena& testArena, const Path& root, const Path& outputDirectory, const InitializerList<Path> assetRoots, const u32 workerThreadCount = 0u);
b8 CookPreparedGraphicsAssetRoots(TestArena& testArena, const Path& root, const Path& outputDirectory, const InitializerList<Path> assetRoots, const u32 workerThreadCount = 0u);
b8 CookSingleGraphicsMeta(const AStringView metaText, AStringView assetDirectory, AStringView assetFilename, const CookCase& cookCase, TestArena& testArena);
inline b8 CookSingleMeshMeta(const AStringView metaText, const CookCase& cookCase, TestArena& testArena){
    return CookSingleGraphicsMeta(metaText, "meshes", "minimal_mesh.nwb", cookCase, testArena);
}
Expected<TestAString> ReadSmokeAssetMeta(TestArena& testArena, AStringView assetDirectory, AStringView assetFilename);
b8 CookSmokeAssetMeta(AStringView assetDirectory, AStringView assetFilename, const CookCase& cookCase, TestArena& testArena);
b8 CookMinimalMeshWithMaterialBind(const AStringView bindText, const CookCase& cookCase, TestArena& testArena);
Expected<NWB::Impl::MaterialBindEntry> ParseMaterialBindFromText(const AStringView bindText, const Path& root, TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena);
#if defined(NWB_FINAL)
[[nodiscard]] Expected<Path> EmitDuplicateGeneratedMaterialBindIncludePath(TestArena& testArena, const CookCase& cookCase);
#endif
template<typename EntryT>
Expected<EntryT> ParseShaderMetadataFile(const Path& metadataPath, TestArena& testArena, NWB::Core::Alloc::ScratchArena& scratchArena, NWB::Impl::ShaderCook& shaderCook){
    NWB::Core::Metascript::Document document(testArena.arena);
    NWB::Impl::ShaderCook::CookString source(testArena.arena);
    if(!NWB::Core::Assets::ParseMetadataDocumentText(metadataPath, "Meta", [&document](const AStringView text){ return document.parse(text); }, source, document))
        return MakeUnexpected(Failure{});

    if constexpr(IsSame_V<EntryT, NWB::Impl::ShaderCook::ShaderEntry>)
        return shaderCook.parseShaderMeta(metadataPath, document, scratchArena);
    else{
        static_assert(IsSame_V<EntryT, NWB::Impl::ShaderCook::IncludeEntry>);
        return shaderCook.parseIncludeMeta(metadataPath, document, scratchArena);
    }
}

b8 WriteMaterialBindShaderProbeSource(TestArena& testArena, const Path& assetRoot, AStringView shaderAssetType, AStringView metaFilename, AStringView sourceFilename, const AStringView sourceText);

b8 WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(TestArena& testArena, const Path& assetRoot, const AStringView bindText, const AStringView materialText, const AStringView pixelSourceText);

b8 CookMaterialBindMaterialIntegrationWithPixelSource(const AStringView bindText, const AStringView materialText, const AStringView pixelSourceText, const CookCase& cookCase, TestArena& testArena);

inline b8 CookMaterialBindMaterialIntegration(const AStringView bindText, const AStringView materialText, const CookCase& cookCase, TestArena& testArena){
    return CookMaterialBindMaterialIntegrationWithPixelSource(bindText, materialText, s_MaterialBindShaderProbeSource, cookCase, testArena);
}
// Only volumes selecting graphics metadata or materials include a shader archive index.
template<typename AssetCodecT>
Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedAsset(const Path& outputDirectory, const Name assetName, const usize expectedVolumeFileCount, TestArena& testArena){
    NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
    mountDesc.volumeName = "graphics";
    mountDesc.mountDirectory = outputDirectory;
    auto filesystemResult = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
    const b8 loadedVolume = static_cast<b8>(filesystemResult);
    EXPECT_TRUE(loadedVolume);
    if(!loadedVolume)
        return MakeUnexpected(filesystemResult.error());
    UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = Move(*filesystemResult);

    if(expectedVolumeFileCount != 0u)
        EXPECT_EQ(filesystem->fileCount(), expectedVolumeFileCount);

    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    const b8 loadedBinary = filesystem->readFile(assetName, binary);
    EXPECT_TRUE(loadedBinary);
    EXPECT_FALSE(binary.empty());
    if(!loadedBinary || binary.empty())
        return MakeUnexpected(Failure{});

    AssetCodecT codec;
    auto deserialized = codec.deserialize(assetName, binary, testArena.arena);
    EXPECT_TRUE(deserialized);
    return deserialized;
}
inline Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedMaterial(TestArena& testArena, const Path& outputDirectory, const Name assetName){
    return LoadCookedAsset<NWB::Impl::MaterialAssetCodec>(outputDirectory, assetName, 0u, testArena);
}
Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> LoadCookedShaderArchiveRecords(TestArena& testArena, const Path& outputDirectory);

template<typename T>
b8 OverwritePOD(const usize offset, const T value, const Span<u8> binary)noexcept(noexcept(NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value))) && IsNothrowDestructible_V<T>){
    if(offset > binary.size() || sizeof(value) > binary.size() - offset)
        return false;

    NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
    return true;
}

b8 EncodeTestMeshletRefs(
    const Span<const NWB::Impl::MeshletPositionStreamRef> positionRefs,
    const Span<const NWB::Impl::MeshletAttributeStreamRef> attributeRefs,
    const b8 skinRequired,
    const Span<NWB::Impl::MeshletDesc> meshlets,
    NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas,
    NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas
);
[[nodiscard]] Expected<MaterialBinaryTypedLayoutOffsets> FindMaterialBinaryTypedLayoutOffsets(Span<const u8> binary)noexcept;
Expected<u64> FindShaderArchiveSourceChecksum(Span<const NWB::Core::ShaderArchive::Record> records, const Name shaderName, const Name stageName)noexcept;
template<typename CodecT>
void CheckCodecRejectsBinary(const CodecT& codec, const Name& virtualPath, const Span<const u8> binary, TestArena& testArena){
    EXPECT_FALSE(codec.deserialize(virtualPath, binary, testArena.arena));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

