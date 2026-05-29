// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/module.h>
#include <core/assets/ref.h>
#include <core/graphics/api.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialParameterValueType{
    enum Enum : u32{
        None = 0,
        Bool = 1,
        Char = 2,
        UChar = 3,
        Short = 4,
        UShort = 5,
        Int = 6,
        UInt = 7,
        Half = 8,
        Float = 9,
    };
};

namespace MaterialBlockClass{
    enum Enum : u32{
        None = 0,
        MaterialConstant = 1,
        MaterialMutable = 2,
    };
};

[[nodiscard]] inline bool IsValidMaterialBlockClass(const MaterialBlockClass::Enum blockClass){
    return blockClass == MaterialBlockClass::MaterialConstant || blockClass == MaterialBlockClass::MaterialMutable;
}


namespace MaterialLayoutFieldType{
    enum Enum : u32{
        None = 0,
        Bool = 1,
        Bool2 = 2,
        Bool3 = 3,
        Bool4 = 4,
        Char = 5,
        Char2 = 6,
        Char3 = 7,
        Char4 = 8,
        UChar = 9,
        UChar2 = 10,
        UChar3 = 11,
        UChar4 = 12,
        Short = 13,
        Short2 = 14,
        Short3 = 15,
        Short4 = 16,
        UShort = 17,
        UShort2 = 18,
        UShort3 = 19,
        UShort4 = 20,
        Int = 21,
        Int2 = 22,
        Int3 = 23,
        Int4 = 24,
        UInt = 25,
        UInt2 = 26,
        UInt3 = 27,
        UInt4 = 28,
        Half = 29,
        Half2 = 30,
        Half3 = 31,
        Half4 = 32,
        Float = 33,
        Float2 = 34,
        Float3 = 35,
        Float4 = 36,
    };
};

[[nodiscard]] inline bool IsValidMaterialLayoutFieldType(const MaterialLayoutFieldType::Enum fieldType){
    return fieldType >= MaterialLayoutFieldType::Bool && fieldType <= MaterialLayoutFieldType::Float4;
}

[[nodiscard]] inline u32 MaterialLayoutFieldComponentCount(const MaterialLayoutFieldType::Enum fieldType){
    if(!IsValidMaterialLayoutFieldType(fieldType))
        return 0u;

    return ((static_cast<u32>(fieldType) - 1u) % 4u) + 1u;
}

[[nodiscard]] inline MaterialParameterValueType::Enum MaterialLayoutFieldValueType(
    const MaterialLayoutFieldType::Enum fieldType
){
    if(!IsValidMaterialLayoutFieldType(fieldType))
        return MaterialParameterValueType::None;

    switch((static_cast<u32>(fieldType) - 1u) / 4u){
    case 0u: return MaterialParameterValueType::Bool;
    case 1u: return MaterialParameterValueType::Char;
    case 2u: return MaterialParameterValueType::UChar;
    case 3u: return MaterialParameterValueType::Short;
    case 4u: return MaterialParameterValueType::UShort;
    case 5u: return MaterialParameterValueType::Int;
    case 6u: return MaterialParameterValueType::UInt;
    case 7u: return MaterialParameterValueType::Half;
    case 8u: return MaterialParameterValueType::Float;
    default: return MaterialParameterValueType::None;
    }
}

[[nodiscard]] inline MaterialLayoutFieldType::Enum MaterialLayoutFieldTypeFromParameterType(
    const MaterialParameterValueType::Enum valueType,
    const u32 componentCount
){
    if(componentCount == 0u || componentCount > 4u)
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

[[nodiscard]] inline u32 MaterialParameterValueTypeByteSize(const MaterialParameterValueType::Enum valueType){
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

[[nodiscard]] inline u32 MaterialLayoutFieldByteSize(const MaterialLayoutFieldType::Enum fieldType){
    return
        MaterialLayoutFieldComponentCount(fieldType)
        * MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType))
    ;
}

[[nodiscard]] inline u32 MaterialLayoutFieldAlignment(const MaterialLayoutFieldType::Enum fieldType){
    return MaterialParameterValueTypeByteSize(MaterialLayoutFieldValueType(fieldType));
}

[[nodiscard]] inline bool AlignMaterialLayoutFieldOffset(
    const u32 byteOffset,
    const MaterialLayoutFieldType::Enum fieldType,
    u32& outByteOffset
){
    const u32 alignment = MaterialLayoutFieldAlignment(fieldType);
    if(alignment == 0u)
        return false;

    return AlignUpU32Checked(byteOffset, alignment, outByteOffset);
}

[[nodiscard]] inline bool AlignMaterialLayoutBlockByteSize(const u32 byteSize, u32& outByteSize){
    return AlignUpU32Checked(byteSize, sizeof(u32), outByteSize);
}

struct MaterialTypedLayoutBlock{
    Name blockName = NAME_NONE;
    MaterialBlockClass::Enum blockClass = MaterialBlockClass::None;
    u32 fieldBegin = 0u;
    u32 fieldCount = 0u;
    u32 byteSize = 0u;
};

struct MaterialTypedLayoutField{
    Name fieldName = NAME_NONE;
    MaterialLayoutFieldType::Enum fieldType = MaterialLayoutFieldType::None;
    u32 offset = 0u;
    UInt4U defaultValue = {};
};


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


public:
    explicit Material(Core::Assets::AssetArena& arena)
        : m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
    {}
    Material(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Material>(virtualPath)
        , m_shaderVariant(arena)
        , m_typedLayoutBlocks(arena)
        , m_typedLayoutFields(arena)
        , m_typedBlockBytes(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setShaderVariant(AStringView variantName){ m_shaderVariant.assign(variantName); }
    void setMaterialInterface(const Name& materialInterface){ m_materialInterface = materialInterface; }
    void setTransparent(const bool transparent){ m_transparent = transparent; }
    void setTwoSided(const bool twoSided){ m_twoSided = twoSided; }
    void setTypedLayout(
        u64 layoutHash,
        const TypedLayoutBlockVector& blocks,
        const TypedLayoutFieldVector& fields,
        const TypedBlockByteVector& blockBytes
    );
    bool setShaderForStage(Core::ShaderType::Enum shaderType, const Core::Assets::AssetRef<Shader>& shaderAsset);

    bool findShaderForStage(Core::ShaderType::Enum shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset)const;

public:
    [[nodiscard]] const Core::Assets::AssetString& shaderVariant()const{ return m_shaderVariant; }
    [[nodiscard]] const Name& materialInterface()const{ return m_materialInterface; }
    [[nodiscard]] u64 typedLayoutHash()const{ return m_typedLayoutHash; }
    [[nodiscard]] const TypedLayoutBlockVector& typedLayoutBlocks()const{ return m_typedLayoutBlocks; }
    [[nodiscard]] const TypedLayoutFieldVector& typedLayoutFields()const{ return m_typedLayoutFields; }
    [[nodiscard]] const TypedBlockByteVector& typedBlockBytes()const{ return m_typedBlockBytes; }
    [[nodiscard]] const StageShaderArray& stageShaders()const{ return m_stageShaders; }
    [[nodiscard]] u32 stageShaderCount()const{ return m_stageShaderCount; }
    [[nodiscard]] bool transparent()const{ return m_transparent; }
    [[nodiscard]] bool twoSided()const{ return m_twoSided; }


private:
    void clearStageShaders();


private:
    Core::Assets::AssetString m_shaderVariant;
    Name m_materialInterface = NAME_NONE;
    u64 m_typedLayoutHash = 0u;
    TypedLayoutBlockVector m_typedLayoutBlocks;
    TypedLayoutFieldVector m_typedLayoutFields;
    TypedBlockByteVector m_typedBlockBytes;
    StageShaderArray m_stageShaders;
    u32 m_stageShaderCount = 0;
    bool m_transparent = false;
    bool m_twoSided = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MaterialAssetCodec final : public Core::Assets::AssetCodec<Material>{
public:
    MaterialAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

