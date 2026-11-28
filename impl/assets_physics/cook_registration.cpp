// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_physics/cook_physics_assets.adhd.h>
#include <impl/assets_physics/cook_physics_rig_assets.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB::Impl{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC_SERIALIZE(PhysicsMaterialAssetCodec, PhysicsMaterialAsset, SerializePhysicsMaterialAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(CollisionLayerMatrixAssetCodec, CollisionLayerMatrixAsset, SerializeCollisionLayerMatrixAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(ConvexHullAssetCodec, ConvexHullAsset, SerializeConvexHullAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(CompoundHullAssetCodec, CompoundHullAsset, SerializeCompoundHullAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(CollisionMeshAssetCodec, CollisionMeshAsset, SerializeCollisionMeshAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(HeightfieldAssetCodec, HeightfieldAsset, SerializeHeightfieldAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(SdfAssetCodec, SdfAsset, SerializeSdfAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(ChainShapeAssetCodec, ChainShapeAsset, SerializeChainShapeAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(CompoundShapeAssetCodec, CompoundShapeAsset, SerializeCompoundShapeAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(FractureAssetCodec, FractureAsset, SerializeFractureAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(SoftBodyAssetCodec, SoftBodyAsset, SerializeSoftBodyAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(RagdollAssetCodec, RagdollAsset, SerializeRagdollAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(ArticulationAssetCodec, ArticulationAsset, SerializeArticulationAsset)
NWB_DEFINE_ASSET_CODEC_SERIALIZE(VehicleAssetCodec, VehicleAsset, SerializeVehicleAsset)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

