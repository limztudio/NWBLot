// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ProjectBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ProjectMagic = 0x50524A31u; // PRJ1
inline constexpr u32 s_ProjectVersion = 1u;
inline constexpr TStringView s_ProjectAssetKindLabel = NWB_TEXT("project");
inline constexpr TStringView s_ProjectLoadBinaryContext = NWB_TEXT("ProjectAsset::loadBinary");
inline constexpr TStringView s_ProjectSerializeContext = NWB_TEXT("ProjectAssetCodec::serialize");

struct ProjectHeaderBinary{
    u32 magic = s_ProjectMagic;
    u32 version = s_ProjectVersion;
    NameHash startSceneHash = {};
};
static_assert(IsStandardLayout_V<ProjectHeaderBinary>, "ProjectHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ProjectHeaderBinary>, "ProjectHeaderBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ComponentBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_ComponentMagic = 0x434D5031u; // CMP1
inline constexpr u32 s_ComponentVersion = 1u;
inline constexpr TStringView s_ComponentAssetKindLabel = NWB_TEXT("component");
inline constexpr TStringView s_ComponentLoadBinaryContext = NWB_TEXT("IComponent::loadBinary");
inline constexpr TStringView s_ComponentSerializeContext = NWB_TEXT("ComponentAssetCodec::serialize");
inline constexpr TStringView s_TransformComponentAssetKindLabel = NWB_TEXT("transform_component");
inline constexpr TStringView s_CameraComponentAssetKindLabel = NWB_TEXT("camera_component");
inline constexpr TStringView s_LightComponentAssetKindLabel = NWB_TEXT("light_component");
inline constexpr TStringView s_ModelComponentAssetKindLabel = NWB_TEXT("model_component");
inline constexpr TStringView s_MeshComponentAssetKindLabel = NWB_TEXT("mesh_component");
inline constexpr TStringView s_RendererComponentAssetKindLabel = NWB_TEXT("renderer_component");

struct ComponentHeaderBinary{
    u32 magic = s_ComponentMagic;
    u32 version = s_ComponentVersion;
    NameHash componentTypeHash = {};
    u64 payloadByteCount = 0u;
};
static_assert(IsStandardLayout_V<ComponentHeaderBinary>, "ComponentHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<ComponentHeaderBinary>, "ComponentHeaderBinary must stay binary-serializable");

struct TransformComponentPayloadBinary{
    Float4 position = Float4(0.f, 0.f, 0.f);
    Float4 rotation = Float4(0.f, 0.f, 0.f, 1.f);
    Float4 scale = Float4(1.f, 1.f, 1.f);
};
static_assert(IsStandardLayout_V<TransformComponentPayloadBinary>, "TransformComponentPayloadBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<TransformComponentPayloadBinary>, "TransformComponentPayloadBinary must stay binary-serializable");

struct CameraComponentPayloadBinary{
    Float4 projection = Float4(0.f, 0.f, 0.f, 0.f);
};
static_assert(IsStandardLayout_V<CameraComponentPayloadBinary>, "CameraComponentPayloadBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<CameraComponentPayloadBinary>, "CameraComponentPayloadBinary must stay binary-serializable");

struct LightComponentPayloadBinary{
    Float4 colorIntensity = Float4(1.f, 1.f, 1.f, 1.f);
    f32 range = 10.f;
    f32 innerConeCos = 0.95f;
    f32 outerConeCos = 0.90f;
    f32 angularRadius = 0.00465f;
    f32 sourceRadius = 0.1f;
    u8 type = 0u;
    u8 enableCaustics = 1u;
    u16 reserved = 0u;
};
static_assert(IsStandardLayout_V<LightComponentPayloadBinary>, "LightComponentPayloadBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<LightComponentPayloadBinary>, "LightComponentPayloadBinary must stay binary-serializable");

struct SingleAssetRefComponentPayloadBinary{
    NameHash assetHash = {};
};
static_assert(IsStandardLayout_V<SingleAssetRefComponentPayloadBinary>, "SingleAssetRefComponentPayloadBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SingleAssetRefComponentPayloadBinary>, "SingleAssetRefComponentPayloadBinary must stay binary-serializable");

struct RendererComponentPayloadBinary{
    NameHash materialHash = {};
    u8 visible = 1u;
    u8 reserved0 = 0u;
    u16 reserved1 = 0u;
    u32 reserved2 = 0u;
};
static_assert(IsStandardLayout_V<RendererComponentPayloadBinary>, "RendererComponentPayloadBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<RendererComponentPayloadBinary>, "RendererComponentPayloadBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EntityBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_EntityMagic = 0x454E5431u; // ENT1
inline constexpr u32 s_EntityVersion = 1u;
inline constexpr TStringView s_EntityAssetKindLabel = NWB_TEXT("entity");
inline constexpr TStringView s_EntityLoadBinaryContext = NWB_TEXT("EntityAsset::loadBinary");
inline constexpr TStringView s_EntitySerializeContext = NWB_TEXT("EntityAssetCodec::serialize");
inline constexpr TStringView s_ComponentsPayloadLabel = NWB_TEXT("entity components");

struct EntityHeaderBinary{
    u32 magic = s_EntityMagic;
    u32 version = s_EntityVersion;
    u64 componentCount = 0u;
};
static_assert(IsStandardLayout_V<EntityHeaderBinary>, "EntityHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<EntityHeaderBinary>, "EntityHeaderBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace SceneBinaryPayload{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_SceneMagic = 0x53434E31u; // SCN1
inline constexpr u32 s_SceneVersion = 1u;
inline constexpr TStringView s_SceneAssetKindLabel = NWB_TEXT("scene");
inline constexpr TStringView s_SceneLoadBinaryContext = NWB_TEXT("SceneAsset::loadBinary");
inline constexpr TStringView s_SceneSerializeContext = NWB_TEXT("SceneAssetCodec::serialize");
inline constexpr TStringView s_SubsystemsPayloadLabel = NWB_TEXT("scene subsystems");
inline constexpr TStringView s_EntitiesPayloadLabel = NWB_TEXT("scene entities");
inline constexpr TStringView s_AssetsPayloadLabel = NWB_TEXT("scene referenced assets");

struct SceneHeaderBinary{
    u32 magic = s_SceneMagic;
    u32 version = s_SceneVersion;
    u64 subsystemCount = 0u;
    u64 entityCount = 0u;
    u64 assetCount = 0u;
};
static_assert(IsStandardLayout_V<SceneHeaderBinary>, "SceneHeaderBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SceneHeaderBinary>, "SceneHeaderBinary must stay binary-serializable");

struct SceneAssetEntryBinary{
    NameHash nicknameHash = {};
    NameHash assetHash = {};
};
static_assert(IsStandardLayout_V<SceneAssetEntryBinary>, "SceneAssetEntryBinary must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<SceneAssetEntryBinary>, "SceneAssetEntryBinary must stay binary-serializable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

