// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include "assets_graphics_material_sources.h"
#include "assets_graphics_mesh_sources.h"
#include "assets_graphics_shader_sources.h"

#include <pipeline/asset_builder/build.h>
#include <pipeline/asset_gatherer/gather.h>

#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_csg/cook.h>
#include <impl/assets_graphics/gather.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_material/binary_payload.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/cook.h>
#include <impl/assets_texture/asset.h>
#include <impl/assets_texture/cook.h>
#include <impl/assets_sampler/asset.h>
#include <impl/assets_sampler/binary_payload.h>
#include <impl/assets_sampler/cook.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_csg/shape_registry.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/cook_entry_registry.h>
#include <core/assets/paths.h>
#include <core/alloc/scratch.h>
#include <core/task/cpu/scheduler.h>
#include <core/common/module.h>
#include <core/mesh/classification.h>
#include <core/metascript/parser.h>
#include <core/filesystem/factory.h>
#include <core/graphics/api.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/spirv_entry_point.h>
#include <core/common/log.h>

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


class AssetsGraphicsFixture : public AssetsGraphicsMeshSources, public AssetsGraphicsMaterialSources, public AssetsGraphicsShaderSources{
public:
    using CapturingLogger = NWB::Tests::CapturingLogger;
    using AString = NWB::Tests::TestAString;
    using Path = NWB::Path;


public:

    struct AssetsGraphicsTestArenaTag{};


public:
    using TestArena = NWB::Tests::TestArena<AssetsGraphicsTestArenaTag>;


public:
    struct CookCase{
        Path root;
        Path outputDirectory;
    };

    struct MinimalAssetCookInfo{
        AStringView assetDirectory = "";
        AStringView assetFilename = "";
    };
    struct MaterialBinaryTypedLayoutOffsets{
        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
    };


public:

    static constexpr Name s_MaterialScratchArena = Name("tests/integration/assets_graphics/material");
    static constexpr Name s_ShaderScratchArena = Name("tests/integration/assets_graphics/shader");
    static constexpr Name s_ModelFixtureScratchArena = Name("tests/integration/assets_graphics/model_fixture");
    static constexpr Name s_CodecScratchArena = Name("tests/integration/assets_graphics/codec");


public:

    template<typename T>
    static NWB::Core::Assets::AssetVector<T> MakeAssetVector(TestArena& testArena)noexcept(noexcept(NWB::Core::Assets::AssetVector<T>(testArena.arena))){
        return NWB::Core::Assets::AssetVector<T>(testArena.arena);
    }
    static NWB::Core::Assets::AssetBytes MakeAssetBytes(TestArena& testArena)noexcept(noexcept(NWB::Core::Assets::AssetBytes(testArena.arena)));
    static void AppendTestMeta(AString& inOutMeta, const AStringView text);
#if defined(NWB_FINAL)
    static AString BuildTriangleMeta(const AStringView assetHeader, const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField, const AStringView suffix);
    static AString BuildMeshTriangleMeta(const AStringView normalField, const AStringView tangentField, const AStringView vertexRefsField);
#endif
    static bool PrepareCleanDirectory(const Path& directory);
    static bool WriteTextFile(const Path& filePath, const AStringView text);
    static AStringView AssetsGraphicsTestConfigurationName()noexcept;
    static Path AssetsGraphicsTestRepoRoot(TestArena& testArena);
    static Path AssetsGraphicsTestCaseRoot(TestArena& testArena, const AStringView caseName);
    static Expected<Path> PrepareAssetsGraphicsCaseRoot(TestArena& testArena, const AStringView caseName);
    static Expected<CookCase> PrepareAssetsGraphicsCookCase(TestArena& testArena, const AStringView caseName);
    static bool BuildPreparedGraphicsAssetRoots(TestArena& testArena, const Path& root, const Path& outputDirectory, const InitializerList<Path> assetRoots, const u32 workerThreadCount = 0u);
    static bool CookPreparedGraphicsAssetRoots(TestArena& testArena, const Path& root, const Path& outputDirectory, const InitializerList<Path> assetRoots, const u32 workerThreadCount = 0u);
    static bool CookSingleGraphicsMeta(const AStringView metaText, AStringView assetDirectory, AStringView assetFilename, TestArena& testArena, const CookCase& cookCase);
    static bool CookSingleMinimalAssetMeta(const AStringView metaText, const MinimalAssetCookInfo& cookInfo, TestArena& testArena, const CookCase& cookCase);
    static bool CookSingleMeshMeta(const AStringView metaText, TestArena& testArena, const CookCase& cookCase);
    static Expected<AString> ReadSmokeAssetMeta(TestArena& testArena, AStringView assetDirectory, AStringView assetFilename);
    static bool CookSmokeAssetMeta(AStringView assetDirectory, AStringView assetFilename, TestArena& testArena, const CookCase& cookCase);
    static bool CookSmokeMeshMeta(AStringView assetFilename, TestArena& testArena, const CookCase& cookCase);
    static bool CookMinimalMeshWithMaterialBind(const AStringView bindText, TestArena& testArena, const CookCase& cookCase);
    static Expected<NWB::Impl::MaterialBindEntry> ParseMaterialBindFromText(TestArena& testArena, const AStringView bindText, const Path& root, NWB::Core::Alloc::ScratchArena& scratchArena);
    #if defined(NWB_FINAL)
    [[nodiscard]] static Expected<Path> EmitDuplicateGeneratedMaterialBindIncludePath(TestArena& testArena, const CookCase& cookCase);
    #endif
    template<typename EntryT>
    static Expected<EntryT> ParseShaderMetadataFile(TestArena& testArena, NWB::Impl::ShaderCook& shaderCook, const Path& metadataPath, NWB::Core::Alloc::ScratchArena& scratchArena){
        NWB::Core::Metascript::Document document(testArena.arena);
        NWB::Impl::ShaderCook::CookString source(testArena.arena);
        if(!NWB::Core::Assets::ParseMetadataDocumentText(metadataPath, "Meta", source, document, [&document](const AStringView text){ return document.parse(text); }))
            return MakeUnexpected(Failure{});

        if constexpr(IsSame_V<EntryT, NWB::Impl::ShaderCook::ShaderEntry>)
            return shaderCook.parseShaderMeta(metadataPath, document, scratchArena);
        else{
            static_assert(IsSame_V<EntryT, NWB::Impl::ShaderCook::IncludeEntry>);
            return shaderCook.parseIncludeMeta(metadataPath, document, scratchArena);
        }
    }

    static bool WriteMaterialBindShaderProbeSource(TestArena& testArena, const Path& assetRoot, AStringView shaderAssetType, AStringView metaFilename, AStringView sourceFilename, const AStringView sourceText);

    static bool WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(TestArena& testArena, const Path& assetRoot, const AStringView bindText, const AStringView materialText, const AStringView pixelSourceText);
    static bool WriteMaterialBindMaterialIntegrationAssets(TestArena& testArena, const Path& assetRoot, const AStringView bindText, const AStringView materialText);

    static bool CookMaterialBindMaterialIntegrationWithPixelSource(const AStringView bindText, const AStringView materialText, const AStringView pixelSourceText, TestArena& testArena, const CookCase& cookCase);

    static bool CookMaterialBindMaterialIntegration(const AStringView bindText, const AStringView materialText, TestArena& testArena, const CookCase& cookCase);
    // Only volumes selecting graphics metadata or materials include a shader archive index.
    template<typename AssetCodecT>
    static Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedAsset(TestArena& testArena, const Path& outputDirectory, const Name assetName, const usize expectedVolumeFileCount){
        NWB::Core::Filesystem::VolumeMountDesc mountDesc(testArena.arena);
        mountDesc.volumeName = "graphics";
        mountDesc.mountDirectory = outputDirectory;
        auto filesystemResult = NWB::Core::Filesystem::CreateFilesystem(testArena.arena, mountDesc);
        const bool loadedVolume = static_cast<bool>(filesystemResult);
        EXPECT_TRUE(loadedVolume);
        if(!loadedVolume)
            return MakeUnexpected(filesystemResult.error());
        UniquePtr<NWB::Core::Filesystem::IFilesystem> filesystem = Move(*filesystemResult);

        if(expectedVolumeFileCount != 0u)
            EXPECT_EQ(filesystem->fileCount(), expectedVolumeFileCount);

        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        const bool loadedBinary = filesystem->readFile(assetName, binary);
        EXPECT_TRUE(loadedBinary);
        EXPECT_FALSE(binary.empty());
        if(!loadedBinary || binary.empty())
            return MakeUnexpected(Failure{});

        AssetCodecT codec;
        auto deserialized = codec.deserialize(testArena.arena, assetName, binary);
        EXPECT_TRUE(deserialized);
        return deserialized;
    }
    static Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedMinimalMesh(TestArena& testArena, const Path& outputDirectory);
    static Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedMesh(TestArena& testArena, const Path& outputDirectory, const Name assetName);
    static Expected<UniquePtr<NWB::Core::Assets::IAsset>> LoadCookedMaterial(TestArena& testArena, const Path& outputDirectory, const Name assetName);
    static Expected<NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>> LoadCookedShaderArchiveRecords(TestArena& testArena, const Path& outputDirectory);


public:
    template<typename T>
    static bool OverwritePOD(NWB::Core::Assets::AssetBytes& binary, const usize offset, const T value)noexcept(noexcept(NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value))) && IsNothrowDestructible_V<T>){
        if(offset > binary.size() || sizeof(value) > binary.size() - offset)
            return false;

        NWB_MEMCPY(binary.data() + offset, sizeof(value), &value, sizeof(value));
        return true;
    }

    static bool EncodeTestMeshletRefs(
        NWB::Core::Assets::AssetVector<NWB::Impl::MeshletDesc>& meshlets,
        const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletPositionStreamRef>& positionRefs,
        const NWB::Core::Assets::AssetVector<NWB::Impl::MeshletAttributeStreamRef>& attributeRefs,
        NWB::Core::Assets::AssetVector<u8>& outPositionRefDeltas,
        NWB::Core::Assets::AssetVector<u8>& outAttributeRefDeltas,
        const bool skinRequired
    );
    [[nodiscard]] static Expected<MaterialBinaryTypedLayoutOffsets> FindMaterialBinaryTypedLayoutOffsets(const NWB::Core::Assets::AssetBytes& binary)noexcept;
    static Expected<u64> FindShaderArchiveSourceChecksum(const NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record>& records, const Name shaderName, const Name stageName)noexcept;
    template<typename CodecT>
    static void CheckCodecRejectsBinary(TestArena& testArena, const CodecT& codec, const Name& virtualPath, const NWB::Core::Assets::AssetBytes& binary){
        EXPECT_FALSE(codec.deserialize(testArena.arena, virtualPath, binary));
    }


public:
    AssetsGraphicsFixture() = delete;
    AssetsGraphicsFixture(const AssetsGraphicsFixture&) = delete;
    ~AssetsGraphicsFixture() = delete;


public:
    AssetsGraphicsFixture& operator=(const AssetsGraphicsFixture&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

