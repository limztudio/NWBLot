// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include "deformable_runtime_names.h"
#include "shader_asset_loader.h"

#include <core/ecs/world.h>
#include <core/scene/scene.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <logger/client/logger.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_shader_stage_names.h>
#include <impl/assets_graphics/material_asset.h>
#include <impl/assets_graphics/shader_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
static constexpr u32 s_StaticGeometryVertexStride = sizeof(GeometryVertex);
static constexpr u32 s_MeshSourceLayoutGeometryVertex = 0u;
static constexpr u32 s_MeshSourceLayoutDeformableVertex = 1u;
static constexpr u32 s_EmulatedVertexStride = sizeof(f32) * 24u;
static constexpr u32 s_TrianglesPerWorkgroup = 32u;
static constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);
static constexpr u32 s_AvboitDownsample = 8u;
static constexpr u32 s_AvboitVirtualSlices = 128u;
static constexpr u32 s_AvboitPhysicalSlices = 64u;
static constexpr u32 s_AvboitExtinctionSlicesPerWord = 4u;
static constexpr f32 s_AvboitExtinctionFixedScale = 45.985905f;
static constexpr f32 s_AvboitSelfOcclusionSliceBias = 2.f;
static constexpr usize s_AvboitControlWordCount = 8u;
static constexpr f32 s_DefaultMeshViewYaw = 0.82f;
static constexpr f32 s_DefaultMeshViewPitch = 0.94f;
static constexpr f32 s_DefaultMeshViewDepthOffset = 2.2f;


struct ShaderDrivenPushConstants{
    u32 triangleCount = 0;
    u32 scissorCullEnabled = 0;
    u32 instanceIndex = 0;
    u32 sourceVertexLayout = 0;
    Float4 viewportRect = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 scissorRect = Float4(0.f, 0.f, 0.f, 0.f);
};

struct AvboitPushConstants{
    u32 frame[4] = {};
    u32 volume[4] = {};
    Float4 params = Float4(0.f, 0.f, 0.f, 0.f);
};

struct TransparentDrawPushConstants{
    ShaderDrivenPushConstants mesh;
    AvboitPushConstants avboit;
};

struct EmulatedVertex{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float4 uv0;
    Float4 color;
    Float4 worldPosition;
};

struct MeshViewGpuData{
    Float4 worldToClip[4] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, 0.f, 1.f),
    };
    Float4 directionalLightDirection = Float4(0.f, 0.f, -1.f, 0.f);
    Float4 directionalLightColorIntensity = Float4(1.f, 1.f, 1.f, 1.f);
    Float4 cameraPosition = Float4(0.f, 0.f, 0.f, 1.f);
};

using MeshViewState = MeshViewGpuData;

struct MeshViewBasis{
    Float4 right = Float4(1.f, 0.f, 0.f, 0.f);
    Float4 up = Float4(0.f, 1.f, 0.f, 0.f);
    Float4 forward = Float4(0.f, 0.f, 1.f, 0.f);
    Float4 positionDepthBias = Float4(0.f, 0.f, 0.f, 0.f);
};

struct MaterialParameterBlock{
    u32 offset = 0;
    u32 count = 0;
};

static_assert(sizeof(ShaderDrivenPushConstants) == 48, "ShaderDrivenPushConstants layout must stay stable");
static_assert(sizeof(AvboitPushConstants) == 48, "AvboitPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) == 96, "TransparentDrawPushConstants layout must stay stable");
static_assert(
    sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize,
    "Transparent draw push constants must fit the portable push constant budget"
);
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");
static_assert(alignof(EmulatedVertex) >= alignof(Float4), "EmulatedVertex must stay SIMD-aligned");
static_assert(sizeof(MeshViewGpuData) == sizeof(f32) * 28u, "MeshViewGpuData layout must match the mesh shaders");
static_assert(alignof(MeshViewGpuData) >= alignof(Float4), "MeshViewGpuData must stay SIMD-aligned");


static const Name& MeshEmulationVertexShaderName(){
    static const Name s("engine/graphics/mesh_emulation_vs");
    return s;
}

static const Name& InstanceBufferName(){
    static const Name s("ecs_graphics/instance_data");
    return s;
}

static const Name& MaterialParameterBufferName(){
    static const Name s("ecs_graphics/material_parameter_data");
    return s;
}

static const Name& MeshViewBufferName(){
    static const Name s("ecs_graphics/mesh_view_data");
    return s;
}

static const Name& DeferredCompositeVertexShaderName(){
    static const Name s("engine/graphics/deferred_composite_vs");
    return s;
}

static const Name& DeferredCompositePixelShaderName(){
    static const Name s("engine/graphics/deferred_composite_ps");
    return s;
}

static const Name& AvboitOccupancyPixelShaderName(){
    static const Name s("engine/graphics/avboit_occupancy_ps");
    return s;
}

static const Name& AvboitDepthWarpComputeShaderName(){
    static const Name s("engine/graphics/avboit_depth_warp_cs");
    return s;
}

static const Name& AvboitExtinctionPixelShaderName(){
    static const Name s("engine/graphics/avboit_extinction_ps");
    return s;
}

static const Name& AvboitIntegrateComputeShaderName(){
    static const Name s("engine/graphics/avboit_integrate_cs");
    return s;
}

static const Name& AvboitAccumulatePixelShaderName(){
    static const Name s("engine/graphics/avboit_accumulate_ps");
    return s;
}

static bool SupportsFormat(Core::IDevice& device, const Core::Format::Enum format, const Core::FormatSupport::Mask requiredSupport){
    return (device.queryFormatSupport(format) & requiredSupport) == requiredSupport;
}

template<usize N>
static Core::Format::Enum SelectSupportedFormat(
    Core::IDevice& device,
    const Core::Format::Enum (&candidates)[N],
    const Core::FormatSupport::Mask requiredSupport
){
    for(const Core::Format::Enum format : candidates){
        if(SupportsFormat(device, format, requiredSupport))
            return format;
    }

    return Core::Format::UNKNOWN;
}

static bool EnsurePointClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(false)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

static bool EnsureLinearClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(true)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

static Core::Format::Enum SelectGBufferAlbedoFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
        Core::Format::BGRA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectGBufferDepthFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::D32,
        Core::Format::D24S8,
        Core::Format::D16,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectAvboitAccumColorFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectAvboitAccumExtinctionFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
        Core::Format::R32_FLOAT,
        Core::Format::RGBA16_FLOAT,
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectAvboitTransmittanceFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::ShaderSample
        | Core::FormatSupport::ShaderUavStore
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectAvboitLowRasterFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::RenderState BuildGeometryRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .enableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip();
    return renderState;
}

static Core::BlendState::RenderTarget BuildAdditiveBlendTarget(const Core::ColorMask::Mask colorWriteMask = Core::ColorMask::All){
    Core::BlendState::RenderTarget target;
    target
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::One)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::One)
        .setBlendOpAlpha(Core::BlendOp::Add)
        .setColorWriteMask(colorWriteMask)
    ;
    return target;
}

static Core::RenderState BuildAvboitVoxelRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    renderState.blendState.targets[0].setColorWriteMask(Core::ColorMask::None);
    return renderState;
}

static Core::RenderState BuildAvboitAccumulateRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .disableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip().setCullNone();
    renderState.blendState
        .setRenderTarget(0, BuildAdditiveBlendTarget())
        .setRenderTarget(1, BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

static Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::Opaque:
        return BuildGeometryRenderState();
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return BuildAvboitVoxelRenderState();
    case MaterialPipelinePass::AvboitAccumulate:
        return BuildAvboitAccumulateRenderState();
    default:
        return BuildGeometryRenderState();
    }
}

static Core::RenderState BuildCompositeRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}

static bool TryFindShaderForStage(const Material& material, const Core::ShaderType::Mask shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset){
    outShaderAsset.reset();

    const Name& stageName = Core::ShaderStageNames::ArchiveStageNameFromShaderType(shaderType);
    if(!stageName)
        return false;

    return material.findShaderForStage(stageName, outShaderAsset);
}

static u32 ComputeDispatchGroupCount(const u32 triangleCount){
    return DivideUp(triangleCount, s_TrianglesPerWorkgroup);
}

static usize NextGrowingCapacity(const usize currentCapacity, const usize requiredCapacity){
    usize capacity = Max<usize>(currentCapacity, 1u);
    while(capacity < requiredCapacity){
        if(capacity > (Limit<usize>::s_Max / 2u))
            return requiredCapacity;
        capacity *= 2u;
    }
    return capacity;
}

static u32 FloatBits(const f32 value){
    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

static bool EqualsAsciiToken(const AStringView text, const AStringView expected){
    return text == expected;
}

static bool ParseMaterialParameterTypeText(
    const AStringView typeText,
    MaterialParameterValueType::Enum& outType,
    u32& outComponentCount
){
    outType = MaterialParameterValueType::None;
    outComponentCount = 0u;

    auto tryMatch = [&](const AStringView baseName, const AStringView vectorName, const MaterialParameterValueType::Enum type) -> bool{
        if(EqualsAsciiToken(typeText, baseName)){
            outType = type;
            outComponentCount = 1u;
            return true;
        }

        const auto parseSuffix = [&](const AStringView prefix) -> bool{
            if(typeText.size() != prefix.size() + 1u)
                return false;
            for(usize i = 0; i < prefix.size(); ++i){
                if(typeText[i] != prefix[i])
                    return false;
            }

            const char suffix = typeText[prefix.size()];
            if(suffix < '1' || suffix > '4')
                return false;

            outType = type;
            outComponentCount = static_cast<u32>(suffix - '0');
            return true;
        };

        return parseSuffix(baseName) || (!vectorName.empty() && parseSuffix(vectorName));
    };

    return
        tryMatch(AStringView("float"), AStringView("vec"), MaterialParameterValueType::Float)
        || tryMatch(AStringView("int"), AStringView("ivec"), MaterialParameterValueType::Int)
        || tryMatch(AStringView("uint"), AStringView("uvec"), MaterialParameterValueType::UInt)
        || tryMatch(AStringView("bool"), AStringView("bvec"), MaterialParameterValueType::Bool)
    ;
}

static bool SplitMaterialParameterCall(const AStringView text, AStringView& outType, AStringView& outArgs){
    const AStringView trimmed = TrimView(text);
    usize openParen = Limit<usize>::s_Max;
    for(usize i = 0; i < trimmed.size(); ++i){
        if(trimmed[i] == '('){
            openParen = i;
            break;
        }
    }
    if(openParen == Limit<usize>::s_Max || trimmed.empty() || trimmed[trimmed.size() - 1u] != ')')
        return false;

    outType = TrimView(trimmed.substr(0u, openParen));
    outArgs = TrimView(trimmed.substr(openParen + 1u, trimmed.size() - openParen - 2u));
    return !outType.empty() && !outArgs.empty();
}

static bool ReadMaterialParameterToken(const AStringView text, usize& inOutCursor, AStringView& outToken){
    while(inOutCursor < text.size() && (IsAsciiSpace(text[inOutCursor]) || text[inOutCursor] == ','))
        ++inOutCursor;
    if(inOutCursor >= text.size())
        return false;

    const usize begin = inOutCursor;
    while(inOutCursor < text.size() && !IsAsciiSpace(text[inOutCursor]) && text[inOutCursor] != ',')
        ++inOutCursor;

    outToken = TrimView(text.substr(begin, inOutCursor - begin));
    return !outToken.empty();
}

static bool SplitMaterialParameterTokens(const AStringView text, AStringView (&outTokens)[4], u32& outTokenCount){
    outTokenCount = 0u;
    usize cursor = 0u;
    AStringView token;
    while(ReadMaterialParameterToken(text, cursor, token)){
        if(outTokenCount >= 4u)
            return false;

        outTokens[outTokenCount] = token;
        ++outTokenCount;
    }

    return outTokenCount > 0u;
}

static bool ParseMaterialBoolToken(const AStringView token, u32& outValue){
    if(EqualsAsciiToken(token, AStringView("true")) || EqualsAsciiToken(token, AStringView("1"))){
        outValue = 1u;
        return true;
    }
    if(EqualsAsciiToken(token, AStringView("false")) || EqualsAsciiToken(token, AStringView("0"))){
        outValue = 0u;
        return true;
    }

    return false;
}

static bool ParseMaterialParameterToken(const AStringView token, const MaterialParameterValueType::Enum type, u32& outValue){
    const char* begin = token.data();
    const char* end = begin + token.size();

    switch(type){
    case MaterialParameterValueType::Float:{
        f64 parsed = 0.0;
        if(!ParseF64FromChars(begin, end, parsed) || !IsFinite(parsed))
            return false;
        if(parsed < static_cast<f64>(Limit<f32>::s_Min) || parsed > static_cast<f64>(Limit<f32>::s_Max))
            return false;

        outValue = FloatBits(static_cast<f32>(parsed));
        return true;
    }
    case MaterialParameterValueType::Int:{
        i64 parsed = 0;
        if(!ParseI64FromChars(begin, end, parsed))
            return false;
        if(parsed < static_cast<i64>(Limit<i32>::s_Min) || parsed > static_cast<i64>(Limit<i32>::s_Max))
            return false;

        outValue = static_cast<u32>(static_cast<i32>(parsed));
        return true;
    }
    case MaterialParameterValueType::UInt:{
        u64 parsed = 0u;
        if(!ParseU64FromChars(begin, end, parsed) || parsed > static_cast<u64>(Limit<u32>::s_Max))
            return false;

        outValue = static_cast<u32>(parsed);
        return true;
    }
    case MaterialParameterValueType::Bool:
        return ParseMaterialBoolToken(token, outValue);
    default:
        return false;
    }
}

static bool TryBuildMaterialParameterGpuData(
    const CompactString& key,
    const CompactString& value,
    MaterialParameterGpuData& outParameter
){
    outParameter = {};
    if(!key || !value)
        return false;

    MaterialParameterValueType::Enum valueType = MaterialParameterValueType::Float;
    u32 componentCount = 0u;
    AStringView valueText = TrimView(value.view());
    AStringView argsText = valueText;
    AStringView typeText;
    if(SplitMaterialParameterCall(valueText, typeText, argsText)){
        if(!ParseMaterialParameterTypeText(typeText, valueType, componentCount))
            return false;
    }
    else if(ParseMaterialBoolToken(valueText, outParameter.data.x)){
        valueType = MaterialParameterValueType::Bool;
        componentCount = 1u;
    }

    AStringView tokens[4];
    u32 tokenCount = 0u;
    if(!SplitMaterialParameterTokens(argsText, tokens, tokenCount))
        return false;
    if(componentCount != 0u && tokenCount != componentCount)
        return false;
    if(componentCount == 0u)
        componentCount = tokenCount;

    for(u32 i = 0; i < tokenCount; ++i){
        if(!ParseMaterialParameterToken(tokens[i], valueType, outParameter.data.raw[i]))
            return false;
    }

    const u64 keyHash = UpdateFnv64TextCanonical(FNV64_OFFSET_BASIS, key.view());
    outParameter.meta.x = static_cast<u32>(keyHash & 0xffffffffull);
    outParameter.meta.y = static_cast<u32>(keyHash >> 32u);
    outParameter.meta.z = static_cast<u32>(valueType);
    outParameter.meta.w = componentCount;
    return true;
}

static InstanceGpuData BuildInstanceGpuData(
    const Core::Scene::TransformComponent* transform,
    const u32 materialParameterOffset,
    const u32 materialParameterCount
){
    InstanceGpuData data;
    data.materialParameters.x = materialParameterOffset;
    data.materialParameters.y = materialParameterCount;
    if(!transform)
        return data;

    data.rotation = transform->rotation;
    data.translation = transform->position;
    data.scale = transform->scale;
    return data;
}

static f32 Float3Dot(const SIMDVector lhs, const SIMDVector rhs){
    return VectorGetX(Vector3Dot(lhs, rhs));
}

static void StoreRotatedBasisVector(Float4& outVector, const Float4& localVector, SIMDVector rotation){
    StoreFloat(Vector3Rotate(LoadFloat(localVector), rotation), &outVector);
}

static MeshViewBasis BuildDefaultMeshViewBasis(){
    SIMDVector sinAngles;
    SIMDVector cosAngles;
    VectorSinCos(&sinAngles, &cosAngles, VectorSet(s_DefaultMeshViewYaw, s_DefaultMeshViewPitch, 0.0f, 0.0f));
    const f32 sinYaw = VectorGetX(sinAngles);
    const f32 cosYaw = VectorGetX(cosAngles);
    const f32 sinPitch = VectorGetY(sinAngles);
    const f32 cosPitch = VectorGetY(cosAngles);

    MeshViewBasis basis;
    basis.right = Float4(cosYaw, 0.0f, sinYaw, 0.0f);
    basis.up = Float4(sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch, 0.0f);
    basis.forward = Float4(-sinYaw * cosPitch, sinPitch, cosYaw * cosPitch, 0.0f);
    basis.positionDepthBias.w = s_DefaultMeshViewDepthOffset;
    return basis;
}

static MeshViewBasis BuildTransformMeshViewBasis(const Core::Scene::TransformComponent& transform){
    MeshViewBasis basis;
    basis.positionDepthBias = transform.position;
    const SIMDVector rotation = LoadFloat(transform.rotation);
    StoreRotatedBasisVector(basis.right, Float4(1.0f, 0.0f, 0.0f), rotation);
    StoreRotatedBasisVector(basis.up, Float4(0.0f, 1.0f, 0.0f), rotation);
    StoreRotatedBasisVector(basis.forward, Float4(0.0f, 0.0f, 1.0f), rotation);
    return basis;
}

static void StoreDirectionalLightDirection(Float4& outDirection, const Float4& forward){
    const SIMDVector lightDirection = VectorNegate(LoadFloat(forward));
    const f32 lightDirectionLengthSquared = VectorGetX(Vector3LengthSq(lightDirection));
    if(!IsFinite(lightDirectionLengthSquared) || lightDirectionLengthSquared <= 0.0001f){
        outDirection = Float4(0.0f, 0.0f, -1.0f, 0.0f);
        return;
    }

    StoreFloat(VectorSetW(Vector3Normalize(lightDirection), 0.0f), &outDirection);
}

static void ApplyDefaultDirectionalLightMeshViewState(MeshViewState& state, const MeshViewBasis& basis){
    StoreDirectionalLightDirection(state.directionalLightDirection, basis.forward);
    state.directionalLightColorIntensity = Float4(1.0f, 1.0f, 1.0f, 1.0f);
}

static void ApplyDefaultCameraPositionMeshViewState(MeshViewState& state, const MeshViewBasis& basis){
    state.cameraPosition = Float4(
        basis.positionDepthBias.x,
        basis.positionDepthBias.y,
        basis.positionDepthBias.z,
        1.0f
    );
}

static bool TryApplyDirectionalLightMeshViewState(
    MeshViewState& state,
    const Core::Scene::TransformComponent& transform,
    const Core::Scene::LightComponent& light){
    if(light.type != Core::Scene::LightType::Directional)
        return false;

    const SIMDVector rotation = LoadFloat(transform.rotation);
    const f32 rotationLengthSquared = VectorGetX(QuaternionLengthSq(rotation));
    if(
        QuaternionIsNaN(rotation)
        || QuaternionIsInfinite(rotation)
        || !IsFinite(rotationLengthSquared)
        || rotationLengthSquared <= 0.0001f
    ){
        return false;
    }

    const SIMDVector lightColorIntensity = LoadFloat(light.colorIntensity);
    if(
        Vector3IsNaN(lightColorIntensity)
        || Vector3IsInfinite(lightColorIntensity)
        || !IsFinite(light.intensity())
        || light.intensity() <= 0.0f
    ){
        return false;
    }

    Float4 lightForward;
    StoreRotatedBasisVector(lightForward, Float4(0.0f, 0.0f, 1.0f), rotation);
    StoreDirectionalLightDirection(state.directionalLightDirection, lightForward);
    state.directionalLightColorIntensity = light.colorIntensity;
    return true;
}

static void BuildCameraProjectionParams(
    const Core::Scene::CameraComponent& camera,
    const f32 fallbackAspectRatio,
    Float4& outProjectionParams
){
    if(Core::Scene::TryBuildCameraProjectionParams(camera, fallbackAspectRatio, outProjectionParams))
        return;

    Core::Scene::CameraComponent fallbackCamera;
    if(Core::Scene::TryBuildCameraProjectionParams(fallbackCamera, fallbackAspectRatio, outProjectionParams))
        return;

    outProjectionParams = Float4(1.0f, 1.0f, 1.0f, 0.0f);
}

static void StoreProjectedViewColumn(
    Float4 (&outWorldToClip)[4],
    const usize columnIndex,
    const f32 viewX,
    const f32 viewY,
    const f32 viewZ,
    const f32 viewW,
    const SIMDVector projection
){
    SIMDVector column = VectorMultiply(VectorSet(viewX, viewY, viewZ, viewZ), projection);
    column = VectorMultiplyAdd(VectorSet(0.0f, 0.0f, viewW, 0.0f), VectorSplatW(projection), column);
    column = VectorSetW(column, viewZ);
    StoreFloat(column, &outWorldToClip[columnIndex]);
}

static void StoreWorldToClipMatrix(Float4 (&outWorldToClip)[4], const MeshViewBasis& basis, const Float4& projectionParams){
    const SIMDVector positionDepthBias = LoadFloat(basis.positionDepthBias);
    const SIMDVector right = LoadFloat(basis.right);
    const SIMDVector up = LoadFloat(basis.up);
    const SIMDVector forward = LoadFloat(basis.forward);
    const SIMDVector projection = LoadFloat(projectionParams);
    const f32 translationX = -Float3Dot(positionDepthBias, right);
    const f32 translationY = -Float3Dot(positionDepthBias, up);
    const f32 translationZ = -Float3Dot(positionDepthBias, forward) + basis.positionDepthBias.w;

    StoreProjectedViewColumn(
        outWorldToClip,
        0u,
        basis.right.x,
        basis.up.x,
        basis.forward.x,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        1u,
        basis.right.y,
        basis.up.y,
        basis.forward.y,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        2u,
        basis.right.z,
        basis.up.z,
        basis.forward.z,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        3u,
        translationX,
        translationY,
        translationZ,
        1.0f,
        projection
    );
}

static f32 ExtentAspectRatio(const u32 width, const u32 height){
    if(width == 0 || height == 0)
        return 1.0f;

    return static_cast<f32>(width) / static_cast<f32>(height);
}

static f32 FramebufferAspectRatio(const Core::IFramebuffer& framebuffer){
    const Core::FramebufferInfoEx& framebufferInfo = framebuffer.getFramebufferInfo();
    return ExtentAspectRatio(framebufferInfo.width, framebufferInfo.height);
}

static void ApplyDefaultCameraMeshViewState(MeshViewState& state, const MeshViewBasis& basis, const f32 fallbackAspectRatio){
    Core::Scene::CameraComponent camera;
    Float4 projectionParams;
    BuildCameraProjectionParams(camera, fallbackAspectRatio, projectionParams);
    StoreWorldToClipMatrix(state.worldToClip, basis, projectionParams);
    ApplyDefaultCameraPositionMeshViewState(state, basis);
}

static void ApplyCameraMeshViewState(
    MeshViewState& state,
    const Core::Scene::TransformComponent& transform,
    const Core::Scene::CameraProjectionData& projectionData
){
    StoreWorldToClipMatrix(state.worldToClip, BuildTransformMeshViewBasis(transform), projectionData.projectionParams);
    StoreFloat(VectorSetW(LoadFloat(transform.position), 1.0f), &state.cameraPosition);
}

static MeshViewState ResolveMeshViewState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    MeshViewState state;
    MeshViewBasis defaultBasis;
    bool defaultBasisResolved = false;
    auto resolveDefaultBasis = [&]() -> const MeshViewBasis&{
        if(!defaultBasisResolved){
            defaultBasis = BuildDefaultMeshViewBasis();
            defaultBasisResolved = true;
        }
        return defaultBasis;
    };

    const Core::Scene::SceneCameraView cameraView = Core::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        __hidden_ecs_graphics::ApplyCameraMeshViewState(
            state,
            *cameraView.transform,
            cameraView.projectionData
        );
    }
    else{
        __hidden_ecs_graphics::ApplyDefaultCameraMeshViewState(
            state,
            resolveDefaultBasis(),
            fallbackAspectRatio
        );
    }

    bool directionalLightApplied = false;
    const auto lightView = world.view<Core::Scene::TransformComponent, Core::Scene::LightComponent>();
    for(auto it = lightView.begin(); it != lightView.end(); ++it){
        auto&& [entity, transform, light] = *it;
        static_cast<void>(entity);
        if(TryApplyDirectionalLightMeshViewState(state, transform, light)){
            directionalLightApplied = true;
            break;
        }
    }
    if(!directionalLightApplied)
        __hidden_ecs_graphics::ApplyDefaultDirectionalLightMeshViewState(state, resolveDefaultBasis());

    return state;
}

static ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 triangleCount,
    const u32 instanceIndex,
    const u32 sourceVertexLayout,
    const Core::ViewportState& viewportState
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.triangleCount = triangleCount;
    pushConstants.instanceIndex = instanceIndex;
    pushConstants.sourceVertexLayout = sourceVertexLayout;

    if(viewportState.viewports.empty())
        return pushConstants;

    const Core::Viewport& viewport = viewportState.viewports[0];
    pushConstants.scissorCullEnabled = 1;
    pushConstants.viewportRect = Float4(viewport.minX, viewport.minY, viewport.maxX, viewport.maxY);

    Core::Rect scissorRect(viewport);
    if(!viewportState.scissorRects.empty())
        scissorRect = viewportState.scissorRects[0];

    pushConstants.scissorRect = Float4(
        static_cast<f32>(scissorRect.minX),
        static_cast<f32>(scissorRect.minY),
        static_cast<f32>(scissorRect.maxX),
        static_cast<f32>(scissorRect.maxY)
    );
    return pushConstants;
}

static bool EqualsAsciiTokenIgnoreCase(const AStringView text, const AStringView expected){
    if(text == expected)
        return true;
    if(text.size() != expected.size())
        return false;

    for(usize i = 0; i < text.size(); ++i){
        const char ch = text[i];
        const char lowered = (ch >= 'A' && ch <= 'Z') ? static_cast<char>(ch + ('a' - 'A')) : ch;
        if(lowered != expected[i])
            return false;
    }
    return true;
}

static bool IsTransparentText(const AStringView text){
    return
        EqualsAsciiTokenIgnoreCase(text, "transparent")
        || EqualsAsciiTokenIgnoreCase(text, "translucent")
        || EqualsAsciiTokenIgnoreCase(text, "blend")
        || EqualsAsciiTokenIgnoreCase(text, "alpha")
        || EqualsAsciiTokenIgnoreCase(text, "avboit")
        || EqualsAsciiTokenIgnoreCase(text, "true")
        || EqualsAsciiTokenIgnoreCase(text, "1")
    ;
}

static bool ParseAlphaValue(const AStringView text, f32& outAlpha){
    const char* begin = text.data();
    const char* end = begin + text.size();
    while(begin < end && IsAsciiSpace(*begin))
        ++begin;
    while(end > begin && IsAsciiSpace(*(end - 1)))
        --end;
    if(begin == end)
        return false;

    f64 parsed = 0.0;
    if(!ParseF64FromChars(begin, end, parsed) || !IsFinite(parsed))
        return false;
    if(parsed < static_cast<f64>(Limit<f32>::s_Min) || parsed > static_cast<f64>(Limit<f32>::s_Max))
        return false;

    outAlpha = static_cast<f32>(Max<f64>(0.0, Min<f64>(1.0, parsed)));
    return true;
}

static u32 MaterialAlphaParameterPriority(const CompactString& key){
    if(EqualsAsciiToken(key.view(), "alpha"))
        return 0u;
    if(EqualsAsciiToken(key.view(), "opacity"))
        return 1u;

    return Limit<u32>::s_Max;
}

static u32 MaterialModeParameterPriority(const CompactString& key){
    if(EqualsAsciiToken(key.view(), "render_mode"))
        return 0u;
    if(EqualsAsciiToken(key.view(), "alpha_mode"))
        return 1u;
    if(EqualsAsciiToken(key.view(), "transparency"))
        return 2u;

    return Limit<u32>::s_Max;
}

static AvboitPushConstants BuildAvboitPushConstants(const RendererSystem::AvboitFrameTargets& targets, const f32 alpha){
    AvboitPushConstants pushConstants;
    pushConstants.frame[0] = targets.fullWidth;
    pushConstants.frame[1] = targets.fullHeight;
    pushConstants.frame[2] = targets.lowWidth;
    pushConstants.frame[3] = targets.lowHeight;
    pushConstants.volume[0] = targets.virtualSliceCount;
    pushConstants.volume[1] = targets.physicalSliceCount;
    const u32 physicalExtinctionWordCount = DivideUp(targets.physicalSliceCount, s_AvboitExtinctionSlicesPerWord);
    pushConstants.volume[2] = static_cast<u32>(
        static_cast<u64>(targets.lowWidth) * static_cast<u64>(targets.lowHeight) * static_cast<u64>(physicalExtinctionWordCount)
    );
    pushConstants.volume[3] = DivideUp(targets.virtualSliceCount, 32u);
    pushConstants.params = Float4(
        alpha,
        s_AvboitExtinctionFixedScale,
        s_AvboitSelfOcclusionSliceBias,
        0.f
    );
    return pushConstants;
}

static TransparentDrawPushConstants BuildTransparentDrawPushConstants(
    const u32 triangleCount,
    const u32 instanceIndex,
    const u32 sourceVertexLayout,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const f32 alpha
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(triangleCount, instanceIndex, sourceVertexLayout, viewportState);
    pushConstants.avboit = BuildAvboitPushConstants(targets, alpha);
    return pushConstants;
}

static u32 DispatchGroupCount1D(const u32 itemCount, const u32 groupSize){
    return DivideUp(itemCount, groupSize);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.pass));
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.depthFormat);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleCount);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        Core::CoreDetail::HashCombine(seed, format);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material && lhs.pass == rhs.pass && lhs.framebufferInfo == rhs.framebufferInfo;
}


RendererSystem::RendererSystem(
    Core::Alloc::CustomArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_geometryMeshes(0, Hasher<Name>(), EqualTo<Name>(), GeometryResourcesMapAllocator(arena))
    , m_materialSurfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), MaterialSurfaceInfoMapAllocator(arena))
    , m_materialPipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), MaterialPipelineMapAllocator(arena))
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), LoggedMaterialPathMapAllocator(arena))
    , m_deformableRuntimeCache(Core::MakeCustomUnique<DeformableRuntimeMeshCache>(arena, arena, graphics, assetManager))
{
    readAccess<Core::Scene::SceneComponent>();
    readAccess<Core::Scene::TransformComponent>();
    readAccess<Core::Scene::CameraComponent>();
    readAccess<RendererComponent>();
    writeAccess<DeformableRendererComponent>();
}
RendererSystem::~RendererSystem()
{}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(delta);
    updateDeformableRuntimeMeshes(world);
}

void RendererSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    DeferredFrameTargets* deferredTargets = nullptr;
    if(!ensureDeferredFrameTargets(framebuffer, deferredTargets))
        return;
    if(!deferredTargets || !deferredTargets->valid())
        return;

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }
    commandList->open();

    clearDeferredTargets(*commandList, *deferredTargets);
    renderMaterialPass(
        *commandList,
        deferredTargets->framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        nullptr,
        nullptr
    );
    commandList->endRenderPass();

    clearAvboitTargets(*commandList, deferredTargets->avboit);
    if(hasTransparentRenderers())
        renderAvboitPasses(*commandList, *deferredTargets);

    commandList->setResourceStatesForBindingSet(deferredTargets->compositeBindingSet.get());
    commandList->commitBarriers();
    if(!renderDeferredComposite(*commandList, *deferredTargets, framebuffer)){
        commandList->close();
        return;
    }

    commandList->close();
    Core::ICommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1);
}

void RendererSystem::backBufferResizing(){
    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

void RendererSystem::backBufferResized(u32 width, u32 height, u32 sampleCount){
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(sampleCount);

    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

RuntimeMeshHandle RendererSystem::deformableRuntimeMeshHandle(const Core::ECS::EntityID entity)const{
    return
        m_deformableRuntimeCache
            ? m_deformableRuntimeCache->handleForEntity(entity)
            : RuntimeMeshHandle{}
    ;
}

u32 RendererSystem::deformableRuntimeMeshEditRevision(const RuntimeMeshHandle handle)const{
    return
        m_deformableRuntimeCache
            ? m_deformableRuntimeCache->editRevision(handle)
            : 0u
    ;
}

bool RendererSystem::bumpDeformableRuntimeMeshRevision(
    const RuntimeMeshHandle handle,
    const RuntimeMeshDirtyFlags dirtyFlags
){
    return m_deformableRuntimeCache && m_deformableRuntimeCache->bumpEditRevision(handle, dirtyFlags);
}

DeformableRuntimeMeshInstance* RendererSystem::findDeformableRuntimeMesh(const RuntimeMeshHandle handle){
    return
        m_deformableRuntimeCache
            ? m_deformableRuntimeCache->findInstance(handle)
            : nullptr
    ;
}

const DeformableRuntimeMeshInstance* RendererSystem::findDeformableRuntimeMesh(const RuntimeMeshHandle handle)const{
    return
        m_deformableRuntimeCache
            ? m_deformableRuntimeCache->findInstance(handle)
            : nullptr
    ;
}

void RendererSystem::updateDeformableRuntimeMeshes(Core::ECS::World& world){
    if(m_deformableRuntimeCache)
        m_deformableRuntimeCache->update(world);
    pruneDeformableGeometryResources();
}

bool RendererSystem::ensureDeferredFrameTargets(Core::IFramebuffer* presentationFramebuffer, DeferredFrameTargets*& outTargets){
    outTargets = nullptr;

    if(!presentationFramebuffer)
        return false;

    const Core::FramebufferInfoEx& presentationInfo = presentationFramebuffer->getFramebufferInfo();
    if(presentationInfo.width == 0 || presentationInfo.height == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: presentation framebuffer has invalid dimensions"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = __hidden_ecs_graphics::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = __hidden_ecs_graphics::SelectGBufferDepthFormat(*device);
    const Core::Format::Enum avboitLowRasterFormat = __hidden_ecs_graphics::SelectAvboitLowRasterFormat(*device);
    const Core::Format::Enum avboitAccumColorFormat = __hidden_ecs_graphics::SelectAvboitAccumColorFormat(*device);
    const Core::Format::Enum avboitAccumExtinctionFormat = __hidden_ecs_graphics::SelectAvboitAccumExtinctionFormat(*device);
    const Core::Format::Enum avboitTransmittanceFormat = __hidden_ecs_graphics::SelectAvboitTransmittanceFormat(*device);
    if(albedoFormat == Core::Format::UNKNOWN || depthFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }
    if(
        avboitLowRasterFormat == Core::Format::UNKNOWN
        || avboitAccumColorFormat == Core::Format::UNKNOWN
        || avboitAccumExtinctionFormat == Core::Format::UNKNOWN
        || avboitTransmittanceFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    if(
        m_deferredTargets.valid()
        && m_deferredTargets.width == presentationInfo.width
        && m_deferredTargets.height == presentationInfo.height
        && m_deferredTargets.albedoFormat == albedoFormat
        && m_deferredTargets.depthFormat == depthFormat
        && m_deferredTargets.avboit.lowRasterFormat == avboitLowRasterFormat
        && m_deferredTargets.avboit.accumColorFormat == avboitAccumColorFormat
        && m_deferredTargets.avboit.accumExtinctionFormat == avboitAccumExtinctionFormat
        && m_deferredTargets.avboit.transmittanceFormat == avboitTransmittanceFormat
    ){
        outTargets = &m_deferredTargets;
        return true;
    }

    if(!ensureDeferredCompositeResources())
        return false;
    if(!ensureAvboitResources())
        return false;

    resetDeferredFrameTargets();
    m_materialPipelines.clear();

    DeferredFrameTargets createdTargets;
    createdTargets.width = presentationInfo.width;
    createdTargets.height = presentationInfo.height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.depthFormat = depthFormat;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_albedo")
        .setClearValue(__hidden_ecs_graphics::s_ClearColor)
    ;
    createdTargets.albedo = m_graphics.createTexture(albedoDesc);
    if(!createdTargets.albedo){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return false;
    }

    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.depthFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/depth")
    ;
    createdTargets.depth = m_graphics.createTexture(depthDesc);
    if(!createdTargets.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return false;
    }

    Core::FramebufferDesc framebufferDesc;
    framebufferDesc
        .addColorAttachment(createdTargets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .setDepthAttachment(createdTargets.depth.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
    ;
    createdTargets.framebuffer = device->createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(__hidden_ecs_graphics::s_AvboitDownsample))
    );
    const u64 lowHeight = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(__hidden_ecs_graphics::s_AvboitDownsample))
    );
    if(lowWidth > Limit<u32>::s_Max || lowHeight > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution dimensions exceed u32 limits"));
        return false;
    }
    avboitTargets.lowWidth = static_cast<u32>(lowWidth);
    avboitTargets.lowHeight = static_cast<u32>(lowHeight);
    avboitTargets.virtualSliceCount = __hidden_ecs_graphics::s_AvboitVirtualSlices;
    avboitTargets.physicalSliceCount = __hidden_ecs_graphics::s_AvboitPhysicalSlices;
    avboitTargets.lowRasterFormat = avboitLowRasterFormat;
    avboitTargets.accumColorFormat = avboitAccumColorFormat;
    avboitTargets.accumExtinctionFormat = avboitAccumExtinctionFormat;
    avboitTargets.transmittanceFormat = avboitTransmittanceFormat;

    Core::TextureDesc lowRasterDesc;
    lowRasterDesc
        .setWidth(avboitTargets.lowWidth)
        .setHeight(avboitTargets.lowHeight)
        .setFormat(avboitTargets.lowRasterFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/low_raster")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.lowRasterTarget = m_graphics.createTexture(lowRasterDesc);
    if(!avboitTargets.lowRasterTarget){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }

    Core::TextureDesc accumColorDesc;
    accumColorDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumColorFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_color")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumColor = m_graphics.createTexture(accumColorDesc);
    if(!avboitTargets.accumColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }

    Core::TextureDesc accumExtinctionDesc;
    accumExtinctionDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumExtinctionFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_extinction")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumExtinction = m_graphics.createTexture(accumExtinctionDesc);
    if(!avboitTargets.accumExtinction){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources);
    avboitTargets.lowFramebuffer = device->createFramebuffer(lowFramebufferDesc);
    if(!avboitTargets.lowFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferDesc accumulationFramebufferDesc;
    accumulationFramebufferDesc
        .addColorAttachment(avboitTargets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .setDepthAttachment(
            Core::FramebufferAttachment()
                .setTexture(createdTargets.depth.get())
                .setSubresources(__hidden_ecs_graphics::s_FramebufferSubresources)
                .setReadOnly(true)
        )
    ;
    avboitTargets.accumulationFramebuffer = device->createFramebuffer(accumulationFramebufferDesc);
    if(!avboitTargets.accumulationFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u32 coverageWordCount = DivideUp(avboitTargets.virtualSliceCount, 32u);
    const u64 coverageBytes = static_cast<u64>(coverageWordCount) * sizeof(u32);
    const u64 depthWarpBytes = static_cast<u64>(avboitTargets.virtualSliceCount) * sizeof(u32);
    const u64 lowPixelCount = static_cast<u64>(avboitTargets.lowWidth) * avboitTargets.lowHeight;
    if(lowPixelCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution pixel count exceeds u32 limits"));
        return false;
    }
    const u32 physicalExtinctionWordCount = DivideUp(
        avboitTargets.physicalSliceCount,
        __hidden_ecs_graphics::s_AvboitExtinctionSlicesPerWord
    );
    if(physicalExtinctionWordCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / physicalExtinctionWordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT packed extinction word count exceeds u32 limits"));
        return false;
    }
    const u64 extinctionWordCount = lowPixelCount * physicalExtinctionWordCount;
    const u64 extinctionBytes = extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = lowPixelCount * sizeof(u32);

    Core::BufferDesc coverageDesc;
    coverageDesc
        .setByteSize(coverageBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_coverage")
    ;
    avboitTargets.coverageBuffer = m_graphics.createBuffer(coverageDesc);
    if(!avboitTargets.coverageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }

    Core::BufferDesc depthWarpDesc;
    depthWarpDesc
        .setByteSize(depthWarpBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_warp_lut")
    ;
    avboitTargets.depthWarpBuffer = m_graphics.createBuffer(depthWarpDesc);
    if(!avboitTargets.depthWarpBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }

    Core::BufferDesc controlDesc;
    controlDesc
        .setByteSize(static_cast<u64>(__hidden_ecs_graphics::s_AvboitControlWordCount) * sizeof(u32))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/control")
    ;
    avboitTargets.controlBuffer = m_graphics.createBuffer(controlDesc);
    if(!avboitTargets.controlBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }

    Core::BufferDesc extinctionDesc;
    extinctionDesc
        .setByteSize(extinctionBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/packed_extinction_volume")
    ;
    avboitTargets.extinctionBuffer = m_graphics.createBuffer(extinctionDesc);
    if(!avboitTargets.extinctionBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }

    Core::BufferDesc extinctionOverflowDesc;
    extinctionOverflowDesc
        .setByteSize(extinctionOverflowBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/extinction_overflow_depth")
    ;
    avboitTargets.extinctionOverflowBuffer = m_graphics.createBuffer(extinctionOverflowDesc);
    if(!avboitTargets.extinctionOverflowBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }

    Core::TextureDesc transmittanceDesc;
    transmittanceDesc
        .setWidth(avboitTargets.lowWidth)
        .setHeight(avboitTargets.lowHeight)
        .setDepth(avboitTargets.physicalSliceCount)
        .setFormat(avboitTargets.transmittanceFormat)
        .setDimension(Core::TextureDimension::Texture3D)
        .setInUAV(true)
        .setName("engine/avboit/transmittance_volume")
        .setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f))
    ;
    avboitTargets.transmittanceTexture = m_graphics.createTexture(transmittanceDesc);
    if(!avboitTargets.transmittanceTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }

    Core::BindingSetDesc occupancyBindingSetDesc;
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.coverageBuffer.get()));
    avboitTargets.occupancyBindingSet = device->createBindingSet(occupancyBindingSetDesc, m_avboitOccupancyBindingLayout);
    if(!avboitTargets.occupancyBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding set"));
        return false;
    }

    Core::BindingSetDesc depthWarpBindingSetDesc;
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.coverageBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, avboitTargets.depthWarpBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.controlBuffer.get()));
    avboitTargets.depthWarpBindingSet = device->createBindingSet(depthWarpBindingSetDesc, m_avboitDepthWarpBindingLayout);
    if(!avboitTargets.depthWarpBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding set"));
        return false;
    }

    Core::BindingSetDesc extinctionBindingSetDesc;
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.depthWarpBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, avboitTargets.controlBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(4, avboitTargets.extinctionBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(5, avboitTargets.extinctionOverflowBuffer.get()));
    avboitTargets.extinctionBindingSet = device->createBindingSet(extinctionBindingSetDesc, m_avboitExtinctionBindingLayout);
    if(!avboitTargets.extinctionBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding set"));
        return false;
    }

    Core::BindingSetDesc integrateBindingSetDesc;
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.extinctionBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        1,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, avboitTargets.extinctionOverflowBuffer.get()));
    avboitTargets.integrateBindingSet = device->createBindingSet(integrateBindingSetDesc, m_avboitIntegrateBindingLayout);
    if(!avboitTargets.integrateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding set"));
        return false;
    }

    Core::BindingSetDesc accumulateBindingSetDesc;
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.depthWarpBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_avboitLinearSampler.get()));
    avboitTargets.accumulateBindingSet = device->createBindingSet(accumulateBindingSetDesc, m_avboitAccumulateBindingLayout);
    if(!avboitTargets.accumulateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding set"));
        return false;
    }

    createdTargets.avboit = Move(avboitTargets);

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        createdTargets.avboit.accumColor.get(),
        createdTargets.avboit.accumColorFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        2,
        createdTargets.avboit.accumExtinction.get(),
        createdTargets.avboit.accumExtinctionFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_deferredSampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, m_deferredCompositeBindingLayout);
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    m_deferredTargets = Move(createdTargets);
    outTargets = &m_deferredTargets;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, depth {}, AVBOIT color {}, extinction {}, transmittance {})")
        , m_deferredTargets.width
        , m_deferredTargets.height
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.depthFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.transmittanceFormat).name)
    );
    return true;
}

bool RendererSystem::ensureDeferredCompositeResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_deferredCompositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));

        m_deferredCompositeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredCompositeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding layout"));
            return false;
        }
    }

    if(!__hidden_ecs_graphics::EnsurePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create deferred composite sampler")))
        return false;

    if(!ensureShaderLoaded(
        m_deferredCompositeVertexShader,
        __hidden_ecs_graphics::DeferredCompositeVertexShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Vertex,
        "ECSGraphics_DeferredCompositeVS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_deferredCompositePixelShader,
        __hidden_ecs_graphics::DeferredCompositePixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_DeferredCompositePS"
    ))
        return false;

    return true;
}

bool RendererSystem::ensureDeferredCompositePipeline(Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!ensureDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(m_deferredCompositePipeline && m_deferredCompositePipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_deferredCompositeVertexShader)
        .setPixelShader(m_deferredCompositePixelShader)
        .setRenderState(__hidden_ecs_graphics::BuildCompositeRenderState())
        .addBindingLayout(m_deferredCompositeBindingLayout)
    ;

    Core::IDevice* device = m_graphics.getDevice();
    m_deferredCompositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredCompositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

bool RendererSystem::ensureAvboitResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!__hidden_ecs_graphics::EnsurePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT")))
        return false;
    if(!__hidden_ecs_graphics::EnsureLinearClampSampler(*device, m_avboitLinearSampler, NWB_TEXT("RendererSystem: failed to create linear sampler for AVBOIT")))
        return false;

    if(!m_avboitEmptyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);

        m_avboitEmptyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitEmptyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"));
            return false;
        }
    }

    if(!m_avboitOccupancyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitOccupancyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitOccupancyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding layout"));
            return false;
        }
    }

    if(!m_avboitDepthWarpBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::AvboitPushConstants)));

        m_avboitDepthWarpBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitDepthWarpBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding layout"));
            return false;
        }
    }

    if(!m_avboitExtinctionBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitExtinctionBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitExtinctionBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding layout"));
            return false;
        }
    }

    if(!m_avboitIntegrateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::AvboitPushConstants)));

        m_avboitIntegrateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitIntegrateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding layout"));
            return false;
        }
    }

    if(!m_avboitAccumulateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitAccumulateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitAccumulateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"));
            return false;
        }
    }

    if(!ensureShaderLoaded(
        m_avboitOccupancyPixelShader,
        __hidden_ecs_graphics::AvboitOccupancyPixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitOccupancyPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitDepthWarpComputeShader,
        __hidden_ecs_graphics::AvboitDepthWarpComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSGraphics_AvboitDepthWarpCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitExtinctionPixelShader,
        __hidden_ecs_graphics::AvboitExtinctionPixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitExtinctionPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitIntegrateComputeShader,
        __hidden_ecs_graphics::AvboitIntegrateComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSGraphics_AvboitIntegrateCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitAccumulatePixelShader,
        __hidden_ecs_graphics::AvboitAccumulatePixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitAccumulatePS"
    ))
        return false;

    return true;
}

bool RendererSystem::ensureAvboitPipelines(AvboitFrameTargets& targets){
    if(!ensureAvboitResources())
        return false;

    Core::IDevice* device = m_graphics.getDevice();

    if(!m_avboitDepthWarpPipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitDepthWarpComputeShader)
            .addBindingLayout(m_avboitDepthWarpBindingLayout)
        ;
        m_avboitDepthWarpPipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitDepthWarpPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
            return false;
        }
    }

    if(!m_avboitIntegratePipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitIntegrateComputeShader)
            .addBindingLayout(m_avboitIntegrateBindingLayout)
        ;
        m_avboitIntegratePipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitIntegratePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
            return false;
        }
    }

    return targets.valid();
}

void RendererSystem::clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo){
        commandList.setTextureState(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.depth){
        commandList.setTextureState(targets.depth.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.albedo){
        commandList.clearTextureFloat(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, __hidden_ecs_graphics::s_ClearColor);
    }

    if(targets.depth){
        commandList.clearDepthStencilTexture(
            targets.depth.get(),
            __hidden_ecs_graphics::s_FramebufferSubresources,
            true,
            Core::s_DepthClearValue,
            false,
            0
        );
    }
}

void RendererSystem::clearAvboitTargets(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(targets.lowRasterTarget){
        commandList.setTextureState(targets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumColor){
        commandList.setTextureState(targets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumExtinction){
        commandList.setTextureState(targets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.coverageBuffer){
        commandList.setBufferState(targets.coverageBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.depthWarpBuffer){
        commandList.setBufferState(targets.depthWarpBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.controlBuffer){
        commandList.setBufferState(targets.controlBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.extinctionBuffer){
        commandList.setBufferState(targets.extinctionBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.extinctionOverflowBuffer){
        commandList.setBufferState(targets.extinctionOverflowBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.transmittanceTexture){
        commandList.setTextureState(targets.transmittanceTexture.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.lowRasterTarget){
        commandList.clearTextureFloat(targets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumColor){
        commandList.clearTextureFloat(targets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumExtinction){
        commandList.clearTextureFloat(targets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.coverageBuffer){
        commandList.clearBufferUInt(targets.coverageBuffer.get(), 0u);
    }

    if(targets.depthWarpBuffer){
        commandList.clearBufferUInt(targets.depthWarpBuffer.get(), 0u);
    }

    if(targets.controlBuffer){
        commandList.clearBufferUInt(targets.controlBuffer.get(), 0u);
    }

    if(targets.extinctionBuffer){
        commandList.clearBufferUInt(targets.extinctionBuffer.get(), 0u);
    }

    if(targets.extinctionOverflowBuffer){
        commandList.clearBufferUInt(targets.extinctionOverflowBuffer.get(), Limit<u32>::s_Max);
    }

    if(targets.transmittanceTexture){
        commandList.clearTextureFloat(targets.transmittanceTexture.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(1.f, 1.f, 1.f, 1.f));
    }
}

void RendererSystem::renderMaterialPass(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return;
    if(pass != MaterialPipelinePass::Opaque && (!passBindingSet || !avboitTargets || !avboitTargets->valid()))
        return;

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector meshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector computeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    InstanceGpuDataVector instanceData{Core::Alloc::ScratchAllocator<InstanceGpuData>(scratchArena)};
    MaterialParameterGpuDataVector materialParameters{Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    const f32 meshViewAspectRatio = avboitTargets
        ? __hidden_ecs_graphics::ExtentAspectRatio(avboitTargets->fullWidth, avboitTargets->fullHeight)
        : __hidden_ecs_graphics::FramebufferAspectRatio(*framebuffer)
    ;
    if(!ensureMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

    gatherMaterialPassDrawItems(framebuffer, pass, transparent, meshDrawItems, computeDrawItems, instanceData, materialParameters);
    if(meshDrawItems.empty() && computeDrawItems.empty())
        return;

    if(!uploadInstanceBuffer(commandList, instanceData))
        return;
    if(!uploadMaterialParameterBuffer(commandList, materialParameters))
        return;

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    renderMeshMaterialPassDrawItems(drawContext, meshDrawItems);
    renderComputeMaterialPassDrawItems(drawContext, computeDrawItems);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    MaterialPassDrawItemVector& meshDrawItems,
    MaterialPassDrawItemVector& computeDrawItems,
    InstanceGpuDataVector& instanceData,
    MaterialParameterGpuDataVector& materialParameters
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto deformableRendererView = m_world.view<DeformableRendererComponent>();
    const usize rendererCapacity = rendererView.candidateCount() + deformableRendererView.candidateCount();
    meshDrawItems.reserve(rendererCapacity);
    computeDrawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
    materialParameters.reserve(rendererCapacity);

    using MaterialParameterBlockPair = Pair<Name, __hidden_ecs_graphics::MaterialParameterBlock>;
    using MaterialParameterBlockMap = HashMap<
        Name,
        __hidden_ecs_graphics::MaterialParameterBlock,
        Hasher<Name>,
        EqualTo<Name>,
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>
    >;
    MaterialParameterBlockMap materialParameterBlocks(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>(materialParameters.get_allocator())
    );
    materialParameterBlocks.reserve(rendererCapacity);

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();

    auto ensureMaterialParameterBlock = [&](
        const MaterialSurfaceInfo& materialInfo,
        __hidden_ecs_graphics::MaterialParameterBlock& outBlock
    ) -> bool{
        const auto foundBlock = materialParameterBlocks.find(materialInfo.materialName);
        if(foundBlock != materialParameterBlocks.end()){
            outBlock = foundBlock.value();
            return true;
        }

        if(materialParameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter offset exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter count exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max) - materialParameters.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material parameter count exceeds u32 limits"));
            return false;
        }

        outBlock.offset = static_cast<u32>(materialParameters.size());
        outBlock.count = static_cast<u32>(materialInfo.parameters.size());
        const usize requiredParameterCapacity = materialParameters.size() + materialInfo.parameters.size();
        if(requiredParameterCapacity > materialParameters.capacity())
            materialParameters.reserve(__hidden_ecs_graphics::NextGrowingCapacity(
                materialParameters.capacity(),
                requiredParameterCapacity
            ));
        AppendTriviallyCopyableVector(materialParameters, materialInfo.parameters);

        materialParameterBlocks.emplace(materialInfo.materialName, outBlock);
        return true;
    };

    auto appendDrawForGeometry = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        GeometryResources& geometry
    ) -> bool{
        if(!geometry.valid())
            return false;

        const Core::Scene::TransformComponent* transform =
            m_world.tryGetComponent<Core::Scene::TransformComponent>(entity)
        ;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(material, materialInfo))
            return false;
        if(!materialInfo || !materialInfo->valid || materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!ensureRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
        if(!pipelineResources)
            return false;

        auto appendInstance = [&]() -> u32{
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }

            __hidden_ecs_graphics::MaterialParameterBlock parameterBlock;
            if(!ensureMaterialParameterBlock(*materialInfo, parameterBlock))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(__hidden_ecs_graphics::BuildInstanceGpuData(
                transform,
                parameterBlock.offset,
                parameterBlock.count
            ));
            return instanceIndex;
        };

        auto appendDrawItem = [&](MaterialPassDrawItemVector& drawItems) -> bool{
            const u32 instanceIndex = appendInstance();
            if(instanceIndex == Limit<u32>::s_Max)
                return false;

            MaterialPassDrawItem drawItem;
            drawItem.geometryKey = geometry.geometryName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItem.alpha = materialInfo->alpha;
            drawItems.push_back(drawItem);
            return true;
        };

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            if(!pipelineResources->meshletPipeline)
                return false;
            return appendDrawItem(meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                return false;
            return appendDrawItem(computeDrawItems);
        }
        default:
            return false;
        }
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        GeometryResources* geometry = nullptr;
        if(!ensureGeometryLoaded(renderer.geometry, geometry))
            continue;
        if(geometry)
            appendDrawForGeometry(entity, renderer.material, *geometry);
    }

    for(auto&& [entity, renderer] : deformableRendererView){
        if(!renderer.visible || !renderer.runtimeMesh.valid())
            continue;

        DeformableRuntimeMeshInstance* instance = findDeformableRuntimeMesh(renderer.runtimeMesh);
        if(!instance || !instance->valid())
            continue;

        GeometryResources* geometry = nullptr;
        if(!ensureDeformableGeometryResources(*instance, geometry))
            continue;
        if(geometry)
            appendDrawForGeometry(entity, renderer.material, *geometry);
    }
}

bool RendererSystem::ensureInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    if(instanceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer request exceeds u32 instance-index limits"));
        return false;
    }
    if(m_instanceBuffer && m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = __hidden_ecs_graphics::NextGrowingCapacity(m_instanceBufferCapacity, instanceCount);
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(__hidden_ecs_graphics::InstanceBufferName())
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    m_instanceBuffer = Move(instanceBuffer);
    m_instanceBufferCapacity = capacity;
    invalidateGeometryBindingSets();
    return true;
}

bool RendererSystem::ensureMaterialParameterBufferCapacity(const usize parameterCount){
    const usize requiredCount = Max<usize>(parameterCount, 1u);
    if(requiredCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer request exceeds u32 limits"));
        return false;
    }
    if(m_materialParameterBuffer && m_materialParameterBufferCapacity >= requiredCount)
        return true;

    const usize capacity = __hidden_ecs_graphics::NextGrowingCapacity(m_materialParameterBufferCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc materialParameterBufferDesc;
    materialParameterBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(MaterialParameterGpuData)))
        .setStructStride(sizeof(MaterialParameterGpuData))
        .setDebugName(__hidden_ecs_graphics::MaterialParameterBufferName())
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialParameterBuffer = m_graphics.createBuffer(materialParameterBufferDesc);
    if(!materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material parameter buffer"));
        return false;
    }

    m_materialParameterBuffer = Move(materialParameterBuffer);
    m_materialParameterBufferCapacity = capacity;
    invalidateGeometryBindingSets();
    return true;
}

bool RendererSystem::ensureMeshViewBuffer(Core::ICommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_meshViewBuffer){
        Core::BufferDesc meshViewBufferDesc;
        meshViewBufferDesc
            .setByteSize(sizeof(__hidden_ecs_graphics::MeshViewGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(__hidden_ecs_graphics::MeshViewBufferName())
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle meshViewBuffer = m_graphics.createBuffer(meshViewBufferDesc);
        if(!meshViewBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
            return false;
        }

        m_meshViewBuffer = Move(meshViewBuffer);
        invalidateGeometryBindingSets();
    }

    const __hidden_ecs_graphics::MeshViewState viewState =
        __hidden_ecs_graphics::ResolveMeshViewState(m_world, fallbackAspectRatio)
    ;

    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadInstanceBuffer(Core::ICommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    if(!ensureInstanceBufferCapacity(instanceData.size()))
        return false;
    if(!m_instanceBuffer)
        return false;

    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance data upload size overflows"));
        return false;
    }

    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_instanceBuffer.get(), instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadMaterialParameterBuffer(Core::ICommandList& commandList, const MaterialParameterGpuDataVector& materialParameters){
    const usize uploadCount = Max<usize>(materialParameters.size(), 1u);
    if(!ensureMaterialParameterBufferCapacity(uploadCount))
        return false;
    if(!m_materialParameterBuffer)
        return false;

    if(uploadCount > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter data upload size overflows"));
        return false;
    }

    MaterialParameterGpuData fallbackParameter;
    const MaterialParameterGpuData* data = materialParameters.empty() ? &fallbackParameter : materialParameters.data();
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_materialParameterBuffer.get(), data, uploadCount * sizeof(MaterialParameterGpuData));
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

void RendererSystem::invalidateGeometryBindingSets(){
    m_emulationViewBindingSet = nullptr;
    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end(); ++it){
        GeometryResources& geometry = it.value();
        geometry.meshBindingSet = nullptr;
        geometry.computeBindingSet = nullptr;
    }
}

bool RendererSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    GeometryResources*& outGeometry,
    MaterialPipelineResources*& outPipelineResources
){
    outGeometry = nullptr;
    outPipelineResources = nullptr;

    const auto foundGeometry = m_geometryMeshes.find(drawItem.geometryKey);
    if(foundGeometry == m_geometryMeshes.end())
        return false;

    const auto foundPipeline = m_materialPipelines.find(drawItem.pipelineKey);
    if(foundPipeline == m_materialPipelines.end())
        return false;

    outGeometry = &foundGeometry.value();
    outPipelineResources = &foundPipeline.value();
    return true;
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.meshletPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
            return;
        if(!ensureMeshBindingSet(geometry))
            return;

        context.commandList.setBufferState(geometry.shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(geometry.shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
        context.commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(geometry.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);

        context.commandList.setMeshletState(meshletState);

        if(context.pass == MaterialPipelinePass::Opaque){
            const __hidden_ecs_graphics::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_graphics::BuildShaderDrivenPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        else{
            const __hidden_ecs_graphics::TransparentDrawPushConstants pushConstants =
                __hidden_ecs_graphics::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        context.commandList.dispatchMesh(geometry.dispatchGroupCount);
    });
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    if(!m_meshViewBuffer || !ensureEmulationViewResources() || !m_emulationViewBindingSet)
        return;

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.computePipeline || !pipelineResources.emulationPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
            return;
        if(!ensureComputeBindingSet(geometry))
            return;
        if(!geometry.computeBindingSet || !geometry.emulationVertexBuffer)
            return;

        context.commandList.setBufferState(geometry.shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(geometry.shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
        context.commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(geometry.computeBindingSet.get());

        context.commandList.setComputeState(computeState);

        const __hidden_ecs_graphics::ShaderDrivenPushConstants pushConstants =
            __hidden_ecs_graphics::BuildShaderDrivenPushConstants(
                geometry.triangleCount,
                drawItem.instanceIndex,
                geometry.sourceVertexLayout,
                context.viewportState
            );
        context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        context.commandList.dispatch(geometry.dispatchGroupCount);

        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

        Core::GraphicsState graphicsState;
        graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
        graphicsState.setFramebuffer(context.framebuffer);
        graphicsState.setViewport(context.viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(geometry.emulationVertexBuffer.get())
                .setSlot(0)
                .setOffset(0)
        );
        graphicsState.addBindingSet(m_emulationViewBindingSet.get());
        if(context.passBindingSet)
            graphicsState.addBindingSet(context.passBindingSet);

        context.commandList.setGraphicsState(graphicsState);

        if(context.pass != MaterialPipelinePass::Opaque){
            const __hidden_ecs_graphics::TransparentDrawPushConstants transparentPushConstants =
                __hidden_ecs_graphics::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&transparentPushConstants, sizeof(transparentPushConstants));
        }

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(geometry.indexCount);
        context.commandList.draw(drawArgs);
    });
}

void RendererSystem::renderAvboitPasses(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return;
    if(!ensureAvboitPipelines(avboitTargets))
        return;

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitOccupancy,
        true,
        avboitTargets.occupancyBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitDepthWarp(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitExtinction,
        true,
        avboitTargets.extinctionBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitIntegration(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.accumulationFramebuffer.get(),
        MaterialPipelinePass::AvboitAccumulate,
        true,
        avboitTargets.accumulateBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();
}

void RendererSystem::dispatchAvboitDepthWarp(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitDepthWarpPipeline || !targets.depthWarpBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.depthWarpBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitDepthWarpPipeline.get());
    computeState.addBindingSet(targets.depthWarpBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_ecs_graphics::AvboitPushConstants pushConstants =
        __hidden_ecs_graphics::BuildAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(1, 1, 1);
}

void RendererSystem::dispatchAvboitIntegration(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitIntegratePipeline || !targets.integrateBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.integrateBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitIntegratePipeline.get());
    computeState.addBindingSet(targets.integrateBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_ecs_graphics::AvboitPushConstants pushConstants =
        __hidden_ecs_graphics::BuildAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    commandList.dispatch(__hidden_ecs_graphics::DispatchGroupCount1D(pixelCount, 64u), 1, 1);
}

bool RendererSystem::renderDeferredComposite(Core::ICommandList& commandList, DeferredFrameTargets& targets, Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;
    if(!targets.compositeBindingSet)
        return false;
    if(!ensureDeferredCompositePipeline(presentationFramebuffer))
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredCompositePipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.compositeBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    return true;
}


bool RendererSystem::ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    const Name geometryPath = geometryAsset.name();
    if(!geometryPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer geometry is empty"));
        return false;
    }

    const auto foundGeometry = m_geometryMeshes.find(geometryPath);
    if(foundGeometry != m_geometryMeshes.end()){
        outGeometry = &foundGeometry.value();
        return outGeometry->valid();
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Geometry::AssetTypeName(), geometryPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Geometry::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not geometry"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(*loadedAsset);

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryPath;
    createdGeometry.sourceVertexLayout = __hidden_ecs_graphics::s_MeshSourceLayoutGeometryVertex;

    const usize indexCount = geometry.indices().size();
    if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count exceeds u32 limits"), StringConvert(geometryPath.c_str()));
        return false;
    }

    createdGeometry.indexCount = static_cast<u32>(indexCount);
    if(createdGeometry.indexCount == 0 || (createdGeometry.indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count {} is incompatible with triangle-based mesh rendering")
            , StringConvert(geometryPath.c_str())
            , createdGeometry.indexCount
        );
        return false;
    }

    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = __hidden_ecs_graphics::ComputeDispatchGroupCount(createdGeometry.triangleCount);
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' produced no dispatch groups"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Name shaderVertexBufferName = DeriveName(geometryPath, AStringView(":shader_vb"));
    const Name shaderIndexBufferName = DeriveName(geometryPath, AStringView(":shader_ib"));
    if(!shaderVertexBufferName || !shaderIndexBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shader-driven buffer names for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    Core::Graphics::BufferSetupDesc shaderVertexSetup;
    shaderVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(geometry.vertices().size() * sizeof(GeometryVertex)))
        .setStructStride(__hidden_ecs_graphics::s_StaticGeometryVertexStride)
        .setDebugName(shaderVertexBufferName)
    ;
    shaderVertexSetup.data = geometry.vertices().data();
    shaderVertexSetup.dataSize = geometry.vertices().size() * sizeof(GeometryVertex);
    createdGeometry.shaderVertexBuffer = m_graphics.setupBuffer(shaderVertexSetup);
    if(!createdGeometry.shaderVertexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader vertex buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const usize expandedIndexCount = static_cast<usize>(createdGeometry.indexCount);
    if(expandedIndexCount > Limit<usize>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' expanded index buffer size overflows"), StringConvert(geometryPath.c_str()));
        return false;
    }
    const usize expandedIndexBytes = expandedIndexCount * sizeof(u32);

    Core::Graphics::BufferSetupDesc shaderIndexSetup;
    shaderIndexSetup.bufferDesc
        .setByteSize(static_cast<u64>(expandedIndexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderIndexBufferName)
    ;
    shaderIndexSetup.data = geometry.indices().data();
    shaderIndexSetup.dataSize = expandedIndexBytes;
    createdGeometry.shaderIndexBuffer = m_graphics.setupBuffer(shaderIndexSetup);
    if(!createdGeometry.shaderIndexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader index buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(geometryPath, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

bool RendererSystem::ensureDeformableGeometryResources(const DeformableRuntimeMeshInstance& instance, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    if(!instance.valid())
        return false;
    if(
        instance.restVertices.size() > static_cast<usize>(Limit<u32>::s_Max)
        || instance.indices.size() > static_cast<usize>(Limit<u32>::s_Max)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deformable runtime mesh '{}' exceeds renderer u32 limits"), instance.handle.value);
        return false;
    }
    if((instance.indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deformable runtime mesh '{}' has non-triangle index count {}")
            , instance.handle.value
            , instance.indices.size()
        );
        return false;
    }

    const Name geometryKey = DeriveRuntimeResourceName(instance.source.name(), instance.handle.value, instance.editRevision, "deformed_draw");
    if(!geometryKey){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive draw resource key for deformable runtime mesh '{}'"), instance.handle.value);
        return false;
    }

    const auto foundGeometry = m_geometryMeshes.find(geometryKey);
    if(foundGeometry != m_geometryMeshes.end()){
        outGeometry = &foundGeometry.value();
        return outGeometry->valid();
    }

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryKey;
    createdGeometry.shaderVertexBuffer = instance.deformedVertexBuffer;
    createdGeometry.shaderIndexBuffer = instance.indexBuffer;
    createdGeometry.indexCount = static_cast<u32>(instance.indices.size());
    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = __hidden_ecs_graphics::ComputeDispatchGroupCount(createdGeometry.triangleCount);
    createdGeometry.sourceVertexLayout = __hidden_ecs_graphics::s_MeshSourceLayoutDeformableVertex;
    createdGeometry.runtimeMesh = instance.handle;
    createdGeometry.runtimeEditRevision = instance.editRevision;
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: deformable runtime mesh '{}' produced no dispatch groups"), instance.handle.value);
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(geometryKey, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

void RendererSystem::pruneDeformableGeometryResources(){
    if(!m_deformableRuntimeCache || m_geometryMeshes.empty())
        return;

    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end();){
        const GeometryResources& geometry = it.value();
        if(!geometry.runtimeMesh.valid()){
            ++it;
            continue;
        }

        const DeformableRuntimeMeshInstance* instance = m_deformableRuntimeCache->findInstance(geometry.runtimeMesh);
        if(!instance || instance->editRevision != geometry.runtimeEditRevision){
            it = m_geometryMeshes.erase(it);
            continue;
        }

        ++it;
    }
}

bool RendererSystem::ensureMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialSurfaceInfos.find(materialPath);
    if(foundInfo != m_materialSurfaceInfos.end()){
        outInfo = &foundInfo.value();
        return outInfo->valid;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Material::AssetTypeName(), materialPath, loadedAsset)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load material '{}'"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not a material"), StringConvert(materialPath.c_str()));
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    createdInfo.shaderVariant = material.shaderVariant().empty()
        ? AString(Core::ShaderArchive::s_DefaultVariant)
        : material.shaderVariant()
    ;
    createdInfo.valid = true;

    __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Pixel, createdInfo.pixelShader);
    __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Mesh, createdInfo.meshShader);

    CompactString alphaText;
    u32 alphaPriority = Limit<u32>::s_Max;
    CompactString modeText;
    u32 modePriority = Limit<u32>::s_Max;

    createdInfo.parameters.reserve(material.parameters().size());
    for(const auto& [key, value] : material.parameters()){
        const u32 candidateAlphaPriority = __hidden_ecs_graphics::MaterialAlphaParameterPriority(key);
        if(candidateAlphaPriority < alphaPriority){
            alphaText = value;
            alphaPriority = candidateAlphaPriority;
        }

        const u32 candidateModePriority = __hidden_ecs_graphics::MaterialModeParameterPriority(key);
        if(candidateModePriority < modePriority){
            modeText = value;
            modePriority = candidateModePriority;
        }

        MaterialParameterGpuData parameter;
        if(__hidden_ecs_graphics::TryBuildMaterialParameterGpuData(key, value, parameter))
            createdInfo.parameters.push_back(parameter);
    }

    if(alphaPriority != Limit<u32>::s_Max){
        f32 parsedAlpha = 1.f;
        if(__hidden_ecs_graphics::ParseAlphaValue(alphaText.view(), parsedAlpha))
            createdInfo.alpha = parsedAlpha;
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: material '{}' has invalid alpha '{}'; using 1.0")
                , StringConvert(materialPath.c_str())
                , StringConvert(alphaText.c_str())
            );
        }
    }

    if(modePriority != Limit<u32>::s_Max){
        createdInfo.transparent = __hidden_ecs_graphics::IsTransparentText(modeText.view());
    }
    if(createdInfo.alpha < 0.999f)
        createdInfo.transparent = true;

    auto result = m_materialSurfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    outInfo = &it.value();
    return outInfo->valid;
}

bool RendererSystem::ensureMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

    Core::IDevice* device = m_graphics.getDevice();
    m_meshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
    if(!m_meshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding layout"));
        return false;
    }

    return true;
}

bool RendererSystem::ensureComputeEmulationResources(){
    if(!m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::ShaderDrivenPushConstants)));

        Core::IDevice* device = m_graphics.getDevice();
        m_computeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_computeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
            return false;
        }
    }

    if(!m_emulationVertexShader){
        if(!ensureShaderLoaded(
            m_emulationVertexShader,
            __hidden_ecs_graphics::MeshEmulationVertexShaderName(),
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSGraphics_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[6];
        attributes[0]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(0)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("POSITION")
        ;
        attributes[1]
            .setFormat(Core::Format::RGB32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 4u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("NORMAL")
        ;
        attributes[2]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 8u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("TANGENT")
        ;
        attributes[3]
            .setFormat(Core::Format::RG32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 12u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("TEXCOORD")
        ;
        attributes[4]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 16u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("COLOR")
        ;
        attributes[5]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 20u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("POSITION1")
        ;

        Core::IDevice* device = m_graphics.getDevice();
        m_emulationInputLayout = device->createInputLayout(attributes, 6, m_emulationVertexShader.get());
        if(!m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::ensureEmulationViewResources(){
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: emulation view resources require a mesh view buffer"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    if(!m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));

        m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(m_emulationViewBindingSet)
        return true;

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));

    m_emulationViewBindingSet = device->createBindingSet(bindingSetDesc, m_emulationViewBindingLayout);
    if(!m_emulationViewBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding set"));
        return false;
    }

    return true;
}

bool RendererSystem::ensureMeshBindingSet(GeometryResources& geometry){
    if(geometry.meshBindingSet)
        return true;
    if(!ensureMeshShaderResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a material parameter buffer"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, m_materialParameterBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.meshBindingSet = device->createBindingSet(bindingSetDesc, m_meshBindingLayout);
    if(!geometry.meshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding set for geometry '{}'"), StringConvert(geometry.geometryName.c_str()));
        return false;
    }

    return true;
}

bool RendererSystem::ensureComputeBindingSet(GeometryResources& geometry){
    if(geometry.computeBindingSet)
        return true;
    if(!ensureComputeEmulationResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a material parameter buffer"));
        return false;
    }

    if(!geometry.emulationVertexBuffer){
        const Name emulationVertexBufferName = DeriveName(geometry.geometryName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(static_cast<u64>(geometry.indexCount) * __hidden_ecs_graphics::s_EmulatedVertexStride)
            .setStructStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setDebugName(emulationVertexBufferName)
        ;
        geometry.emulationVertexBuffer = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(!geometry.emulationVertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, geometry.emulationVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, m_materialParameterBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout);
    if(!geometry.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for geometry '{}'")
            , StringConvert(geometry.geometryName.c_str())
        );
        return false;
    }

    return true;
}


bool RendererSystem::ensureRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::IFramebuffer* framebuffer,
    MaterialPipelineResources*& outResources
){
    outResources = nullptr;

    if(!framebuffer)
        return false;

    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    if(!materialInfo.valid || !materialKey){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    auto [it, inserted] = m_materialPipelines.try_emplace(pipelineKey);
    MaterialPipelineResources& resources = it.value();
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(resources.meshletPipeline){
            outResources = &resources;
            return true;
        }
        break;
    case RenderPath::ComputeEmulation:
        if(resources.computePipeline && resources.emulationPipeline){
            outResources = &resources;
            return true;
        }
        break;
    default:
        break;
    }

    auto removeFailedEntry = [&](){
        if(inserted)
            m_materialPipelines.erase(it);
    };
    auto failMaterialPipeline = [&](){
        removeFailedEntry();
        return false;
    };

    const AStringView shaderVariant = materialInfo.shaderVariant.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : AStringView(materialInfo.shaderVariant)
    ;

    const bool hasPixelShader = materialInfo.pixelShader.valid();
    const bool hasMeshShader = materialInfo.meshShader.valid();
    Core::ShaderHandle passPixelShader;

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::AvboitOccupancy:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitOccupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitExtinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitAccumulatePixelShader;
        break;
    default:
        break;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::RenderState renderState = __hidden_ecs_graphics::BuildRenderStateForPass(pass);

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!ensureMeshShaderResources())
            return false;
        if(!ensureShaderLoaded(resources.meshShader, materialInfo.meshShader.name(), shaderVariant, Core::ShaderType::Mesh, "ECSGraphics_RendererMesh"))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_meshBindingLayout);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            pipelineDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            pipelineDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            pipelineDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }

        resources.meshletPipeline = device->createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!ensureComputeEmulationResources())
            return false;
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::MeshComputeArchiveStageName();
        if(!ensureShaderLoaded(
            resources.computeShader,
            materialInfo.meshShader.name(),
            shaderVariant,
            Core::ShaderType::Compute,
            "ECSGraphics_RendererCS",
            &meshComputeArchiveStageName
        ))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }
        if(!ensureEmulationViewResources())
            return false;

        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader);
        computeDesc.addBindingLayout(m_computeBindingLayout);
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_emulationInputLayout);
        emulationDesc.setVertexShader(m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        emulationDesc.addBindingLayout(m_emulationViewBindingLayout);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }
        resources.emulationPipeline = device->createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        return true;
    };

    const bool meshSupported = device->queryFeatureSupport(Core::Feature::Meshlets);
    if(pass == MaterialPipelinePass::Opaque && !hasPixelShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a pixel shader"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }

    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a mesh shader; compute emulation is derived internally from that mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    if(meshSupported && hasMeshShader){
        if(!tryBuildMeshPipeline()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device")
                , StringConvert(materialKey.c_str())
            );
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        outResources = &resources;
        return true;
    }

    if(!tryBuildComputePipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    outResources = &resources;
    return true;
}

bool RendererSystem::hasTransparentRenderers(){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> bool{
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(material, materialInfo))
            return false;
        return materialInfo && materialInfo->valid && materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }

    auto deformableRendererView = m_world.view<DeformableRendererComponent>();
    for(auto&& [entity, renderer] : deformableRendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }

    return false;
}

void RendererSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const bool meshSupported){
    auto [it, inserted] = m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
    if(!inserted){
        if(it.value() == renderPath)
            return;
        it.value() = renderPath;
    }

    switch(renderPath){
    case RenderPath::MeshShader:{
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: material '{}' selected MeshShader + PS on this device"),
            StringConvert(materialKey.c_str())
        );
        break;
    }
    case RenderPath::ComputeEmulation:{
        if(!meshSupported){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because this device does not support mesh shaders"),
                StringConvert(materialKey.c_str())
            );
        }
        else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS through compute emulation"),
                StringConvert(materialKey.c_str())
            );
        }
        break;
    }
    default:{
        break;
    }
    }
}

bool RendererSystem::ensureShaderLoaded(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const AStringView variantName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName,
    const Name* archiveStageName
){
    return ShaderAssetLoader::EnsureLoaded(
        outShader,
        shaderName,
        variantName,
        shaderType,
        debugName,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        NWB_TEXT("RendererSystem"),
        archiveStageName
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

