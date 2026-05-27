// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_asset_cook.h"

#include "skinned_mesh_validation.h"
#include "mesh_asset_binary_payload.h"
#include "mesh_binary_payload.h"
#include "meshlet_payload_packing.h"

#include <core/alloc/scratch.h>
#include <core/assets/asset_paths.h>
#include <core/mesh/frame_math.h>
#include <core/mesh/tangent_frame_rebuild.h>
#include <core/metascript/parser.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_mesh_asset_cook{


#include "mesh_asset_cook_metadata.inl"

static bool ParseSourceMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    MeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);
static bool ParseSourceSkinnedMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset,
    SkinnedMeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
);

static bool ValidateMeshAssetFields(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset
){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        "Mesh meta",
        { "positions", "normals", "tangents", "uv0", "colors", "vertex_refs", "indices" }
    );
}

static bool ValidateSkinnedMeshAssetFields(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Value& asset
){
    return Core::Assets::ValidateMetadataAssetFields(
        discoveredFile.filePath,
        asset,
        "SkinnedMesh mesh meta",
        {
            "positions",
            "normals",
            "tangents",
            "uv0",
            "colors",
            "vertex_refs",
            "indices",
            "skin",
            "skeleton_joint_count",
            "inverse_bind_matrices",
        }
    );
}

static bool ParseMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = MeshCookEntry(outEntry.positions.get_allocator().arena());

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("Mesh meta '{}': asset is not a map"), PathToString<tchar>(discoveredFile.filePath));
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "Mesh",
        outEntry.virtualPath,
        scratchArena
    ))
        return false;
    if(!ValidateMeshAssetFields(discoveredFile, asset))
        return false;
    return ParseSourceMeshMeta(discoveredFile, asset, outEntry, scratchArena);
}

static bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    outMesh = Mesh(meshEntry.positions.get_allocator().arena(), meshEntry.virtualPath);
    outMesh.setPayload(
        Move(meshEntry.positions),
        Move(meshEntry.normals),
        Move(meshEntry.tangents),
        Move(meshEntry.uv0),
        Move(meshEntry.colors),
        Move(meshEntry.vertexRefs),
        Move(meshEntry.meshlets),
        Move(meshEntry.meshletBounds),
        Move(meshEntry.meshletVertexRefs),
        Move(meshEntry.meshletPrimitiveIndices)
    );
    return outMesh.validatePayload();
}

template<usize ComponentCount>
static bool ParseU16Tuple(
    const Path& nwbFilePath,
    const Core::Metascript::Value& value,
    const AStringView label,
    u16 (&outValues)[ComponentCount],
    Core::Alloc::ScratchArena& scratchArena
){
    if(!value.isList() || value.asList().size() != ComponentCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': '{}' must be a {}-component integer list")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
            , ComponentCount
        );
        return false;
    }

    const auto& list = value.asList();
    for(usize i = 0; i < ComponentCount; ++i){
        u32 parsed = 0u;
        const MetadataU32ValueFailure::Enum failure = ValidateMetadataU32Value(list[i], parsed);
        if(failure != MetadataU32ValueFailure::None){
            const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
            LogMetadataU32ValueFailure(nwbFilePath, s_SkinnedMeshMetaKind, componentLabel, failure);
            return false;
        }
        if(parsed > Limit<u16>::s_Max){
            const ScratchString componentLabel = MakeIndexedLabel(scratchArena, label, i);
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': '{}' contains a value that exceeds u16")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(componentLabel)
            );
            return false;
        }
        outValues[i] = static_cast<u16>(parsed);
    }
    return true;
}

static bool NormalizeSkinInfluenceWeights(
    const Path& nwbFilePath,
    const AStringView label,
    SkinInfluence4& influence
){
    const SIMDVector weights = VectorSet(
        influence.weight[0u],
        influence.weight[1u],
        influence.weight[2u],
        influence.weight[3u]
    );
    if(!SkinnedMeshValidation::FiniteVector(weights, 0xFu) || !Vector4GreaterOrEqual(weights, VectorZero())){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': '{}' weights must be finite and non-negative")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f32 weightSum = VectorGetX(Vector4Dot(weights, s_SIMDOne));
    if(!IsFinite(weightSum) || weightSum <= SkinnedMeshValidation::s_Epsilon){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': '{}' weights must contain a positive total")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }

    const f32 inverseWeightSum = 1.0f / weightSum;
    const SIMDVector normalizedWeights = VectorScale(weights, inverseWeightSum);
    influence.weight[0u] = VectorGetX(normalizedWeights);
    influence.weight[1u] = VectorGetY(normalizedWeights);
    influence.weight[2u] = VectorGetZ(normalizedWeights);
    influence.weight[3u] = VectorGetW(normalizedWeights);

    if(!SkinnedMeshValidation::ValidSkinInfluenceWeights(normalizedWeights)){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': '{}' weights failed normalization")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(label)
        );
        return false;
    }
    return true;
}

static bool ParseSkeletonJointCount(const Path& nwbFilePath, const Core::Metascript::Value& asset, u32& outJointCount){
    outJointCount = 0u;

    const Core::Metascript::Value* jointCount = FindField(asset, "skeleton_joint_count");
    if(!jointCount)
        return true;

    return ParseMetadataU32Value(nwbFilePath, *jointCount, s_SkinnedMeshMetaKind, "skeleton_joint_count", outJointCount);
}

static bool ParseInverseBindMatrices(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    const u32 skeletonJointCount,
    Core::Assets::AssetVector<SkinnedMeshJointMatrix>& outMatrices,
    Core::Alloc::ScratchArena& scratchArena
){
    outMatrices.clear();

    const Core::Metascript::Value* matrices = FindField(asset, "inverse_bind_matrices");
    if(!matrices || (matrices->isList() && matrices->asList().empty()) || (matrices->isMap() && matrices->asMap().empty()))
        return true;
    if(!matrices->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': 'inverse_bind_matrices' must be a list")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(skeletonJointCount == 0u || matrices->asList().size() != skeletonJointCount){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': inverse bind matrix count must match skeleton_joint_count")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const auto& matrixList = matrices->asList();
    outMatrices.reserve(matrixList.size());
    for(usize matrixIndex = 0u; matrixIndex < matrixList.size(); ++matrixIndex){
        const Core::Metascript::Value& matrixValue = matrixList[matrixIndex];
        if(!matrixValue.isList() || matrixValue.asList().size() != 4u){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': inverse_bind_matrices[{}] must contain four columns")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }

        SkinnedMeshJointMatrix matrix{};
        const auto& columns = matrixValue.asList();
        const ScratchString label = MakeIndexedLabel(scratchArena, "inverse_bind_matrices", matrixIndex);
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
            alignas(16) f32 column[4] = {};
            const ScratchString columnLabel = MakeIndexedLabel(scratchArena, label, columnIndex);
            if(!ParseMetadataF32Tuple(nwbFilePath, columns[columnIndex], s_SkinnedMeshMetaKind, columnLabel, column, scratchArena))
                return false;
            matrix.rows[columnIndex] = Float4(column[0u], column[1u], column[2u], column[3u]);
        }

        if(!SkinnedMeshValidation::ValidAffineJointMatrix(LoadFloat(matrix))){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': inverse_bind_matrices[{}] is not a finite invertible affine matrix")
                , PathToString<tchar>(nwbFilePath)
                , matrixIndex
            );
            return false;
        }
        outMatrices.push_back(matrix);
    }
    return true;
}

#include "mesh_asset_cook_source.inl"

static bool ParseSkinnedMeshMeta(
    const DiscoveredNwbFile& discoveredFile,
    const Core::Metascript::Document& doc,
    SkinnedMeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    outEntry = SkinnedMeshCookEntry(outEntry.positions.get_allocator().arena());

    const Core::Metascript::Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMesh mesh meta '{}': asset is not a map")
            , PathToString<tchar>(discoveredFile.filePath)
        );
        return false;
    }

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(
        discoveredFile.assetRoot,
        discoveredFile.virtualRoot,
        discoveredFile.filePath,
        asset,
        "SkinnedMesh",
        outEntry.virtualPath,
        scratchArena
    ))
        return false;

    if(!ValidateSkinnedMeshAssetFields(discoveredFile, asset))
        return false;
    outEntry.meshClass = Core::Mesh::MeshClass::Skinned;
    return ParseSourceSkinnedMeshMeta(discoveredFile, asset, outEntry, scratchArena);
}

static bool BuildSkinnedMeshAsset(SkinnedMeshCookEntry& meshEntry, SkinnedMesh& outMesh){
    outMesh = SkinnedMesh(meshEntry.positions.get_allocator().arena(), meshEntry.virtualPath);

    outMesh.setMeshClass(meshEntry.meshClass);
    outMesh.setSkeletonJointCount(meshEntry.skeletonJointCount);
    outMesh.setPayload(
        Move(meshEntry.positions),
        Move(meshEntry.normals),
        Move(meshEntry.tangents),
        Move(meshEntry.uv0),
        Move(meshEntry.colors),
        Move(meshEntry.skin),
        Move(meshEntry.inverseBindMatrices),
        Move(meshEntry.vertexRefs),
        Move(meshEntry.meshlets),
        Move(meshEntry.meshletBounds),
        Move(meshEntry.meshletVertexRefs),
        Move(meshEntry.meshletPrimitiveIndices)
    );
    return outMesh.validatePayload();
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    MeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_mesh_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_mesh_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_mesh_asset_cook::ParseMeshMeta(discoveredFile, doc, outEntry, scratchArena);
}

bool ParseSkinnedMeshCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    SkinnedMeshCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    __hidden_mesh_asset_cook::DiscoveredNwbFile discoveredFile;
    if(!__hidden_mesh_asset_cook::BuildDiscoveredNwbFile(assetRoot, virtualRoot, nwbFilePath, discoveredFile))
        return false;
    return __hidden_mesh_asset_cook::ParseSkinnedMeshMeta(discoveredFile, doc, outEntry, scratchArena);
}

bool BuildMeshAsset(MeshCookEntry& meshEntry, Mesh& outMesh){
    return __hidden_mesh_asset_cook::BuildMeshAsset(meshEntry, outMesh);
}

bool BuildSkinnedMeshAsset(SkinnedMeshCookEntry& meshEntry, SkinnedMesh& outMesh){
    return __hidden_mesh_asset_cook::BuildSkinnedMeshAsset(meshEntry, outMesh);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool MeshAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("MeshAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Mesh::s_AssetTypeText)
        );
        return false;
    }

    const Mesh& mesh = static_cast<const Mesh&>(asset);
    if(!mesh.validatePayload())
        return false;

    usize reserveBytes = sizeof(MeshBinaryPayload::MeshHeaderBinary);
    const bool canReserve = MeshAssetBinaryPayload::AddMeshBaseReserveBytes(reserveBytes, mesh);

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    MeshBinaryPayload::MeshHeaderBinary header;
    MeshAssetBinaryPayload::FillMeshBaseHeader(header, mesh);
    AppendPOD(outBinary, header);

    const tchar* const serializeFailureContext = NWB_TEXT("MeshAssetCodec::serialize");
    if(!MeshAssetBinaryPayload::AppendMeshAttributeStreams(outBinary, mesh, serializeFailureContext))
        return false;
    return MeshAssetBinaryPayload::AppendMeshletStreams(outBinary, mesh, serializeFailureContext);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

