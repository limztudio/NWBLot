// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <core/assets/asset_ref.h>
#include <core/graphics/common.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialParameterValueType{
    enum Enum : u32{
        None = 0,
        Float = 1,
        Int = 2,
        UInt = 3,
        Bool = 4,
    };
};

struct MaterialParameterGpuData{
    UInt4 meta = {};
    UInt4 data = {};
};
static_assert(sizeof(MaterialParameterGpuData) == sizeof(u32) * 8u, "MaterialParameterGpuData layout must match the mesh shaders");
static_assert(alignof(MaterialParameterGpuData) >= alignof(UInt4), "MaterialParameterGpuData must stay SIMD-aligned");
static_assert(IsTriviallyCopyable_V<MaterialParameterGpuData>, "MaterialParameterGpuData must stay cheap to upload");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::TypedAsset<Material>{
public:
    NWB_DEFINE_ASSET_TYPE("material")


public:
    static constexpr auto s_ShaderStageCount = static_cast<usize>(Core::ShaderType::Count);

    using StageShaderArray = Array<Core::Assets::AssetRef<Shader>, s_ShaderStageCount>;
    using ParameterVector = Vector<MaterialParameterGpuData>;


public:
    Material() = default;
    explicit Material(const Name& virtualPath)
        : Core::Assets::TypedAsset<Material>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset);
#if defined(NWB_COOK)
    bool setParameter(const CompactString& key, const CompactString& value);
#endif

    bool findShaderForStage(Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const;

    [[nodiscard]] const AString& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const StageShaderArray& stageShaders()const{ return m_stageShaders; }
    [[nodiscard]] u32 stageShaderCount()const{ return m_stageShaderCount; }
    [[nodiscard]] const ParameterVector& parameters()const{ return m_parameters; }
    [[nodiscard]] f32 alpha()const{ return m_alpha; }
    [[nodiscard]] bool transparent()const{ return m_transparent; }


private:
    void clearStageShaders();


private:
    AString m_shaderVariant;
    StageShaderArray m_stageShaders;
    ParameterVector m_parameters;
    f32 m_alpha = 1.f;
    u32 m_stageShaderCount = 0;
    bool m_transparent = false;
#if defined(NWB_COOK)
    u32 m_alphaPriority = Limit<u32>::s_Max;
    u32 m_modePriority = Limit<u32>::s_Max;
    bool m_modeTransparent = false;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::TypedAssetCodec<Material>{
public:
    MaterialAssetCodec() = default;

public:
    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

