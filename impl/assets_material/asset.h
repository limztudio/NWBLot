// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/paths.h>
#include <core/assets/ref.h>
#include <core/graphics/api.h>
#include <impl/assets/graphics/mesh/material_typed_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Sampler;
class Shader;
class Texture;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transparent materials need all three pass shaders; opaque materials carry none.
[[nodiscard]] inline bool HasValidMaterialAvboitPixelShaderContract(
    const bool transparent,
    const Core::Assets::AssetRef<Shader>& accumulatePixelShader,
    const Core::Assets::AssetRef<Shader>& occupancyPixelShader,
    const Core::Assets::AssetRef<Shader>& extinctionPixelShader
)noexcept{
    if(transparent){
        return
            accumulatePixelShader.valid()
            && occupancyPixelShader.valid()
            && extinctionPixelShader.valid()
        ;
    }

    return
        !accumulatePixelShader.valid()
        && !occupancyPixelShader.valid()
        && !extinctionPixelShader.valid()
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialParameterValueType{
    static constexpr auto s_MaterialParameterValueTypeNoneBase = 0;
    enum Enum : u32{
        None = s_MaterialParameterValueTypeNoneBase,
        Bool,
        Char,
        UChar,
        Short,
        UShort,
        Int,
        UInt,
        Half,
        Float,
    };
};

namespace MaterialBlockClass{
    static constexpr auto s_MaterialBlockClassNoneBase = 0;
    enum Enum : u32{
        None = s_MaterialBlockClassNoneBase,
        MaterialConstant,
        MaterialMutable,
    };
};

[[nodiscard]] inline bool IsValidMaterialBlockClass(const MaterialBlockClass::Enum blockClass)noexcept{
    return blockClass == MaterialBlockClass::MaterialConstant || blockClass == MaterialBlockClass::MaterialMutable;
}


namespace MaterialResourceKind{
    static constexpr auto s_MaterialResourceKindNoneBase = 0;
    enum Enum : u32{
        None = s_MaterialResourceKindNoneBase,
        SampledImage2D,
        Sampler,
    };
};

[[nodiscard]] inline bool IsValidMaterialResourceKind(const MaterialResourceKind::Enum resourceKind)noexcept{
    return resourceKind == MaterialResourceKind::SampledImage2D || resourceKind == MaterialResourceKind::Sampler;
}


// Resource fields name an engine/project asset resolved to a heap slot at runtime.
namespace MaterialResourceSource{
    static constexpr auto s_MaterialResourceSourceNoneBase = 0u;
    enum Enum : u32{
        None = s_MaterialResourceSourceNoneBase,
        Asset,
    };
};

[[nodiscard]] inline bool IsMaterialAssetReference(const AStringView resourceName)noexcept{
    const usize rootEnd = resourceName.find('/');
    if(
        rootEnd == AStringView::npos
        || rootEnd == 0u
        || rootEnd + 1u >= resourceName.size()
    )
        return false;

    const AStringView virtualRoot = resourceName.substr(0u, rootEnd);
    if(
        virtualRoot != Core::Assets::s_EngineVirtualRoot
        && virtualRoot != Core::Assets::s_ProjectVirtualRoot
    )
        return false;

    usize componentBegin = rootEnd + 1u;
    for(usize index = componentBegin; index <= resourceName.size(); ++index){
        if(index != resourceName.size() && resourceName[index] != '/')
            continue;

        const AStringView component = resourceName.substr(componentBegin, index - componentBegin);
        if(component.empty() || component == "." || component == ".." || component.find('\\') != AStringView::npos)
            return false;
        componentBegin = index + 1u;
    }
    return true;
}

[[nodiscard]] inline bool IsSupportedMaterialResourceReference(
    const MaterialResourceKind::Enum resourceKind,
    const MaterialResourceSource::Enum resourceSource,
    const AStringView resourceName
)noexcept{
    return IsValidMaterialResourceKind(resourceKind)
        && resourceSource == MaterialResourceSource::Asset
        && IsMaterialAssetReference(resourceName)
    ;
}

// Serialized resource paths arrive as Name hashes, so their original text cannot be revalidated here.
// The explicit asset source and the resource kind keep the renderer's asset-family dispatch unambiguous.
[[nodiscard]] inline bool IsValidSerializedMaterialResourceReference(
    const MaterialResourceKind::Enum resourceKind,
    const MaterialResourceSource::Enum resourceSource,
    const Name& resourceName
)noexcept{
    if(!resourceName)
        return false;

    return IsValidMaterialResourceKind(resourceKind)
        && resourceSource == MaterialResourceSource::Asset
    ;
}


// Material-authored fixture names resolve to heap descriptors through the shared cook/load/render contract.
namespace MaterialResourceFixture{
    inline constexpr AStringView s_CheckerRgba8 = "builtin/material_fixture/checker_rgba8";
    inline constexpr AStringView s_LinearClamp = "builtin/material_fixture/linear_clamp";
};


[[nodiscard]] inline bool IsKnownMaterialResourceFixture(
    const MaterialResourceKind::Enum resourceKind,
    const AStringView fixtureName
)noexcept{
    switch(resourceKind){
    case MaterialResourceKind::SampledImage2D:
        return fixtureName == MaterialResourceFixture::s_CheckerRgba8;
    case MaterialResourceKind::Sampler:
        return fixtureName == MaterialResourceFixture::s_LinearClamp;
    default:
        return false;
    }
}


[[nodiscard]] inline bool IsKnownMaterialResourceFixture(
    const MaterialResourceKind::Enum resourceKind,
    const Name& fixtureName
){
    switch(resourceKind){
    case MaterialResourceKind::SampledImage2D:
        return fixtureName == Name(MaterialResourceFixture::s_CheckerRgba8);
    case MaterialResourceKind::Sampler:
        return fixtureName == Name(MaterialResourceFixture::s_LinearClamp);
    default:
        return false;
    }
}


[[nodiscard]] inline bool IsValidSerializedMaterialResourceReference(
    const MaterialResourceKind::Enum resourceKind,
    const MaterialResourceSource::Enum resourceSource,
    const Name& resourceName,
    const Name& fixtureName
){
    if(!IsValidMaterialResourceKind(resourceKind) || resourceSource != MaterialResourceSource::Asset)
        return false;

    if(fixtureName)
        return IsKnownMaterialResourceFixture(resourceKind, fixtureName);

    return static_cast<bool>(resourceName);
}


namespace MaterialLayoutFieldType{
    static constexpr auto s_MaterialLayoutFieldTypeNoneBase = 0;
    static constexpr auto s_SampledImage2DBase = 37;
    enum Enum : u32{
        None = s_MaterialLayoutFieldTypeNoneBase,
        Bool,
        Bool2,
        Bool3,
        Bool4,
        Char,
        Char2,
        Char3,
        Char4,
        UChar,
        UChar2,
        UChar3,
        UChar4,
        Short,
        Short2,
        Short3,
        Short4,
        UShort,
        UShort2,
        UShort3,
        UShort4,
        Int,
        Int2,
        Int3,
        Int4,
        UInt,
        UInt2,
        UInt3,
        UInt4,
        Half,
        Half2,
        Half3,
        Half4,
        Float,
        Float2,
        Float3,
        Float4,
        // Resource fields occupy one patched uint heap slot in the typed-byte payload.
        // They intentionally live after the contiguous numeric range so a resource can never be mistaken for an authored uint parameter.
        SampledImage2D = s_SampledImage2DBase,
        Sampler,
    };
};

inline constexpr u32 s_MaterialLayoutFieldComponentsPerValueType = NWB_MATERIAL_TYPED_VALUE_COMPONENT_COUNT;
inline constexpr u32 s_MaterialLayoutFieldFirstTypeId = static_cast<u32>(MaterialLayoutFieldType::Bool);
inline constexpr u32 s_MaterialParameterFirstValueTypeId = static_cast<u32>(MaterialParameterValueType::Bool);
static_assert(
    static_cast<u32>(MaterialParameterValueType::Float) - s_MaterialParameterFirstValueTypeId
        == (static_cast<u32>(MaterialLayoutFieldType::Float4) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType,
    "Material layout field/value type ordering must remain contiguous"
);

[[nodiscard]] inline bool IsValidMaterialLayoutFieldType(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    return fieldType >= MaterialLayoutFieldType::Bool && fieldType <= MaterialLayoutFieldType::Sampler;
}

[[nodiscard]] inline bool IsMaterialLayoutNumericFieldType(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    return fieldType >= MaterialLayoutFieldType::Bool && fieldType <= MaterialLayoutFieldType::Float4;
}

[[nodiscard]] inline bool IsMaterialLayoutResourceFieldType(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    return fieldType == MaterialLayoutFieldType::SampledImage2D || fieldType == MaterialLayoutFieldType::Sampler;
}

[[nodiscard]] inline MaterialResourceKind::Enum MaterialLayoutFieldResourceKind(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    switch(fieldType){
    case MaterialLayoutFieldType::SampledImage2D: return MaterialResourceKind::SampledImage2D;
    case MaterialLayoutFieldType::Sampler: return MaterialResourceKind::Sampler;
    default: return MaterialResourceKind::None;
    }
}

[[nodiscard]] inline u32 MaterialLayoutFieldComponentCount(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    if(!IsMaterialLayoutNumericFieldType(fieldType))
        return 0u;

    return ((static_cast<u32>(fieldType) - s_MaterialLayoutFieldFirstTypeId) % s_MaterialLayoutFieldComponentsPerValueType) + 1u;
}

[[nodiscard]] inline MaterialParameterValueType::Enum MaterialLayoutFieldValueType(
    const MaterialLayoutFieldType::Enum fieldType
)noexcept{
    if(!IsMaterialLayoutNumericFieldType(fieldType))
        return MaterialParameterValueType::None;

    const u32 valueTypeOffset = (static_cast<u32>(fieldType) - s_MaterialLayoutFieldFirstTypeId) / s_MaterialLayoutFieldComponentsPerValueType;
    return static_cast<MaterialParameterValueType::Enum>(s_MaterialParameterFirstValueTypeId + valueTypeOffset);
}

[[nodiscard]] inline MaterialLayoutFieldType::Enum MaterialLayoutFieldTypeFromParameterType(
    const MaterialParameterValueType::Enum valueType,
    const u32 componentCount
)noexcept{
    if(componentCount == 0u || componentCount > s_MaterialLayoutFieldComponentsPerValueType)
        return MaterialLayoutFieldType::None;

    u32 firstFieldType = 0u;
    switch(valueType){
    case MaterialParameterValueType::Bool: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Bool); break;
    case MaterialParameterValueType::Char: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Char); break;
    case MaterialParameterValueType::UChar: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UChar); break;
    case MaterialParameterValueType::Short: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Short); break;
    case MaterialParameterValueType::UShort: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UShort); break;
    case MaterialParameterValueType::Int: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Int); break;
    case MaterialParameterValueType::UInt: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::UInt); break;
    case MaterialParameterValueType::Half: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Half); break;
    case MaterialParameterValueType::Float: firstFieldType = static_cast<u32>(MaterialLayoutFieldType::Float); break;
    default: return MaterialLayoutFieldType::None;
    }

    return static_cast<MaterialLayoutFieldType::Enum>(firstFieldType + componentCount - 1u);
}

[[nodiscard]] inline u32 MaterialParameterValueTypeByteSize(const MaterialParameterValueType::Enum valueType)noexcept{
    switch(valueType){
    case MaterialParameterValueType::Bool:
    case MaterialParameterValueType::Char:
    case MaterialParameterValueType::UChar:
        return sizeof(u8);
    case MaterialParameterValueType::Short:
    case MaterialParameterValueType::UShort:
    case MaterialParameterValueType::Half:
        return sizeof(u16);
    case MaterialParameterValueType::Int:
    case MaterialParameterValueType::UInt:
    case MaterialParameterValueType::Float:
        return sizeof(u32);
    default:
        return 0u;
    }
}

[[nodiscard]] inline u32 MaterialLayoutFieldByteSize(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return sizeof(u32);

    return
        MaterialLayoutFieldComponentCount(fieldType)
        * MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType))
    ;
}

[[nodiscard]] inline u32 MaterialLayoutFieldAlignment(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    if(IsMaterialLayoutResourceFieldType(fieldType))
        return sizeof(u32);

    return MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType));
}

[[nodiscard]] inline bool AlignMaterialLayoutFieldOffset(
    const u32 byteOffset,
    const MaterialLayoutFieldType::Enum fieldType,
    u32& outByteOffset
)noexcept{
    const u32 alignment = MaterialLayoutFieldAlignment(fieldType);
    if(alignment == 0u)
        return false;

    return AlignUpU32Checked(byteOffset, alignment, outByteOffset);
}

[[nodiscard]] inline bool AlignMaterialLayoutBlockByteSize(const u32 byteSize, u32& outByteSize)noexcept{
    return AlignUpU32Checked(byteSize, NWB_MATERIAL_TYPED_WORD_BYTES, outByteSize);
}

struct MaterialTypedLayoutBlock{
    Name blockName = s_NameNone;
    MaterialBlockClass::Enum blockClass = MaterialBlockClass::None;
    u32 fieldBegin = 0u;
    u32 fieldCount = 0u;
    u32 byteSize = 0u;
};

struct MaterialTypedLayoutField{
    Name fieldName = s_NameNone;
    MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
    u32 offset = 0u;
    UInt4U defaultValue = {};
};

// A cooked material separates resource identity from its numeric/default payload.
// `constantByteOffset` is the slot word the renderer patches after resolving the descriptor handle. Exactly one typed reference is valid.
struct MaterialResourceReference{
    Name blockName = s_NameNone;
    Name fieldName = s_NameNone;
    Core::Assets::AssetRef<Texture> textureAsset;
    Core::Assets::AssetRef<Sampler> samplerAsset;
    // A cooked material keeps resource identity separate from its numeric/default typed payload.
    // `constantByteOffset` points at the four-byte slot word the renderer patches after it resolves the device-lifetime descriptor handle.
    Name fixtureName = s_NameNone;
    MaterialResourceKind::Enum resourceKind = MaterialResourceKind::None;
    MaterialResourceSource::Enum resourceSource = MaterialResourceSource::None;
    u32 constantByteOffset = 0u;
};

[[nodiscard]] inline bool AssignMaterialResourceReferenceAsset(
    MaterialResourceReference& reference,
    const MaterialResourceKind::Enum resourceKind,
    const Name& resourceName
){
    switch(resourceKind){
    case MaterialResourceKind::SampledImage2D:
        reference.textureAsset.virtualPath = resourceName;
        return true;
    case MaterialResourceKind::Sampler:
        reference.samplerAsset.virtualPath = resourceName;
        return true;
    default:
        NWB_ASSERT(false);
        return false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Material final : public Core::Assets::TypedAsset<Material>{
public:
    NWB_DEFINE_ASSET_TYPE("material")


public:
    static constexpr auto s_ShaderStageCount = static_cast<usize>(Core::ShaderType::Count);

    using StageShaderArray = Array<Core::Assets::AssetRef<Shader>, s_ShaderStageCount>;
    using TypedLayoutBlockVector = Core::Assets::AssetVector<MaterialTypedLayoutBlock>;
    using TypedLayoutFieldVector = Core::Assets::AssetVector<MaterialTypedLayoutField>;
    using TypedBlockByteVector = Core::Assets::AssetVector<u8>;
    using ResourceReferenceVector = Core::Assets::AssetVector<MaterialResourceReference>;


public:
    explicit Material(Core::Assets::AssetArena& arena)
        : m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}
    Material(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Material>(virtualPath)
        , m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
        , m_resourceReferences(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }
    void setMaterialInterface(const Name& materialInterface)noexcept{ m_materialInterface = materialInterface; }
    void setShadingModelId(const u32 shadingModelId)noexcept{ m_shadingModelId = shadingModelId; }
    void setSurfaceDispatchId(const u32 surfaceDispatchId)noexcept{ m_surfaceDispatchId = surfaceDispatchId; }
    void setAvboitAccumulatePixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset)noexcept{ m_avboitAccumulatePixelShader = shaderAsset; }
    void setAvboitOccupancyPixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset)noexcept{ m_avboitOccupancyPixelShader = shaderAsset; }
    void setAvboitExtinctionPixelShader(const Core::Assets::AssetRef<Shader>& shaderAsset)noexcept{ m_avboitExtinctionPixelShader = shaderAsset; }
    void setTransparent(const bool transparent)noexcept{ m_transparent = transparent; }
    void setTwoSided(const bool twoSided)noexcept{ m_twoSided = twoSided; }
    void setRefractive(const bool refractive)noexcept{ m_refractive = refractive; }
    void setTypedLayout(
        u64 layoutHash,
        const TypedLayoutBlockVector& blocks,
        const TypedLayoutFieldVector& fields,
        const TypedBlockByteVector& blockBytes
    );
    void setResourceReferences(const ResourceReferenceVector& resourceReferences);
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset)noexcept;

    bool findShaderForStage(Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const noexcept;

public:
    [[nodiscard]] const Core::Assets::AssetString& shaderVariant()const noexcept{ return m_shaderVariant; }
    [[nodiscard]] const Name& materialInterface()const noexcept{ return m_materialInterface; }
    [[nodiscard]] u32 shadingModelId()const noexcept{ return m_shadingModelId; }
    [[nodiscard]] u32 surfaceDispatchId()const noexcept{ return m_surfaceDispatchId; }
    [[nodiscard]] u64 typedLayoutHash()const noexcept{ return m_typedLayoutHash; }
    [[nodiscard]] const TypedLayoutBlockVector& typedLayoutBlocks()const noexcept{ return m_typedLayoutBlocks; }
    [[nodiscard]] const TypedLayoutFieldVector& typedLayoutFields()const noexcept{ return m_typedLayoutFields; }
    [[nodiscard]] const TypedBlockByteVector& typedBlockBytes()const noexcept{ return m_typedBlockBytes; }
    [[nodiscard]] const ResourceReferenceVector& resourceReferences()const noexcept{ return m_resourceReferences; }
    [[nodiscard]] const StageShaderArray& stageShaders()const noexcept{ return m_stageShaders; }
    [[nodiscard]] u32 stageShaderCount()const noexcept{ return m_stageShaderCount; }
    // Cook-generated AVBOIT accumulate pixel shader for this material's transparent draw. Valid only for surface-authored transparent materials; missing means a cook/runtime contract failure.
    // Not a graphics stage: the material has one pixel stage, and this is the transparent-only shader the renderer selects by pass.
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitAccumulatePixelShader()const noexcept{ return m_avboitAccumulatePixelShader; }
    // Occupancy/extinction twins of the accumulate shader, so all three AVBOIT passes read the same surface renderCoverage. Same validity contract as above.
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitOccupancyPixelShader()const noexcept{ return m_avboitOccupancyPixelShader; }
    [[nodiscard]] const Core::Assets::AssetRef<Shader>& avboitExtinctionPixelShader()const noexcept{ return m_avboitExtinctionPixelShader; }
    [[nodiscard]] bool transparent()const noexcept{ return m_transparent; }
    [[nodiscard]] bool twoSided()const noexcept{ return m_twoSided; }
    // Refractive-caster flag, separate from `transparent`. Refraction values stay shader-side (NwbMeshSurface).
    [[nodiscard]] bool refractive()const noexcept{ return m_refractive; }


private:
    void clearStageShaders()noexcept;


private:
    Core::Assets::AssetString m_shaderVariant;
    Name m_materialInterface = s_NameNone;
    u32 m_shadingModelId = 0u;
    u32 m_surfaceDispatchId = 0u;
    u64 m_typedLayoutHash = 0u;
    TypedLayoutBlockVector m_typedLayoutBlocks;
    TypedLayoutFieldVector m_typedLayoutFields;
    TypedBlockByteVector m_typedBlockBytes;
    ResourceReferenceVector m_resourceReferences;
    StageShaderArray m_stageShaders;
    u32 m_stageShaderCount = 0;
    bool m_transparent = false;
    bool m_twoSided = false;
    bool m_refractive = false;
    Core::Assets::AssetRef<Shader> m_avboitAccumulatePixelShader;
    Core::Assets::AssetRef<Shader> m_avboitOccupancyPixelShader;
    Core::Assets::AssetRef<Shader> m_avboitExtinctionPixelShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_ASSET_CODEC(MaterialAssetCodec, Material);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

