// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "assets_graphics_fixture.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets_graphics_material{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = AssetsGraphicsFixture::AString;
using CapturingLogger = AssetsGraphicsFixture::CapturingLogger;
using Path = AssetsGraphicsFixture::Path;
using TestArena = AssetsGraphicsFixture::TestArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ParseMaterialEntryFromMetaText(
    const AStringView metaText,
    TestArena& testArena,
    NWB::Impl::MaterialCookEntry& outEntry,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    NWB::Core::Metascript::Document doc(testArena.arena);
    if(!doc.parse(metaText))
        return false;

    const Path assetRoot = AssetsGraphicsFixture::AssetsGraphicsTestCaseRoot(testArena, "material_meta") / "assets";
    const Path nwbFilePath = assetRoot / "materials" / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(assetRoot, "project", nwbFilePath, doc, outEntry, scratchArena);
}

static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

template<usize SnippetCount>
static void CheckGeneratedSourceContainsAll(
    const AStringView generatedSourceView,
    const AStringView (&expectedSnippets)[SnippetCount]
){
    for(const AStringView expectedSnippet : expectedSnippets)
        EXPECT_TRUE(ContainsText(generatedSourceView, expectedSnippet));
}

static void CheckGeneratedSourceHasNoMutableLoads(const AStringView generatedSourceView){
    EXPECT_FALSE(ContainsText(generatedSourceView, "nwbMaterialLoadMutable"));
}

static void CheckGeneratedSourceHasNoImplicitInstanceAccessors(const AStringView generatedSourceView){
    EXPECT_FALSE(ContainsText(generatedSourceView, "nwbMeshLoadInstance()"));
}

static AString BuildGeneratedUint2ConstantText(const AStringView symbol, const u64 value){
    AString text("static const uint2 ");
    text.append(symbol.data(), symbol.size());
    text += " = uint2(";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value & 0xffffffffull), text);
    text += ", ";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value >> 32u), text);
    text += ");";
    return text;
}

static AString BuildGeneratedUintConstantText(const AStringView symbol, const u32 value){
    AString text("static const uint ");
    text.append(symbol.data(), symbol.size());
    text += " = ";
    char digits[16u];
    text += FormatDecimal(static_cast<usize>(value), digits);
    text += "u;";
    return text;
}

static bool ContainsGeneratedUint2Constant(const AStringView generatedSourceView, const AStringView symbol, const u64 value){
    const AString expected = BuildGeneratedUint2ConstantText(symbol, value);
    return ContainsText(generatedSourceView, AStringView(expected.data(), expected.size()));
}

static bool ContainsGeneratedUintConstant(const AStringView generatedSourceView, const AStringView symbol, const u32 value){
    const AString expected = BuildGeneratedUintConstantText(symbol, value);
    return ContainsText(generatedSourceView, AStringView(expected.data(), expected.size()));
}

static bool ContainsCanonicalPath(const NWB::Impl::ShaderCook::CookVector<Path>& paths, const Path& expectedPath){
    ErrorCode errorCode;
    const Path expectedAbsolutePath = AbsolutePath(expectedPath, errorCode).lexically_normal();
    if(errorCode)
        return false;

    for(const Path& path : paths){
        errorCode.clear();
        const Path absolutePath = AbsolutePath(path, errorCode).lexically_normal();
        if(!errorCode && absolutePath == expectedAbsolutePath)
            return true;
    }

    return false;
}

static void CheckMaterialBindStructBlockClass(
    const NWB::Impl::MaterialBindStruct& bindStruct,
    const NWB::Impl::MaterialBlockClass::Enum expectedBlockClass
){
    const bool hasConstantAttribute = bindStruct.findAttribute("material_constant") != nullptr;
    const bool hasMutableAttribute = bindStruct.findAttribute("material_mutable") != nullptr;
    EXPECT_EQ(hasConstantAttribute, (expectedBlockClass == NWB::Impl::MaterialBlockClass::MaterialConstant));
    EXPECT_EQ(hasMutableAttribute, (expectedBlockClass == NWB::Impl::MaterialBlockClass::MaterialMutable));
}

static void CheckGeneratedMaterialBindSource(const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "#ifndef NWB_GENERATED_MATERIAL_BIND_PROJECT_MATERIAL_INTERFACES_TEST_SURFACE_BIND",
        "static const uint2 NWB_MATERIAL_BIND_INTERFACE_HASH_0 = uint2(",
        "static const uint2 NWB_MATERIAL_BIND_LAYOUT_HASH = uint2(",
        "#if NWB_MATERIAL_TYPED_BINDING != NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE",
        "static const uint NWB_MATERIAL_BIND_BLOCK_COUNT = 2u;",
        "static const uint NWB_MATERIAL_BIND_FIELD_COUNT = 6u;",
        "static const uint NWB_MATERIAL_BIND_STORAGE_CONSTANT = 1u;",
        "static const uint NWB_MATERIAL_BIND_STORAGE_MUTABLE = 2u;",
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 44u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 4u;",
        "static const uint NWB_MATERIAL_BIND_RUNTIME_STORAGE = 2u;",
        "static const uint NWB_MATERIAL_BIND_RUNTIME_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_RUNTIME_BYTE_SIZE = 4u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_STORAGE = 1u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_BYTE_SIZE = 44u;",
        "struct NwbTestSurfaceMaterial",
        "static const uint2 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_KEY = uint2(",
        "static const float4 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_DEFAULT = float4(1.0, 1.0, 1.0, 1.0);",
        "static const uint NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_BYTE_OFFSET = 0u;",
        "nwbMaterialLoadConstantFloat4(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET)",
        "nwbMaterialLoadConstantInt2(instance, NWB_MATERIAL_BIND_SURFACE_LAYER_IDS_BYTE_OFFSET)",
        "nwbMaterialLoadConstantUInt3(instance, NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_BYTE_OFFSET)",
        "nwbMaterialLoadConstantBool4(instance, NWB_MATERIAL_BIND_SURFACE_CHANNEL_ENABLED_BYTE_OFFSET)",
        "nwbMaterialLoadMutableFloat(instance, NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_BYTE_OFFSET)",
        "float4 nwbMaterialBindLoadSurfaceBaseColor",
        "NwbTestSurfaceMaterial nwbMaterialBindLoadSurface",
        "static const float NWB_MATERIAL_BIND_RUNTIME_FADE_ALPHA_DEFAULT = float(1.0);",
    };
    CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
    EXPECT_FALSE(ContainsText(
        generatedSourceView,
        "nwbMaterialFind"
    ));
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(generatedSourceView);
}

static void CheckGeneratedHalfMaterialBindSource(const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 20u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 0u;",
        "static const half NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_DEFAULT = half(0.5h);",
        "static const half2 NWB_MATERIAL_BIND_SURFACE_RANGE_DEFAULT = half2(0.0h, 1.0h);",
        "static const half3 NWB_MATERIAL_BIND_SURFACE_TINT_DEFAULT = half3(0.25h, 0.5h, 0.75h);",
        "static const half4 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_DEFAULT = half4(1.0h, 1.0h, 1.0h, 1.0h);",
        "static const uint NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_RANGE_BYTE_OFFSET = 2u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET = 6u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET = 12u;",
        "nwbMaterialLoadConstantHalf(instance, NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET)",
        "nwbMaterialLoadConstantHalf2(instance, NWB_MATERIAL_BIND_SURFACE_RANGE_BYTE_OFFSET)",
        "nwbMaterialLoadConstantHalf3(instance, NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET)",
        "nwbMaterialLoadConstantHalf4(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_BYTE_OFFSET)",
        "half4 nwbMaterialBindLoadSurfaceBaseColor",
    };
    CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(generatedSourceView);
}

static void CheckGeneratedMixedHalfMaterialBindSource(const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 24u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_METALLIC_BYTE_OFFSET = 4u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET = 8u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_FLAGS_BYTE_OFFSET = 16u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_TAIL_BYTE_OFFSET = 20u;",
    };
    CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(generatedSourceView);
}

static void CheckGeneratedCompactIntegerMaterialBindSource(const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 20u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 0u;",
        "bool4 enabled;",
        "char4 signed_bytes;",
        "uchar4 bytes;",
        "short2 signed_words;",
        "ushort2 words;",
        "static const bool4 NWB_MATERIAL_BIND_SURFACE_ENABLED_DEFAULT = bool4(true, false, true, false);",
        "static const char4 NWB_MATERIAL_BIND_SURFACE_SIGNED_BYTES_DEFAULT = char4(-1, 0, 1, 127);",
        "static const uchar4 NWB_MATERIAL_BIND_SURFACE_BYTES_DEFAULT = uchar4(0u, 1u, 254u, 255u);",
        "static const short2 NWB_MATERIAL_BIND_SURFACE_SIGNED_WORDS_DEFAULT = short2(-32768, 32767);",
        "static const ushort2 NWB_MATERIAL_BIND_SURFACE_WORDS_DEFAULT = ushort2(0u, 65535u);",
        "static const uint NWB_MATERIAL_BIND_SURFACE_ENABLED_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_SIGNED_BYTES_BYTE_OFFSET = 4u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_BYTES_BYTE_OFFSET = 8u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_SIGNED_WORDS_BYTE_OFFSET = 12u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_WORDS_BYTE_OFFSET = 16u;",
        "nwbMaterialLoadConstantBool4(instance, NWB_MATERIAL_BIND_SURFACE_ENABLED_BYTE_OFFSET)",
        "nwbMaterialLoadConstantChar4(instance, NWB_MATERIAL_BIND_SURFACE_SIGNED_BYTES_BYTE_OFFSET)",
        "nwbMaterialLoadConstantUChar4(instance, NWB_MATERIAL_BIND_SURFACE_BYTES_BYTE_OFFSET)",
        "nwbMaterialLoadConstantShort2(instance, NWB_MATERIAL_BIND_SURFACE_SIGNED_WORDS_BYTE_OFFSET)",
        "nwbMaterialLoadConstantUShort2(instance, NWB_MATERIAL_BIND_SURFACE_WORDS_BYTE_OFFSET)",
    };
    CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(generatedSourceView);
}

static const NWB::Impl::MaterialTypedLayoutBlock* FindMaterialTypedLayoutBlock(
    const NWB::Impl::Material& material,
    const AStringView blockName
){
    const Name blockNameHash(blockName);
    for(const NWB::Impl::MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(block.blockName == blockNameHash)
            return &block;
    }
    return nullptr;
}

static const NWB::Impl::MaterialTypedLayoutField* FindMaterialTypedLayoutField(
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName
){
    const Name fieldNameHash(fieldName);
    for(u32 fieldOffset = 0u; fieldOffset < block.fieldCount; ++fieldOffset){
        const usize fieldIndex = static_cast<usize>(block.fieldBegin) + fieldOffset;
        if(fieldIndex >= material.typedLayoutFields().size())
            return nullptr;

        const NWB::Impl::MaterialTypedLayoutField& field = material.typedLayoutFields()[fieldIndex];
        if(field.fieldName == fieldNameHash)
            return &field;
    }
    return nullptr;
}

static const NWB::Impl::MaterialTypedLayoutField* CheckMaterialTypedLayoutField(
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName,
    const NWB::Impl::MaterialLayoutFieldType::Enum expectedFieldType,
    const u32 expectedOffset
){
    const NWB::Impl::MaterialTypedLayoutField* field = FindMaterialTypedLayoutField(material, block, fieldName);
    EXPECT_NE(field, nullptr);
    if(field){
        EXPECT_EQ(field->fieldType, expectedFieldType);
        EXPECT_EQ(field->offset, expectedOffset);
    }
    return field;
}

struct ExpectedMaterialLayoutField{
    AStringView name;
    NWB::Impl::MaterialLayoutFieldType::Enum fieldType = NWB::Impl::MaterialLayoutFieldType::None;
    u32 byteOffset = 0u;
};

template<usize ExpectedFieldCount>
static void CheckMaterialTypedLayoutFields(
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const ExpectedMaterialLayoutField (&expectedFields)[ExpectedFieldCount]
){
    for(const ExpectedMaterialLayoutField& expectedField : expectedFields)
        CheckMaterialTypedLayoutField(
            material,
            block,
            expectedField.name,
            expectedField.fieldType,
            expectedField.byteOffset
        );
}

template<typename ValueType>
static ValueType LoadMaterialTypedLayoutDefaultPOD(
    const NWB::Impl::MaterialTypedLayoutField& field,
    const u32 componentIndex
){
    ValueType value = {};
    const usize byteOffset = static_cast<usize>(componentIndex) * sizeof(ValueType);
    if(byteOffset <= sizeof(field.defaultValue) && sizeof(value) <= sizeof(field.defaultValue) - byteOffset){
        const u8* bytes = reinterpret_cast<const u8*>(&field.defaultValue);
        NWB_MEMCPY(&value, sizeof(value), bytes + byteOffset, sizeof(value));
    }
    return value;
}

template<typename ValueType, usize ExpectedDefaultCount>
static void CheckMaterialTypedLayoutDefaultPODValues(
    const NWB::Impl::MaterialTypedLayoutField& field,
    const ValueType (&expectedDefaults)[ExpectedDefaultCount]
){
    for(usize componentIndex = 0u; componentIndex < ExpectedDefaultCount; ++componentIndex){
        EXPECT_EQ(LoadMaterialTypedLayoutDefaultPOD<ValueType>(field, static_cast<u32>(componentIndex)), expectedDefaults[componentIndex]);
    }
}

template<usize ExpectedDefaultCount>
static void CheckMaterialTypedLayoutHalfField(
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName,
    const NWB::Impl::MaterialLayoutFieldType::Enum expectedFieldType,
    const u32 expectedOffset,
    const f32 (&expectedDefaults)[ExpectedDefaultCount]
){
    const NWB::Impl::MaterialTypedLayoutField* field = CheckMaterialTypedLayoutField(
        material,
        block,
        fieldName,
        expectedFieldType,
        expectedOffset
    );
    if(!field)
        return;

    for(usize componentIndex = 0u; componentIndex < ExpectedDefaultCount; ++componentIndex){
        EXPECT_EQ(ConvertHalfToFloat(LoadMaterialTypedLayoutDefaultPOD<Half>(*field, static_cast<u32>(componentIndex))), expectedDefaults[componentIndex]);
    }
}

template<typename T>
static bool LoadMaterialTypedBlockPOD(
    const NWB::Impl::Material& material,
    const AStringView blockName,
    const u32 byteOffset,
    T& outValue
){
    outValue = {};

    const Name blockNameHash(blockName);
    usize blockByteBegin = 0u;
    for(const NWB::Impl::MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(block.blockName == blockNameHash){
            const usize valueOffset = blockByteBegin + byteOffset;
            if(
                valueOffset > material.typedBlockBytes().size()
                || sizeof(outValue) > material.typedBlockBytes().size() - valueOffset
            )
                return false;

            NWB_MEMCPY(&outValue, sizeof(outValue), material.typedBlockBytes().data() + valueOffset, sizeof(outValue));
            return true;
        }

        blockByteBegin += block.byteSize;
    }

    return false;
}

struct ExpectedHalfBlockValue{
    u32 byteOffset = 0u;
    f32 value = 0.f;
};

template<usize ExpectedValueCount>
static void CheckMaterialTypedBlockHalfRawValues(
    const NWB::Impl::Material& material,
    const AStringView blockName,
    const ExpectedHalfBlockValue (&expectedValues)[ExpectedValueCount]
){
    Half rawValue = 0u;
    for(const ExpectedHalfBlockValue& expectedValue : expectedValues){
        EXPECT_TRUE(LoadMaterialTypedBlockPOD(material, blockName, expectedValue.byteOffset, rawValue)
            && rawValue == ConvertFloatToHalf(expectedValue.value));
    }
}

template<typename ValueType>
struct ExpectedTypedBlockValue{
    AStringView blockName;
    u32 byteOffset = 0u;
    ValueType value = {};
};

using ExpectedTypedBlockFloatValue = ExpectedTypedBlockValue<f32>;
using ExpectedTypedBlockU32Value = ExpectedTypedBlockValue<u32>;
using ExpectedTypedBlockU8Value = ExpectedTypedBlockValue<u8>;
using ExpectedTypedBlockI16Value = ExpectedTypedBlockValue<i16>;
using ExpectedTypedBlockU16Value = ExpectedTypedBlockValue<u16>;

template<typename ValueType, usize ExpectedValueCount>
static void CheckMaterialTypedBlockValues(
    const NWB::Impl::Material& material,
    const ExpectedTypedBlockValue<ValueType> (&expectedValues)[ExpectedValueCount]
){
    ValueType loadedValue = {};
    for(const ExpectedTypedBlockValue<ValueType>& expectedValue : expectedValues){
        EXPECT_TRUE(LoadMaterialTypedBlockPOD(material, expectedValue.blockName, expectedValue.byteOffset, loadedValue)
            && loadedValue == expectedValue.value);
    }
}

static void CheckMinimalMaterialTypedLayout(
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    EXPECT_NE(material.typedLayoutHash(), 0u);
    EXPECT_EQ(material.typedLayoutBlocks().size(), s_ExpectedDualCount);
    EXPECT_EQ(material.typedLayoutFields().size(), 6u);

    const NWB::Impl::MaterialTypedLayoutBlock* runtimeBlock = FindMaterialTypedLayoutBlock(material, "runtime");
    EXPECT_NE(runtimeBlock, nullptr);
    if(runtimeBlock){
        EXPECT_EQ(runtimeBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialMutable);
        EXPECT_EQ(runtimeBlock->fieldCount, 1u);
        EXPECT_EQ(runtimeBlock->byteSize, 4u);

        const NWB::Impl::MaterialTypedLayoutField* fadeAlpha = CheckMaterialTypedLayoutField(
            material,
            *runtimeBlock,
            "fade_alpha",
            NWB::Impl::MaterialLayoutFieldType::Float,
            0u
        );
        if(fadeAlpha){
            const f32 fadeAlphaDefaults[] = { 1.0f };
            CheckMaterialTypedLayoutDefaultPODValues(*fadeAlpha, fadeAlphaDefaults);
        }
    }

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    EXPECT_NE(surfaceBlock, nullptr);
    if(surfaceBlock){
        EXPECT_EQ(surfaceBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialConstant);
        EXPECT_EQ(surfaceBlock->fieldCount, 5u);
        EXPECT_EQ(surfaceBlock->byteSize, 44u);

        const NWB::Impl::MaterialTypedLayoutField* baseColor = CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "base_color",
            NWB::Impl::MaterialLayoutFieldType::Float4,
            0u
        );
        if(baseColor){
            const f32 baseColorDefaults[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            CheckMaterialTypedLayoutDefaultPODValues(*baseColor, baseColorDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* roughness = CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "roughness",
            NWB::Impl::MaterialLayoutFieldType::Float,
            16u
        );
        if(roughness){
            const f32 roughnessDefaults[] = { 0.5f };
            CheckMaterialTypedLayoutDefaultPODValues(*roughness, roughnessDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* layerIds = CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "layer_ids",
            NWB::Impl::MaterialLayoutFieldType::Int2,
            20u
        );
        if(layerIds){
            const u32 layerIdDefaults[] = { 1u, s_ExpectedDualCount };
            CheckMaterialTypedLayoutDefaultPODValues(*layerIds, layerIdDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* featureMask = CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "feature_mask",
            NWB::Impl::MaterialLayoutFieldType::UInt3,
            28u
        );
        if(featureMask){
            const u32 featureMaskDefaults[] = { expectedFeatureMaskX, expectedFeatureMaskY, expectedFeatureMaskZ };
            CheckMaterialTypedLayoutDefaultPODValues(*featureMask, featureMaskDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* channelEnabled = CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "channel_enabled",
            NWB::Impl::MaterialLayoutFieldType::Bool4,
            40u
        );
        if(channelEnabled){
            const u8 channelEnabledDefaults[] = { 1u, 0u, 1u, 0u };
            CheckMaterialTypedLayoutDefaultPODValues(*channelEnabled, channelEnabledDefaults);
        }
    }
}

static void CheckMinimalMaterialTypedBlockBytes(
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    EXPECT_EQ(material.typedBlockBytes().size(), 48u);

    const ExpectedTypedBlockFloatValue expectedFloatValues[] = {
        { "runtime", 0u, 0.75f },
        { "surface", 0u, 0.25f },
        { "surface", 4u, 0.5f },
        { "surface", 8u, 0.75f },
        { "surface", 12u, 1.0f },
        { "surface", 16u, 0.25f },
    };
    CheckMaterialTypedBlockValues(material, expectedFloatValues);

    const ExpectedTypedBlockU32Value expectedU32Values[] = {
        { "surface", 20u, 1u },
        { "surface", 24u, s_ExpectedDualCount },
        { "surface", 28u, expectedFeatureMaskX },
        { "surface", 32u, expectedFeatureMaskY },
        { "surface", 36u, expectedFeatureMaskZ },
    };
    CheckMaterialTypedBlockValues(material, expectedU32Values);

    const ExpectedTypedBlockU8Value expectedU8Values[] = {
        { "surface", 40u, 1u },
        { "surface", 41u, 0u },
        { "surface", 42u, 1u },
        { "surface", 43u, 0u },
    };
    CheckMaterialTypedBlockValues(material, expectedU8Values);
}

static void CheckHalfMaterialTypedLayoutAndBlockBytes(const NWB::Impl::Material& material){
    EXPECT_NE(material.typedLayoutHash(), 0u);
    EXPECT_EQ(material.typedLayoutBlocks().size(), 1u);
    EXPECT_EQ(material.typedLayoutFields().size(), 4u);
    EXPECT_EQ(material.typedBlockBytes().size(), 20u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    EXPECT_NE(surfaceBlock, nullptr);
    if(!surfaceBlock)
        return;

    EXPECT_EQ(surfaceBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialConstant);
    EXPECT_EQ(surfaceBlock->fieldCount, 4u);
    EXPECT_EQ(surfaceBlock->byteSize, 20u);

    const f32 roughnessDefaults[] = { 0.5f };
    CheckMaterialTypedLayoutHalfField(
        material,
        *surfaceBlock,
        "roughness",
        NWB::Impl::MaterialLayoutFieldType::Half,
        0u,
        roughnessDefaults
    );

    const f32 rangeDefaults[] = { 0.0f, 1.0f };
    CheckMaterialTypedLayoutHalfField(
        material,
        *surfaceBlock,
        "range",
        NWB::Impl::MaterialLayoutFieldType::Half2,
        s_ExpectedDualCount,
        rangeDefaults
    );

    const f32 tintDefaults[] = { 0.25f, 0.5f, 0.75f };
    CheckMaterialTypedLayoutHalfField(
        material,
        *surfaceBlock,
        "tint",
        NWB::Impl::MaterialLayoutFieldType::Half3,
        6u,
        tintDefaults
    );

    const f32 baseColorDefaults[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    CheckMaterialTypedLayoutHalfField(
        material,
        *surfaceBlock,
        "base_color",
        NWB::Impl::MaterialLayoutFieldType::Half4,
        12u,
        baseColorDefaults
    );

    const ExpectedHalfBlockValue expectedHalfValues[] = {
        { 0u, 0.25f },
        { s_ExpectedDualCount, 0.125f },
        { 4u, 0.5f },
        { 6u, 1.0f },
        { 8u, 0.75f },
        { 10u, 0.5f },
        { 12u, 1.0f },
        { 14u, 0.5f },
        { 16u, 0.25f },
        { 18u, 0.0f },
    };
    CheckMaterialTypedBlockHalfRawValues(material, "surface", expectedHalfValues);
}

static void CheckMixedHalfMaterialTypedLayoutAndBlockBytes(const NWB::Impl::Material& material){
    EXPECT_NE(material.typedLayoutHash(), 0u);
    EXPECT_EQ(material.typedLayoutBlocks().size(), 1u);
    EXPECT_EQ(material.typedLayoutFields().size(), 5u);
    EXPECT_EQ(material.typedBlockBytes().size(), 24u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    EXPECT_NE(surfaceBlock, nullptr);
    if(!surfaceBlock)
        return;

    EXPECT_EQ(surfaceBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialConstant);
    EXPECT_EQ(surfaceBlock->fieldCount, 5u);
    EXPECT_EQ(surfaceBlock->byteSize, 24u);

    const ExpectedMaterialLayoutField expectedFields[] = {
        { "roughness", NWB::Impl::MaterialLayoutFieldType::Half, 0u },
        { "metallic", NWB::Impl::MaterialLayoutFieldType::Float, 4u },
        { "tint", NWB::Impl::MaterialLayoutFieldType::Half3, 8u },
        { "flags", NWB::Impl::MaterialLayoutFieldType::UInt, 16u },
        { "tail", NWB::Impl::MaterialLayoutFieldType::Half, 20u },
    };
    CheckMaterialTypedLayoutFields(material, *surfaceBlock, expectedFields);

    const auto& bytes = material.typedBlockBytes();
    if(bytes.size() < 24u)
        return;
    const u32 expectedZeroByteOffsets[] = { s_ExpectedDualCount, 3u, 14u, 15u, 22u, 23u };
    for(const u32 byteOffset : expectedZeroByteOffsets)
        EXPECT_EQ(bytes[byteOffset], 0u);

    const ExpectedHalfBlockValue expectedHalfValues[] = {
        { 0u, 0.25f },
        { 8u, 1.0f },
        { 10u, 0.5f },
        { 12u, 0.25f },
        { 20u, 0.875f },
    };
    CheckMaterialTypedBlockHalfRawValues(material, "surface", expectedHalfValues);

    const ExpectedTypedBlockFloatValue expectedFloatValues[] = {
        { "surface", 4u, 0.75f },
    };
    CheckMaterialTypedBlockValues(material, expectedFloatValues);

    const ExpectedTypedBlockU32Value expectedU32Values[] = {
        { "surface", 16u, 42u },
    };
    CheckMaterialTypedBlockValues(material, expectedU32Values);
}

static void CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(const NWB::Impl::Material& material){
    EXPECT_NE(material.typedLayoutHash(), 0u);
    EXPECT_EQ(material.typedLayoutBlocks().size(), 1u);
    EXPECT_EQ(material.typedLayoutFields().size(), 5u);
    EXPECT_EQ(material.typedBlockBytes().size(), 20u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    EXPECT_NE(surfaceBlock, nullptr);
    if(!surfaceBlock)
        return;

    EXPECT_EQ(surfaceBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialConstant);
    EXPECT_EQ(surfaceBlock->fieldCount, 5u);
    EXPECT_EQ(surfaceBlock->byteSize, 20u);

    const ExpectedMaterialLayoutField expectedFields[] = {
        { "enabled", NWB::Impl::MaterialLayoutFieldType::Bool4, 0u },
        { "signed_bytes", NWB::Impl::MaterialLayoutFieldType::Char4, 4u },
        { "bytes", NWB::Impl::MaterialLayoutFieldType::UChar4, 8u },
        { "signed_words", NWB::Impl::MaterialLayoutFieldType::Short2, 12u },
        { "words", NWB::Impl::MaterialLayoutFieldType::UShort2, 16u },
    };
    CheckMaterialTypedLayoutFields(material, *surfaceBlock, expectedFields);

    const NWB::Impl::MaterialTypedLayoutField* enabled = FindMaterialTypedLayoutField(material, *surfaceBlock, "enabled");
    if(enabled){
        const u8 enabledDefaults[] = { 1u, 0u, 1u, 0u };
        CheckMaterialTypedLayoutDefaultPODValues(*enabled, enabledDefaults);
    }

    const NWB::Impl::MaterialTypedLayoutField* signedBytes =
        FindMaterialTypedLayoutField(material, *surfaceBlock, "signed_bytes");
    if(signedBytes){
        const u8 signedByteDefaults[] = { 0xffu, 0u, 1u, 127u };
        CheckMaterialTypedLayoutDefaultPODValues(*signedBytes, signedByteDefaults);
    }

    const NWB::Impl::MaterialTypedLayoutField* signedWords =
        FindMaterialTypedLayoutField(material, *surfaceBlock, "signed_words");
    if(signedWords){
        const u16 signedWordDefaults[] = { 0x8000u, 0x7fffu };
        CheckMaterialTypedLayoutDefaultPODValues(*signedWords, signedWordDefaults);
    }

    const ExpectedTypedBlockU8Value expectedU8Values[] = {
        { "surface", 0u, 0u },
        { "surface", 1u, 1u },
        { "surface", s_ExpectedDualCount, 0u },
        { "surface", 3u, 1u },
        { "surface", 4u, 0x80u },
        { "surface", 5u, 0xfeu },
        { "surface", 6u, s_ExpectedDualCount },
        { "surface", 7u, 64u },
        { "surface", 8u, 3u },
        { "surface", 9u, 4u },
        { "surface", 10u, 5u },
        { "surface", 11u, 6u },
    };
    CheckMaterialTypedBlockValues(material, expectedU8Values);

    const ExpectedTypedBlockI16Value expectedI16Values[] = {
        { "surface", 12u, -1234 },
        { "surface", 14u, 2345 },
    };
    CheckMaterialTypedBlockValues(material, expectedI16Values);

    const ExpectedTypedBlockU16Value expectedU16Values[] = {
        { "surface", 16u, 7u },
        { "surface", 18u, 65534u },
    };
    CheckMaterialTypedBlockValues(material, expectedU16Values);
}

static void CheckGeneratedMaterialBindBinaryConstants(
    const AStringView generatedSourceView,
    const NWB::Impl::Material& material
){
    EXPECT_TRUE(ContainsGeneratedUint2Constant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_LAYOUT_HASH",
        material.typedLayoutHash()
    ));
    EXPECT_TRUE(ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_BLOCK_COUNT",
        static_cast<u32>(material.typedLayoutBlocks().size())
    ));
    EXPECT_TRUE(ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_FIELD_COUNT",
        static_cast<u32>(material.typedLayoutFields().size())
    ));
    u32 constantByteSize = 0u;
    u32 mutableByteSize = 0u;
    for(const NWB::Impl::MaterialTypedLayoutBlock& block : material.typedLayoutBlocks()){
        if(block.blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant)
            constantByteSize += block.byteSize;
        else if(block.blockClass == NWB::Impl::MaterialBlockClass::MaterialMutable)
            mutableByteSize += block.byteSize;
    }
    EXPECT_TRUE(ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE",
        constantByteSize
    ));
    EXPECT_TRUE(ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE",
        mutableByteSize
    ));

    const NameHash& materialInterfaceHash = material.materialInterface().hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        AString symbol("NWB_MATERIAL_BIND_INTERFACE_HASH_");
        char laneDigits[16u];
        symbol += FormatDecimal(static_cast<usize>(lane), laneDigits);
        EXPECT_TRUE(ContainsGeneratedUint2Constant(
            generatedSourceView,
            AStringView(symbol.data(), symbol.size()),
            materialInterfaceHash.qwords[lane]
        ));
    }
}

static bool BuildMaterialFromBindAndMeta(
    const AStringView bindText,
    const AStringView materialText,
    const AStringView caseName,
    TestArena& testArena,
    NWB::Impl::Material& outMaterial,
    NWB::Core::Alloc::ScratchArena& scratchArena
){
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    if(!ParseMaterialEntryFromMetaText(materialText, testArena, materialEntry, scratchArena))
        return false;

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot(testArena.arena);
    bool built = false;
    if(AssetsGraphicsFixture::ParseMaterialBindFromText(testArena, bindText, caseName, bindEntry, bindRoot, scratchArena)){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
        bindEntries.push_back(Move(bindEntry));
        NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
        materialEntries.push_back(Move(materialEntry));
        built =
            NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries, scratchArena)
            && NWB::Impl::BuildMaterialAsset(materialEntries[0u], outMaterial)
        ;
    }

    if(!bindRoot.empty()){
        ErrorCode errorCode;
        built = RemoveAllIfExists(bindRoot, errorCode) && built;
    }
    return built;
}

static bool RoundTripMaterialAssetCodec(
    TestArena& testArena,
    NWB::Impl::MaterialAssetCodec& codec,
    const NWB::Impl::Material& material,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
    const bool serialized = codec.serialize(material, binary);
    EXPECT_TRUE(serialized);
    EXPECT_FALSE(binary.empty());
    if(!serialized || binary.empty())
        return false;

    const bool deserialized = codec.deserialize(
        testArena.arena,
        material.virtualPath(),
        binary,
        outLoadedAsset
    );
    EXPECT_TRUE(deserialized);
    EXPECT_NE(outLoadedAsset.get(), nullptr);
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static void SetGeneratedMaterialAvboitPixelShaders(NWB::Impl::Material& material){
    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> accumulatePixelShader;
    accumulatePixelShader.virtualPath = Name("generated/avboit_accumulate_ps/project/materials/test_material");
    material.setAvboitAccumulatePixelShader(accumulatePixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> occupancyPixelShader;
    occupancyPixelShader.virtualPath = Name("generated/avboit_occupancy_ps/project/materials/test_material");
    material.setAvboitOccupancyPixelShader(occupancyPixelShader);

    NWB::Core::Assets::AssetRef<NWB::Impl::Shader> extinctionPixelShader;
    extinctionPixelShader.virtualPath = Name("generated/avboit_extinction_ps/project/materials/test_material");
    material.setAvboitExtinctionPixelShader(extinctionPixelShader);
}

TEST(AssetsGraphics, MaterialBindHalfTypedLayoutValues){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_HalfMaterialBindSource,
        AssetsGraphicsFixture::s_HalfMaterialMeta,
        "material_bind_half_typed_layout_values",
        testArena,
        material,
        scratchArena
    );
    EXPECT_TRUE(built);
    if(built)
        CheckHalfMaterialTypedLayoutAndBlockBytes(material);

    NWB::Impl::Material mixedMaterial(testArena.arena);
    const bool builtMixed = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MixedHalfMaterialBindSource,
        AssetsGraphicsFixture::s_MixedHalfMaterialMeta,
        "material_bind_mixed_half_typed_layout_values",
        testArena,
        mixedMaterial,
        scratchArena
    );
    EXPECT_TRUE(builtMixed);
    if(builtMixed)
        CheckMixedHalfMaterialTypedLayoutAndBlockBytes(mixedMaterial);

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_HalfMaterialBindSource,
        "material_bind_half_generated_text",
        bindEntry,
        bindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedHalfMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    }

    Path mixedBindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry mixedBindEntry(testArena.arena);
    const bool parsedMixed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MixedHalfMaterialBindSource,
        "material_bind_mixed_half_generated_text",
        mixedBindEntry,
        mixedBindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsedMixed);
    if(parsedMixed){
        mixedBindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            mixedBindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedMixedHalfMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(bindRoot, errorCode));
    EXPECT_TRUE(RemoveAllIfExists(mixedBindRoot, errorCode));
}

TEST(AssetsGraphics, MaterialBindCompactIntegerTypedLayoutValues){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_CompactIntegerMaterialBindSource,
        AssetsGraphicsFixture::s_CompactIntegerMaterialMeta,
        "material_bind_compact_integer_typed_layout_values",
        testArena,
        material,
        scratchArena
    );
    EXPECT_TRUE(built);
    if(built)
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(material);

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_CompactIntegerMaterialBindSource,
        "material_bind_compact_integer_generated_text",
        bindEntry,
        bindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedCompactIntegerMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(bindRoot, errorCode));
}

TEST(AssetsGraphics, MaterialMetadataInterfaceAndBlockParameters){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    const bool parsed = ParseMaterialEntryFromMetaText(AssetsGraphicsFixture::s_BlockScopedMaterialMeta, testArena, materialEntry, scratchArena);
    EXPECT_TRUE(parsed);
    if(!parsed)
        return;

    EXPECT_EQ(AStringView(materialEntry.materialInterface), AStringView("project/material_interfaces/test_surface"));
    EXPECT_EQ(materialEntry.parameters.size(), 3u);

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot(testArena.arena);
    const bool parsedBind = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_meta_bind_validation",
        bindEntry,
        bindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsedBind);
    if(!parsedBind)
        return;
    bindEntry.virtualPath = "project/material_interfaces/test_surface";

    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    bindEntries.push_back(Move(bindEntry));
    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
    materialEntries.push_back(Move(materialEntry));
    const bool validated = NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries, scratchArena);
    EXPECT_TRUE(validated);
    if(!validated)
        return;

    NWB::Impl::Material material(testArena.arena);
    const bool built = NWB::Impl::BuildMaterialAsset(materialEntries[0u], material);
    EXPECT_TRUE(built);
    if(!built)
        return;

    EXPECT_FALSE(material.transparent());
    EXPECT_FALSE(material.twoSided());
    EXPECT_FALSE(material.refractive());
    EXPECT_EQ(material.materialInterface(), Name("project/material_interfaces/test_surface"));

    NWB::Impl::Material twoSidedMaterial(testArena.arena);
    EXPECT_TRUE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_TwoSidedMaterialMeta,
        "material_meta_explicit_two_sided",
        testArena,
        twoSidedMaterial,
        scratchArena
    ));
    EXPECT_TRUE(twoSidedMaterial.twoSided());
    EXPECT_FALSE(twoSidedMaterial.transparent());
    EXPECT_FALSE(twoSidedMaterial.refractive());

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, MaterialCookRejectsMissingAvboitPixelShaders){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);

    NWB::Impl::Material transparentMaterial(testArena.arena);
    EXPECT_FALSE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_TransparentMaterialMeta,
        "material_cook_missing_avboit_transparent",
        testArena,
        transparentMaterial,
        scratchArena
    ));

    NWB::Impl::Material refractiveMaterial(testArena.arena);
    EXPECT_FALSE(BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_RefractiveMaterialMeta,
        "material_cook_missing_avboit_refractive",
        testArena,
        refractiveMaterial,
        scratchArena
    ));

    EXPECT_EQ(logger.errorCount(), s_ExpectedDualCount);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "AVBOIT pixel shaders must be present if and only if it is transparent"
    )));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingShaderVariant){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_MissingShaderVariantMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingShaderVariantMaterialMeta, testArena, materialEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("field 'shader_variant' is required")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsMissingRenderProperties){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_MissingRefractiveMaterialMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    EXPECT_FALSE(ParseMaterialEntryFromMetaText(s_MissingRefractiveMaterialMeta, testArena, materialEntry, scratchArena));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("'refractive' is required and must be 0 or 1")));
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsExplicitOpticalStages){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    const auto expectRejected = [&](const AStringView metaText){
        NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, materialEntry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "explicit 'shaders' cannot be used with transparent/refractive materials"
        )));
    };

    expectRejected(AssetsGraphicsFixture::s_ExplicitTransparentMaterialMeta);
    expectRejected(AssetsGraphicsFixture::s_ExplicitRefractiveMaterialMeta);
#endif
}

TEST(AssetsGraphics, MaterialMetadataRejectsEngineRootedPolicySelectors){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_EngineRootedInterfaceMeta = R"NWB_META(material asset;

asset.interface = "engine/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedSurfaceMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.surface = "engine/shaders/surface.surface";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedBxdfMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "engine/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedStageShaderMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";
asset.transparent = 0;
asset.two_sided = 0;
asset.refractive = 0;

asset.shaders = {
    "mesh": "engine/graphics/mesh/shared_ms",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    const auto expectRejected = [&](const AStringView metaText, const TStringView expectedError){
        NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
        EXPECT_FALSE(ParseMaterialEntryFromMetaText(metaText, testArena, materialEntry, scratchArena));
        EXPECT_TRUE(logger.sawErrorContaining(expectedError));
    };

    expectRejected(s_EngineRootedInterfaceMeta, NWB_TEXT("interface must use the project/ virtual root"));
    expectRejected(s_EngineRootedSurfaceMeta, NWB_TEXT("field 'surface' must use the project/ virtual root"));
    expectRejected(s_EngineRootedBxdfMeta, NWB_TEXT("field 'bxdf' must use the project/ virtual root"));
    expectRejected(s_EngineRootedStageShaderMeta, NWB_TEXT("shader stage 'mesh' must use the project/ virtual root"));
    EXPECT_EQ(logger.errorCount(), 4u);
#endif
}

TEST(AssetsGraphics, MaterialCodecTypedLayoutBoundary){
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);

    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        const bool built = BuildMaterialFromBindAndMeta(
            AssetsGraphicsFixture::s_MinimalMaterialBindSource,
            AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
            "material_codec_typed_layout_boundary",
            testArena,
            material,
            scratchArena
        );
        EXPECT_TRUE(built);
        if(!built)
            return;

        NWB::Impl::MaterialAssetCodec codec;
        material.setRefractive(true);
        EXPECT_TRUE(NWB::Impl::HasValidMaterialAvboitPixelShaderContract(
            material.transparent(),
            material.avboitAccumulatePixelShader(),
            material.avboitOccupancyPixelShader(),
            material.avboitExtinctionPixelShader()
        ));
        UniquePtr<NWB::Core::Assets::IAsset> opaqueRefractiveLoadedAsset;
        if(RoundTripMaterialAssetCodec(testArena, codec, material, opaqueRefractiveLoadedAsset)){
            const NWB::Impl::Material& opaqueRefractiveMaterial =
                static_cast<const NWB::Impl::Material&>(*opaqueRefractiveLoadedAsset);
            EXPECT_FALSE(opaqueRefractiveMaterial.transparent());
            EXPECT_TRUE(opaqueRefractiveMaterial.refractive());
            EXPECT_TRUE(NWB::Impl::HasValidMaterialAvboitPixelShaderContract(
                opaqueRefractiveMaterial.transparent(),
                opaqueRefractiveMaterial.avboitAccumulatePixelShader(),
                opaqueRefractiveMaterial.avboitOccupancyPixelShader(),
                opaqueRefractiveMaterial.avboitExtinctionPixelShader()
            ));
        }

        material.setTransparent(true);

#if defined(NWB_FINAL)
        NWB::Core::Assets::AssetBytes invalidBinary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_FALSE(codec.serialize(material, invalidBinary));
        EXPECT_TRUE(invalidBinary.empty());
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "AVBOIT pixel shaders must be present if and only if the material is transparent"
        )));
#endif

        SetGeneratedMaterialAvboitPixelShaders(material);
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(RoundTripMaterialAssetCodec(testArena, codec, material, loadedAsset)){
            const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            EXPECT_EQ(loadedMaterial.materialInterface(), material.materialInterface());
            EXPECT_EQ(loadedMaterial.typedLayoutHash(), material.typedLayoutHash());
            EXPECT_TRUE(loadedMaterial.transparent());
            EXPECT_TRUE(loadedMaterial.refractive());
            CheckMinimalMaterialTypedLayout(loadedMaterial);
            CheckMinimalMaterialTypedBlockBytes(loadedMaterial);
        }

        NWB::Impl::Material halfMaterial(testArena.arena);
        const bool builtHalfMaterial = BuildMaterialFromBindAndMeta(
            AssetsGraphicsFixture::s_HalfMaterialBindSource,
            AssetsGraphicsFixture::s_HalfMaterialMeta,
            "material_codec_half_typed_layout",
            testArena,
            halfMaterial,
            scratchArena
        );
        EXPECT_TRUE(builtHalfMaterial);
        UniquePtr<NWB::Core::Assets::IAsset> loadedHalfAsset;
        if(builtHalfMaterial && RoundTripMaterialAssetCodec(testArena, codec, halfMaterial, loadedHalfAsset)){
            const NWB::Impl::Material& loadedHalfMaterial = static_cast<const NWB::Impl::Material&>(*loadedHalfAsset);
            EXPECT_EQ(loadedHalfMaterial.materialInterface(), halfMaterial.materialInterface());
            EXPECT_EQ(loadedHalfMaterial.typedLayoutHash(), halfMaterial.typedLayoutHash());
            CheckHalfMaterialTypedLayoutAndBlockBytes(loadedHalfMaterial);
        }

        NWB::Impl::Material mixedHalfMaterial(testArena.arena);
        const bool builtMixedHalfMaterial = BuildMaterialFromBindAndMeta(
            AssetsGraphicsFixture::s_MixedHalfMaterialBindSource,
            AssetsGraphicsFixture::s_MixedHalfMaterialMeta,
            "material_codec_mixed_half_typed_layout",
            testArena,
            mixedHalfMaterial,
            scratchArena
        );
        EXPECT_TRUE(builtMixedHalfMaterial);
        UniquePtr<NWB::Core::Assets::IAsset> loadedMixedHalfAsset;
        if(
            builtMixedHalfMaterial
            && RoundTripMaterialAssetCodec(testArena, codec, mixedHalfMaterial, loadedMixedHalfAsset)
        ){
            const NWB::Impl::Material& loadedMixedHalfMaterial =
                static_cast<const NWB::Impl::Material&>(*loadedMixedHalfAsset);
            EXPECT_EQ(loadedMixedHalfMaterial.materialInterface(), mixedHalfMaterial.materialInterface());
            EXPECT_EQ(loadedMixedHalfMaterial.typedLayoutHash(), mixedHalfMaterial.typedLayoutHash());
            CheckMixedHalfMaterialTypedLayoutAndBlockBytes(loadedMixedHalfMaterial);
        }

#if defined(NWB_FINAL)
        EXPECT_EQ(logger.errorCount(), 1u);
#else
        EXPECT_EQ(logger.errorCount(), 0u);
#endif
    }

#if defined(NWB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = AssetsGraphicsFixture::MakeAssetBytes(testArena);
        EXPECT_TRUE(codec.serialize(material, binary));

        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
        EXPECT_TRUE(AssetsGraphicsFixture::FindMaterialBinaryTypedLayoutOffsets(
            binary,
            layoutHashOffset,
            blockByteCountOffset
        ));

        NWB::Core::Assets::AssetBytes hashMismatchBinary = binary;
        const u64 invalidLayoutHash = material.typedLayoutHash() == Limit<u64>::s_Max ? material.typedLayoutHash() - 1u : material.typedLayoutHash() + 1u;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(hashMismatchBinary, layoutHashOffset, invalidLayoutHash));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), hashMismatchBinary);

        NWB::Core::Assets::AssetBytes byteSizeMismatchBinary = binary;
        EXPECT_FALSE(material.typedBlockBytes().empty());
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(
            byteSizeMismatchBinary,
            blockByteCountOffset,
            static_cast<u32>(material.typedBlockBytes().size() - 1u)
        ));
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        constexpr usize s_AvboitPixelShaderBinaryBytes = sizeof(u32) + sizeof(NameHash);
        ASSERT_GE(binary.size(), s_AvboitPixelShaderBinaryBytes * 3u);
        const usize occupancyPresenceOffset = binary.size() - s_AvboitPixelShaderBinaryBytes * s_ExpectedDualCount;
        NWB::Core::Assets::AssetBytes missingOccupancyBinary = binary;
        EXPECT_TRUE(AssetsGraphicsFixture::OverwritePOD(missingOccupancyBinary, occupancyPresenceOffset, static_cast<u32>(0u)));
        missingOccupancyBinary.erase(
            missingOccupancyBinary.begin() + occupancyPresenceOffset + sizeof(u32),
            missingOccupancyBinary.begin() + occupancyPresenceOffset + s_AvboitPixelShaderBinaryBytes
        );
        AssetsGraphicsFixture::CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), missingOccupancyBinary);

        EXPECT_EQ(logger.errorCount(), 3u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "typed block byte count does not match typed layout"
        )));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "AVBOIT pixel shaders must be present if and only if the material is transparent"
        )));
    }
#endif
}

TEST(AssetsGraphics, MaterialBindSchemaValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    Path root(testArena.arena);
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_schema_valid",
        entry,
        root,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        EXPECT_EQ(entry.structs.size(), s_ExpectedDualCount);
        EXPECT_EQ(entry.instances.size(), s_ExpectedDualCount);

        const NWB::Impl::MaterialBindStruct* surfaceStruct = entry.findStruct("NwbTestSurfaceMaterial");
        const NWB::Impl::MaterialBindStruct* runtimeStruct = entry.findStruct("NwbTestRuntimeMaterial");
        EXPECT_NE(surfaceStruct, nullptr);
        EXPECT_NE(runtimeStruct, nullptr);
        if(surfaceStruct){
            CheckMaterialBindStructBlockClass(
                *surfaceStruct,
                NWB::Impl::MaterialBlockClass::MaterialConstant
            );
            EXPECT_EQ(surfaceStruct->fields.size(), 5u);

            const NWB::Impl::MaterialBindField* baseColorField = surfaceStruct->findField("base_color");
            EXPECT_NE(baseColorField, nullptr);
            if(baseColorField){
                EXPECT_EQ(AStringView(baseColorField->type), "float4");
                const NWB::Impl::MaterialBindAttribute* defaultAttribute = baseColorField->findAttribute("default");
                ASSERT_NE(defaultAttribute, nullptr);
                ASSERT_EQ(defaultAttribute->arguments.size(), 1u);
                EXPECT_EQ(AStringView(defaultAttribute->arguments[0u]), "float4(1.0, 1.0, 1.0, 1.0)");
            }
        }
        if(runtimeStruct){
            CheckMaterialBindStructBlockClass(
                *runtimeStruct,
                NWB::Impl::MaterialBlockClass::MaterialMutable
            );
            EXPECT_EQ(runtimeStruct->fields.size(), 1u);
        }

        const NWB::Impl::MaterialBindInstance* surfaceInstance = entry.findInstance("surface");
        const NWB::Impl::MaterialBindInstance* runtimeInstance = entry.findInstance("runtime");
        EXPECT_NE(surfaceInstance, nullptr);
        EXPECT_NE(runtimeInstance, nullptr);
        if(surfaceInstance)
            EXPECT_EQ(AStringView(surfaceInstance->type), "NwbTestSurfaceMaterial");
        if(runtimeInstance)
            EXPECT_EQ(AStringView(runtimeInstance->type), "NwbTestRuntimeMaterial");
    }

    Path halfRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry halfEntry(testArena.arena);
    const bool parsedHalf = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_HalfMaterialBindSource,
        "material_bind_half_schema_valid",
        halfEntry,
        halfRoot,
        scratchArena
    );
    EXPECT_TRUE(parsedHalf);
    if(parsedHalf){
        const NWB::Impl::MaterialBindStruct* halfStruct = halfEntry.findStruct("NwbTestSurfaceMaterial");
        EXPECT_NE(halfStruct, nullptr);
        if(halfStruct){
            EXPECT_NE(halfStruct->findField("roughness"), nullptr);
            EXPECT_NE(halfStruct->findField("range"), nullptr);
            EXPECT_NE(halfStruct->findField("tint"), nullptr);
            EXPECT_NE(halfStruct->findField("base_color"), nullptr);
        }
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    EXPECT_TRUE(RemoveAllIfExists(halfRoot, errorCode));

    auto expectParseFailure = [&](
        const AStringView bindText,
        const AStringView caseName,
        const TStringView expectedError
    ){
        SCOPED_TRACE(caseName.data());

        Path invalidRoot(testArena.arena);
        NWB::Impl::MaterialBindEntry invalidEntry(testArena.arena);
        {
            CapturingLogger failureLogger;
            NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
                failureLogger,
                NWB::Core::Common::LoggerBreakPolicy::ReportOnly
            );

            EXPECT_FALSE(AssetsGraphicsFixture::ParseMaterialBindFromText(
                testArena,
                bindText,
                caseName,
                invalidEntry,
                invalidRoot,
                scratchArena
            ));
            EXPECT_TRUE(failureLogger.sawErrorContaining(expectedError));
        }

        ErrorCode removeErrorCode;
        EXPECT_TRUE(RemoveAllIfExists(invalidRoot, removeErrorCode));
    };

    expectParseFailure(
        AssetsGraphicsFixture::s_UnknownBlockClassMaterialBindSource,
        "material_bind_unknown_block_class",
        NWB_TEXT("unsupported attribute 'material_project'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_UnsupportedFieldTypeMaterialBindSource,
        "material_bind_unsupported_field_type",
        NWB_TEXT("unsupported type 'double'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InvalidDefaultMaterialBindSource,
        "material_bind_invalid_default",
        NWB_TEXT("attribute 'default' requires one non-empty string argument")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_MissingDefaultMaterialBindSource,
        "material_bind_missing_default",
        NWB_TEXT("must declare a default attribute")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_ResourceAttributeMaterialBindSource,
        "material_bind_resource_attribute",
        NWB_TEXT("has unsupported attribute 'texture_asset'")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_DuplicateInstanceMaterialBindSource,
        "material_bind_duplicate_instance",
        NWB_TEXT("duplicate struct instance declaration")
    );
    expectParseFailure(
        AssetsGraphicsFixture::s_InstanceOverrideMaterialBindSource,
        "material_bind_instance_override",
        NWB_TEXT("unsupported asset field 'instance_override'")
    );

    Path float1DefaultRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry float1DefaultEntry(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_Float1DefaultMaterialBindSource,
        "material_bind_float1_default",
        float1DefaultEntry,
        float1DefaultRoot,
        scratchArena
    ));
    float1DefaultEntry.virtualPath = "project/material_interfaces/test_surface";
    {
        SCOPED_TRACE("material_bind_float1_default_include");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
            failureLogger,
            NWB::Core::Common::LoggerBreakPolicy::ReportOnly
        );

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_FALSE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            float1DefaultEntry,
            generatedSource,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));
    }

    const Name cacheInterface("project/material_interfaces/test_surface");
    NWB::Impl::MaterialBindTypedLayoutCache layoutCache(testArena.arena);
    const NWB::Impl::MaterialBindTypedLayout* cachedLayout = nullptr;
    {
        SCOPED_TRACE("material_bind_float1_default_cache");

        CapturingLogger failureLogger;
        NWB::Core::Common::LoggerRegistrationGuard failureLoggerRegistrationGuard(
            failureLogger,
            NWB::Core::Common::LoggerBreakPolicy::ReportOnly
        );

        EXPECT_FALSE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(
            cacheInterface,
            float1DefaultEntry,
            layoutCache,
            cachedLayout,
            scratchArena
        ));
        EXPECT_TRUE(failureLogger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));
    }
    EXPECT_EQ(cachedLayout, nullptr);
    EXPECT_TRUE(layoutCache.entries.empty());
    EXPECT_TRUE(layoutCache.lookup.empty());

    Path validCacheRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry validCacheEntry(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_cache_valid_after_failed_layout",
        validCacheEntry,
        validCacheRoot,
        scratchArena
    ));
    validCacheEntry.virtualPath = "project/material_interfaces/test_surface";
    EXPECT_TRUE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(
        cacheInterface,
        validCacheEntry,
        layoutCache,
        cachedLayout,
        scratchArena
    ));
    EXPECT_NE(cachedLayout, nullptr);
    EXPECT_EQ(layoutCache.entries.size(), 1u);
    EXPECT_EQ(layoutCache.lookup.size(), 1u);
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode removeErrorCode;
    EXPECT_TRUE(RemoveAllIfExists(float1DefaultRoot, removeErrorCode));
    EXPECT_TRUE(RemoveAllIfExists(validCacheRoot, removeErrorCode));
}

TEST(AssetsGraphics, MaterialBindGeneratedSlangText){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    Path root(testArena.arena);
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_generated_text",
        entry,
        root,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        entry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            entry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}


TEST(AssetsGraphics, MaterialBindEngineAndProjectResourcePaths){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_AssetResourceMaterialBindSource,
        AssetsGraphicsFixture::s_AssetResourceMaterialMeta,
        "material_bind_asset_resource",
        testArena,
        material,
        scratchArena
    );
    EXPECT_TRUE(built);
    if(built){
        const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
        ASSERT_NE(surfaceBlock, nullptr);
        EXPECT_EQ(surfaceBlock->blockClass, NWB::Impl::MaterialBlockClass::MaterialConstant);
        EXPECT_EQ(surfaceBlock->byteSize, 24u);
        CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "base_color_map",
            NWB::Impl::MaterialLayoutFieldType::SampledImage2D,
            16u
        );
        CheckMaterialTypedLayoutField(
            material,
            *surfaceBlock,
            "base_color_sampler",
            NWB::Impl::MaterialLayoutFieldType::Sampler,
            20u
        );

        ASSERT_EQ(material.resourceReferences().size(), s_ExpectedDualCount);
        const NWB::Impl::MaterialResourceReference& imageReference = material.resourceReferences()[0u];
        const NWB::Impl::MaterialResourceReference& samplerReference = material.resourceReferences()[1u];
        EXPECT_EQ(imageReference.blockName, Name("surface"));
        EXPECT_EQ(imageReference.fieldName, Name("base_color_map"));
        EXPECT_EQ(imageReference.textureAsset.name(), Name("project/textures/test_checker"));
        EXPECT_FALSE(imageReference.samplerAsset.valid());
        EXPECT_EQ(imageReference.resourceKind, NWB::Impl::MaterialResourceKind::SampledImage2D);
        EXPECT_EQ(imageReference.resourceSource, NWB::Impl::MaterialResourceSource::Asset);
        EXPECT_EQ(imageReference.constantByteOffset, 16u);
        EXPECT_EQ(samplerReference.blockName, Name("surface"));
        EXPECT_EQ(samplerReference.fieldName, Name("base_color_sampler"));
        EXPECT_FALSE(samplerReference.textureAsset.valid());
        EXPECT_EQ(samplerReference.samplerAsset.name(), Name("engine/samplers/linear_clamp"));
        EXPECT_EQ(samplerReference.resourceKind, NWB::Impl::MaterialResourceKind::Sampler);
        EXPECT_EQ(samplerReference.resourceSource, NWB::Impl::MaterialResourceSource::Asset);
        EXPECT_FALSE(imageReference.fixtureName);
        EXPECT_FALSE(samplerReference.fixtureName);
        EXPECT_EQ(samplerReference.constantByteOffset, 20u);

        NWB::Impl::MaterialAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(RoundTripMaterialAssetCodec(testArena, codec, material, loadedAsset)){
            const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            ASSERT_EQ(loadedMaterial.resourceReferences().size(), s_ExpectedDualCount);
            EXPECT_EQ(loadedMaterial.resourceReferences()[0u].textureAsset, imageReference.textureAsset);
            EXPECT_FALSE(loadedMaterial.resourceReferences()[0u].samplerAsset.valid());
            EXPECT_EQ(loadedMaterial.resourceReferences()[0u].resourceSource, imageReference.resourceSource);
            EXPECT_EQ(loadedMaterial.resourceReferences()[0u].constantByteOffset, imageReference.constantByteOffset);
            EXPECT_FALSE(loadedMaterial.resourceReferences()[1u].textureAsset.valid());
            EXPECT_EQ(loadedMaterial.resourceReferences()[1u].samplerAsset, samplerReference.samplerAsset);
            EXPECT_EQ(loadedMaterial.resourceReferences()[1u].resourceSource, samplerReference.resourceSource);
            EXPECT_EQ(loadedMaterial.resourceReferences()[1u].constantByteOffset, samplerReference.constantByteOffset);
        }
    }

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_AssetResourceMaterialBindSource,
        "material_bind_asset_resource_generated",
        bindEntry,
        bindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";
        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
        const AStringView expectedSnippets[] = {
            "Texture2D<float4> nwbMaterialBindLoadSurfaceBaseColorMap",
            "SamplerState nwbMaterialBindLoadSurfaceBaseColorSampler",
            "NwbHeapSampledImage2DNonUniform(nwbMaterialLoadConstantUInt(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_MAP_BYTE_OFFSET))",
            "NwbHeapSamplerNonUniform(nwbMaterialLoadConstantUInt(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_SAMPLER_BYTE_OFFSET))",
            "float4 base_color;",
        };
        CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
        EXPECT_FALSE(ContainsText(generatedSourceView, "texture2d base_color_map"));
        EXPECT_FALSE(ContainsText(generatedSourceView, "value.base_color_map"));
        EXPECT_FALSE(ContainsText(generatedSourceView, "value.base_color_sampler"));
    }

    EXPECT_EQ(logger.errorCount(), 0u);
    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(bindRoot, errorCode));
}


TEST(AssetsGraphics, MaterialBindStaticResourceFixtures){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_StaticResourceFixtureMaterialBindSource,
        AssetsGraphicsFixture::s_StaticResourceFixtureMaterialMeta,
        "material_bind_static_resource_fixture",
        testArena,
        material,
        scratchArena
    );
    EXPECT_TRUE(built);
    if(built){
        ASSERT_EQ(material.resourceReferences().size(), s_ExpectedDualCount);
        const NWB::Impl::MaterialResourceReference& imageReference = material.resourceReferences()[0u];
        const NWB::Impl::MaterialResourceReference& samplerReference = material.resourceReferences()[1u];
        EXPECT_EQ(imageReference.blockName, Name("surface"));
        EXPECT_EQ(imageReference.fieldName, Name("base_color_map"));
        EXPECT_FALSE(imageReference.textureAsset.valid());
        EXPECT_EQ(imageReference.resourceKind, NWB::Impl::MaterialResourceKind::SampledImage2D);
        EXPECT_EQ(imageReference.fixtureName, Name(NWB::Impl::MaterialResourceFixture::s_CheckerRgba8));
        EXPECT_EQ(imageReference.constantByteOffset, 0u);
        EXPECT_EQ(samplerReference.blockName, Name("surface"));
        EXPECT_EQ(samplerReference.fieldName, Name("base_color_sampler"));
        EXPECT_FALSE(samplerReference.samplerAsset.valid());
        EXPECT_EQ(samplerReference.resourceKind, NWB::Impl::MaterialResourceKind::Sampler);
        EXPECT_EQ(samplerReference.fixtureName, Name(NWB::Impl::MaterialResourceFixture::s_LinearClamp));
        EXPECT_EQ(samplerReference.constantByteOffset, 4u);

        NWB::Impl::MaterialAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(RoundTripMaterialAssetCodec(testArena, codec, material, loadedAsset)){
            const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            ASSERT_EQ(loadedMaterial.resourceReferences().size(), s_ExpectedDualCount);
            EXPECT_EQ(loadedMaterial.resourceReferences()[0u].fixtureName, imageReference.fixtureName);
            EXPECT_EQ(loadedMaterial.resourceReferences()[1u].fixtureName, samplerReference.fixtureName);
            EXPECT_EQ(loadedMaterial.resourceReferences()[0u].constantByteOffset, imageReference.constantByteOffset);
            EXPECT_EQ(loadedMaterial.resourceReferences()[1u].constantByteOffset, samplerReference.constantByteOffset);
        }
    }

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = AssetsGraphicsFixture::ParseMaterialBindFromText(
        testArena,
        AssetsGraphicsFixture::s_StaticResourceFixtureMaterialBindSource,
        "material_bind_static_resource_fixture_generated",
        bindEntry,
        bindRoot,
        scratchArena
    );
    EXPECT_TRUE(parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";
        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        EXPECT_TRUE(NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
        const AStringView expectedSnippets[] = {
            "Texture2D<float4> nwbMaterialBindLoadSurfaceBaseColorMap",
            "SamplerState nwbMaterialBindLoadSurfaceBaseColorSampler",
            "NwbHeapSampledImage2DNonUniform(nwbMaterialLoadConstantUInt(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_MAP_BYTE_OFFSET))",
            "NwbHeapSamplerNonUniform(nwbMaterialLoadConstantUInt(instance, NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_SAMPLER_BYTE_OFFSET))",
        };
        CheckGeneratedSourceContainsAll(generatedSourceView, expectedSnippets);
        EXPECT_FALSE(ContainsText(generatedSourceView, "float4 base_color;"));
        EXPECT_FALSE(ContainsText(generatedSourceView, "texture2d base_color_map"));
        EXPECT_FALSE(ContainsText(generatedSourceView, "value.base_color_map"));
        EXPECT_FALSE(ContainsText(generatedSourceView, "value.base_color_sampler"));
    }

    EXPECT_EQ(logger.errorCount(), 0u);
    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(bindRoot, errorCode));
}


TEST(AssetsGraphics, MaterialBindEngineAndProjectResourceValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialScratchArena);
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::SampledImage2D,
        NWB::Impl::MaterialResourceSource::Asset,
        "builtin/material_fixture/checker_rgba8"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "project/samplers/../linear_clamp"
    ));
    EXPECT_FALSE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "engine/samplers/../linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::SampledImage2D,
        NWB::Impl::MaterialResourceSource::Asset,
        "project/materials/smoke_pattern"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "project/samplers/linear_clamp"
    ));
    EXPECT_TRUE(NWB::Impl::IsSupportedMaterialResourceReference(
        NWB::Impl::MaterialResourceKind::Sampler,
        NWB::Impl::MaterialResourceSource::Asset,
        "engine/samplers/linear_clamp"
    ));

    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        AssetsGraphicsFixture::s_SecondAssetResourceMaterialBindSource,
        AssetsGraphicsFixture::s_AssetResourceMaterialMeta,
        "material_bind_asset_resource_validation",
        testArena,
        material,
        scratchArena
    );
    ASSERT_TRUE(built);
    ASSERT_EQ(material.resourceReferences().size(), s_ExpectedDualCount);
    const NWB::Impl::MaterialResourceReference& imageReference = material.resourceReferences()[0u];
    EXPECT_EQ(imageReference.textureAsset.name(), Name("project/textures/test_checker"));
    EXPECT_FALSE(imageReference.samplerAsset.valid());
    EXPECT_EQ(imageReference.resourceKind, NWB::Impl::MaterialResourceKind::SampledImage2D);
    EXPECT_EQ(imageReference.resourceSource, NWB::Impl::MaterialResourceSource::Asset);
    EXPECT_EQ(imageReference.constantByteOffset, 16u);

    NWB::Impl::MaterialAssetCodec codec;
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    ASSERT_TRUE(RoundTripMaterialAssetCodec(testArena, codec, material, loadedAsset));
    const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    ASSERT_EQ(loadedMaterial.resourceReferences().size(), s_ExpectedDualCount);
    EXPECT_EQ(loadedMaterial.resourceReferences()[0u].textureAsset, imageReference.textureAsset);
    EXPECT_FALSE(loadedMaterial.resourceReferences()[0u].samplerAsset.valid());
    EXPECT_EQ(loadedMaterial.resourceReferences()[0u].resourceSource, imageReference.resourceSource);
    EXPECT_EQ(logger.errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetsGraphics, MaterialBindCookIntegration){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        "material_bind_material_integration",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    const Path generatedCsgIncludeRoot = root / "cache" / "tests" / "csg_modules";
    const Path generatedCsgBuiltInIncludePath = generatedCsgIncludeRoot / "engine" / "csg" / "generated" / "built_in.slangi";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    CheckGeneratedMaterialBindSource(AStringView(generatedSource.data(), generatedSource.size()));
    NWB::Impl::ShaderCook::CookString generatedCsgSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedCsgBuiltInIncludePath, generatedCsgSource));

    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    includeDirectories.push_back(generatedCsgIncludeRoot);
    includeDirectories.push_back(AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets" / "graphics");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialCookScratchArena);
    EXPECT_TRUE(shaderCook.gatherShaderDependencies(
        root / "assets" / "shaders" / "material_ps.slang",
        includeDirectories,
        dependencies,
        scratchArena
    ));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedIncludePath));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedCsgBuiltInIncludePath));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        EXPECT_EQ(loadedAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        EXPECT_EQ(material.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckMinimalMaterialTypedLayout(material);
        CheckMinimalMaterialTypedBlockBytes(material);
        CheckGeneratedMaterialBindBinaryConstants(
            AStringView(generatedSource.data(), generatedSource.size()),
            material
        );
    }

    Path halfRoot(testArena.arena);
    Path halfOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
        AssetsGraphicsFixture::s_HalfMaterialBindSource,
        AssetsGraphicsFixture::s_HalfMaterialMeta,
        AssetsGraphicsFixture::s_HalfMaterialBindShaderProbeSource,
        "material_bind_half_material_integration",
        testArena,
        halfRoot,
        halfOutputDirectory
    ));

    const Path halfGeneratedIncludePath =
        halfRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString halfGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(halfGeneratedIncludePath, halfGeneratedSource));
    const AStringView halfGeneratedSourceView(halfGeneratedSource.data(), halfGeneratedSource.size());
    CheckGeneratedHalfMaterialBindSource(halfGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedHalfAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        halfOutputDirectory,
        Name("project/materials/test_material"),
        loadedHalfAsset
    ));
    if(loadedHalfAsset){
        EXPECT_EQ(loadedHalfAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& halfMaterial = static_cast<const NWB::Impl::Material&>(*loadedHalfAsset);
        EXPECT_EQ(halfMaterial.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckHalfMaterialTypedLayoutAndBlockBytes(halfMaterial);
        CheckGeneratedMaterialBindBinaryConstants(halfGeneratedSourceView, halfMaterial);
    }

    Path compactRoot(testArena.arena);
    Path compactOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
        AssetsGraphicsFixture::s_CompactIntegerMaterialBindSource,
        AssetsGraphicsFixture::s_CompactIntegerMaterialMeta,
        AssetsGraphicsFixture::s_CompactIntegerMaterialBindShaderProbeSource,
        "material_bind_compact_integer_material_integration",
        testArena,
        compactRoot,
        compactOutputDirectory
    ));

    const Path compactGeneratedIncludePath =
        compactRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString compactGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(compactGeneratedIncludePath, compactGeneratedSource));
    const AStringView compactGeneratedSourceView(compactGeneratedSource.data(), compactGeneratedSource.size());
    CheckGeneratedCompactIntegerMaterialBindSource(compactGeneratedSourceView);

    UniquePtr<NWB::Core::Assets::IAsset> loadedCompactAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        compactOutputDirectory,
        Name("project/materials/test_material"),
        loadedCompactAsset
    ));
    if(loadedCompactAsset){
        EXPECT_EQ(loadedCompactAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& compactMaterial = static_cast<const NWB::Impl::Material&>(*loadedCompactAsset);
        EXPECT_EQ(compactMaterial.materialInterface(), Name("project/material_interfaces/test_surface"));
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(compactMaterial);
        CheckGeneratedMaterialBindBinaryConstants(compactGeneratedSourceView, compactMaterial);
    }

    Path resourceRoot(testArena.arena);
    Path resourceOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
        AssetsGraphicsFixture::s_AssetResourceMaterialBindSource,
        AssetsGraphicsFixture::s_AssetResourceMaterialMeta,
        AssetsGraphicsFixture::s_AssetResourceShaderProbeSource,
        "material_bind_asset_resource_integration",
        testArena,
        resourceRoot,
        resourceOutputDirectory
    ));

    const Path resourceGeneratedIncludePath =
        resourceRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString resourceGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(resourceGeneratedIncludePath, resourceGeneratedSource));
    const AStringView resourceGeneratedSourceView(resourceGeneratedSource.data(), resourceGeneratedSource.size());
    const AStringView resourceGeneratedSnippets[] = {
        "Texture2D<float4> nwbMaterialBindLoadSurfaceBaseColorMap",
        "SamplerState nwbMaterialBindLoadSurfaceBaseColorSampler",
        "NwbHeapSampledImage2DNonUniform",
        "NwbHeapSamplerNonUniform",
    };
    CheckGeneratedSourceContainsAll(resourceGeneratedSourceView, resourceGeneratedSnippets);

    UniquePtr<NWB::Core::Assets::IAsset> loadedResourceAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        resourceOutputDirectory,
        Name("project/materials/test_material"),
        loadedResourceAsset
    ));
    if(loadedResourceAsset){
        EXPECT_EQ(loadedResourceAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& resourceMaterial = static_cast<const NWB::Impl::Material&>(*loadedResourceAsset);
        ASSERT_EQ(resourceMaterial.resourceReferences().size(), s_ExpectedDualCount);
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[0u].textureAsset.name(),
            Name("project/textures/test_checker")
        );
        EXPECT_FALSE(resourceMaterial.resourceReferences()[0u].samplerAsset.valid());
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[0u].resourceSource,
            NWB::Impl::MaterialResourceSource::Asset
        );
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[1u].samplerAsset.name(),
            Name("engine/samplers/linear_clamp")
        );
        EXPECT_FALSE(resourceMaterial.resourceReferences()[1u].textureAsset.valid());
        EXPECT_EQ(
            resourceMaterial.resourceReferences()[1u].resourceSource,
            NWB::Impl::MaterialResourceSource::Asset
        );
    }

    Path fixtureRoot(testArena.arena);
    Path fixtureOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegrationWithPixelSource(
        AssetsGraphicsFixture::s_StaticResourceFixtureMaterialBindSource,
        AssetsGraphicsFixture::s_StaticResourceFixtureMaterialMeta,
        AssetsGraphicsFixture::s_StaticResourceFixtureShaderProbeSource,
        "material_bind_static_resource_fixture_integration",
        testArena,
        fixtureRoot,
        fixtureOutputDirectory
    ));

    const Path fixtureGeneratedIncludePath =
        fixtureRoot / "cache" / "tests" / "material_bind_includes"
        / "project" / "material_interfaces" / "test_surface.bind"
    ;
    NWB::Impl::ShaderCook::CookString fixtureGeneratedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(fixtureGeneratedIncludePath, fixtureGeneratedSource));
    const AStringView fixtureGeneratedSourceView(fixtureGeneratedSource.data(), fixtureGeneratedSource.size());
    const AStringView fixtureGeneratedSnippets[] = {
        "Texture2D<float4> nwbMaterialBindLoadSurfaceBaseColorMap",
        "SamplerState nwbMaterialBindLoadSurfaceBaseColorSampler",
        "NwbHeapSampledImage2DNonUniform",
        "NwbHeapSamplerNonUniform",
    };
    CheckGeneratedSourceContainsAll(fixtureGeneratedSourceView, fixtureGeneratedSnippets);

    UniquePtr<NWB::Core::Assets::IAsset> loadedFixtureAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        fixtureOutputDirectory,
        Name("project/materials/test_material"),
        loadedFixtureAsset
    ));
    if(loadedFixtureAsset){
        EXPECT_EQ(loadedFixtureAsset->assetType(), NWB::Impl::Material::AssetTypeName());
        const NWB::Impl::Material& fixtureMaterial = static_cast<const NWB::Impl::Material&>(*loadedFixtureAsset);
        ASSERT_EQ(fixtureMaterial.resourceReferences().size(), s_ExpectedDualCount);
        EXPECT_EQ(
            fixtureMaterial.resourceReferences()[0u].fixtureName,
            Name(NWB::Impl::MaterialResourceFixture::s_CheckerRgba8)
        );
        EXPECT_EQ(
            fixtureMaterial.resourceReferences()[1u].fixtureName,
            Name(NWB::Impl::MaterialResourceFixture::s_LinearClamp)
        );
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(halfRoot, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(compactRoot, errorCode));
    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(resourceRoot, errorCode));
    EXPECT_TRUE(RemoveAllIfExists(fixtureRoot, errorCode));

#if defined(NWB_FINAL)
    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnknownInterfaceParameterMaterialMeta,
        "material_bind_unknown_interface_parameter",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "parameter 'surface.missing' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));

    Path flatRoot(testArena.arena);
    Path flatOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_FlatInterfaceParameterMaterialMeta,
        "material_bind_flat_interface_parameter",
        testArena,
        flatRoot,
        flatOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "interface parameter 'runtime.fade_alpha' must be declared inside a block map"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(flatRoot, errorCode));

    Path untypedParameterRoot(testArena.arena);
    Path untypedParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UntypedMaterialParameterMeta,
        "material_bind_untyped_material_parameter",
        testArena,
        untypedParameterRoot,
        untypedParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value '0.25, 0.5, 0.75, 1.0'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(untypedParameterRoot, errorCode));

    Path vectorAliasParameterRoot(testArena.arena);
    Path vectorAliasParameterOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_VectorAliasMaterialParameterMeta,
        "material_bind_vector_alias_material_parameter",
        testArena,
        vectorAliasParameterRoot,
        vectorAliasParameterOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "has invalid value 'vec4(0.25, 0.5, 0.75, 1.0)'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(vectorAliasParameterRoot, errorCode));

    Path unsupportedFieldRoot(testArena.arena);
    Path unsupportedFieldOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_UnsupportedMaterialFieldMeta,
        "material_bind_unsupported_material_field",
        testArena,
        unsupportedFieldRoot,
        unsupportedFieldOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("unsupported asset field 'compiler'")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(unsupportedFieldRoot, errorCode));

    Path incompleteBindRoot(testArena.arena);
    Path incompleteBindOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMaterialBindMaterialIntegration(
        AssetsGraphicsFixture::s_SurfaceOnlyMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        "material_bind_incomplete_block_scoped",
        testArena,
        incompleteBindRoot,
        incompleteBindOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "typed parameter 'runtime.fade_alpha' is not declared by interface"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(incompleteBindRoot, errorCode));

    Path interfaceShaderMismatchRoot(testArena.arena);
    Path interfaceShaderMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_without_bind_shader",
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory
    ));
    const Path interfaceShaderMismatchAssetRoot = interfaceShaderMismatchRoot / "assets";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceShaderMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceShaderMismatchRoot,
        interfaceShaderMismatchOutputDirectory,
        { interfaceShaderMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("does not include a generated material bind")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceShaderMismatchRoot, errorCode));

    Path interfaceIdentityMismatchRoot(testArena.arena);
    Path interfaceIdentityMismatchOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_interface_identity_mismatch",
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory
    ));
    const Path interfaceIdentityMismatchAssetRoot = interfaceIdentityMismatchRoot / "assets";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        interfaceIdentityMismatchAssetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta,
        AssetsGraphicsFixture::s_OtherMaterialBindShaderProbeSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        interfaceIdentityMismatchAssetRoot / "material_interfaces" / "other_surface.bind",
        AssetsGraphicsFixture::s_MinimalMaterialBindSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        interfaceIdentityMismatchRoot,
        interfaceIdentityMismatchOutputDirectory,
        { interfaceIdentityMismatchAssetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "includes generated material bind interface 'project/material_interfaces/other_surface'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(interfaceIdentityMismatchRoot, errorCode));
#endif
}

TEST(AssetsGraphics, TransparentMaterialCookUsesViewDependentSurface){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    const bool cooked = AssetsGraphicsFixture::CookMaterialSurfaceIntegration(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_TransparentMaterialMeta,
        AssetsGraphicsFixture::s_ViewDependentTransparentMaterialSurfaceSource,
        "material_view_dependent_transparent_surface",
        testArena,
        root,
        outputDirectory
    );
    EXPECT_TRUE(cooked);
    if(cooked){
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
            testArena,
            outputDirectory,
            Name("project/materials/test_material"),
            loadedAsset
        ));
        if(loadedAsset){
            const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            EXPECT_TRUE(material.transparent());
            EXPECT_EQ(
                material.avboitAccumulatePixelShader().virtualPath,
                Name("generated/avboit_accumulate_ps/project/materials/test_material")
            );
            EXPECT_EQ(
                material.avboitOccupancyPixelShader().virtualPath,
                Name("generated/avboit_occupancy_ps/project/materials/test_material")
            );
            EXPECT_EQ(
                material.avboitExtinctionPixelShader().virtualPath,
                Name("generated/avboit_extinction_ps/project/materials/test_material")
            );
            EXPECT_TRUE(NWB::Impl::HasValidMaterialAvboitPixelShaderContract(
                material.transparent(),
                material.avboitAccumulatePixelShader(),
                material.avboitOccupancyPixelShader(),
                material.avboitExtinctionPixelShader()
            ));
        }

        NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
        EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
        const Name pixelStageName("ps");
        const Name generatedPixelShaderName("generated/material_ps/project/materials/test_material");
        const Name accumulatePixelShaderName("generated/avboit_accumulate_ps/project/materials/test_material");
        const Name occupancyPixelShaderName("generated/avboit_occupancy_ps/project/materials/test_material");
        const Name extinctionPixelShaderName("generated/avboit_extinction_ps/project/materials/test_material");
        u64 sourceChecksum = 0u;
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, generatedPixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, accumulatePixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, occupancyPixelShaderName, pixelStageName, sourceChecksum));
        EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(records, extinctionPixelShaderName, pixelStageName, sourceChecksum));
    }

    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, ShadowTransmittanceDispatchIsolatesOverlappingBindApis){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    ASSERT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "shadow_transmittance_dispatch_overlapping_bind_apis",
        root,
        outputDirectory
    ));

    const Path assetRoot = root / "assets";
    const bool assetsWritten =
        AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "material_interfaces" / "shadow_dispatch_first.bind",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialBindSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "material_interfaces" / "shadow_dispatch_second.bind",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialBindSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_shared_surface_helper.slangi",
            AssetsGraphicsFixture::s_ShadowDispatchSharedSurfaceHelperSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_first.surface",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialSurfaceSource
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "shaders" / "shadow_dispatch_second.surface",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialSurfaceSource
        )
        && AssetsGraphicsFixture::WriteTextFile(assetRoot / "shaders" / "shadow_dispatch.bxdf", AssetsGraphicsFixture::s_MaterialBindBxdfSource)
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "materials" / "shadow_dispatch_first.nwb",
            AssetsGraphicsFixture::s_ShadowDispatchFirstMaterialMeta
        )
        && AssetsGraphicsFixture::WriteTextFile(
            assetRoot / "materials" / "shadow_dispatch_second.nwb",
            AssetsGraphicsFixture::s_ShadowDispatchSecondMaterialMeta
        )
    ;
    ASSERT_TRUE(assetsWritten);

    const Path engineAssetRoot = AssetsGraphicsFixture::AssetsGraphicsTestRepoRoot(testArena) / "impl" / "assets";
    const bool cooked = AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { engineAssetRoot, assetRoot }
    );
    EXPECT_TRUE(cooked);
    if(cooked){
        const Path generatedDispatchPath =
            root / "cache" / "tests" / "shadow_modules" / "shadow" / "generated" / "transmittance_dispatch.slangi"
        ;
        NWB::Impl::ShaderCook::CookString generatedDispatchSource(testArena.arena);
        EXPECT_TRUE(ReadTextFile(generatedDispatchPath, generatedDispatchSource));
        const AStringView generatedDispatch(generatedDispatchSource.data(), generatedDispatchSource.size());
        EXPECT_FALSE(ContainsText(generatedDispatch, "using namespace nwbShadowBindModel"));
        EXPECT_TRUE(ContainsText(
            generatedDispatch,
            "#define nwbMaterialBindLoadSurface nwbShadowBindModel"
        ));
        EXPECT_TRUE(ContainsText(generatedDispatch, "#undef nwbMaterialBindLoadSurface"));
    }
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MaterialRejectsMissingInterfaceCookIntegration){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_missing_interface_rejection",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssetsWithPixelSource(
        testArena,
        assetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_MissingInterfaceMaterialMeta,
        AssetsGraphicsFixture::s_UnboundMaterialShaderProbeSource
    ));
    EXPECT_FALSE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(
        testArena,
        root,
        outputDirectory,
        { assetRoot }
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("interface is required")));

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
#endif
}

TEST(AssetsGraphics, MaterialBindDependencyInvalidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::PrepareAssetsGraphicsCookCase(
        testArena,
        "material_bind_dependency_invalidation",
        root,
        outputDirectory
    ));
    const Path assetRoot = root / "assets";
    if(!AssetsGraphicsFixture::WriteMaterialBindMaterialIntegrationAssets(
        testArena,
        assetRoot,
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        AssetsGraphicsFixture::s_BlockScopedMaterialMeta
    ))
        return;

    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(!loadedAsset)
        return;

    const NWB::Impl::Material& material = static_cast<const NWB::Impl::Material&>(*loadedAsset);
    CheckMinimalMaterialTypedLayout(material);
    CheckMinimalMaterialTypedBlockBytes(material);
    CheckGeneratedMaterialBindBinaryConstants(AStringView(generatedSource.data(), generatedSource.size()), material);
    const u64 initialLayoutHash = material.typedLayoutHash();

    NWB::Core::GraphicsVector<NWB::Core::ShaderArchive::Record> records(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 initialPixelSourceChecksum = 0u;
    EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name("ps"),
        initialPixelSourceChecksum
    ));

    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        assetRoot / "material_interfaces" / "test_surface.bind",
        AssetsGraphicsFixture::s_UpdatedDefaultMaterialBindSource
    ));
    EXPECT_TRUE(AssetsGraphicsFixture::CookPreparedGraphicsAssetRoots(testArena, root, outputDirectory, { assetRoot }));

    generatedSource.clear();
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView updatedGeneratedSource(generatedSource.data(), generatedSource.size());
    EXPECT_TRUE(ContainsText(
        updatedGeneratedSource,
        "static const uint3 NWB_MATERIAL_BIND_SURFACE_FEATURE_MASK_DEFAULT = uint3(7u, 8u, 9u);"
    ));

    loadedAsset.reset();
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedMaterial(
        testArena,
        outputDirectory,
        Name("project/materials/test_material"),
        loadedAsset
    ));
    if(loadedAsset){
        const NWB::Impl::Material& updatedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
        CheckMinimalMaterialTypedLayout(updatedMaterial, 7u, 8u, 9u);
        CheckMinimalMaterialTypedBlockBytes(updatedMaterial, 7u, 8u, 9u);
        CheckGeneratedMaterialBindBinaryConstants(updatedGeneratedSource, updatedMaterial);
        EXPECT_NE(updatedMaterial.typedLayoutHash(), initialLayoutHash);
    }

    records.clear();
    EXPECT_TRUE(AssetsGraphicsFixture::LoadCookedShaderArchiveRecords(testArena, outputDirectory, records));
    u64 updatedPixelSourceChecksum = 0u;
    EXPECT_TRUE(AssetsGraphicsFixture::FindShaderArchiveSourceChecksum(
        records,
        Name("project/shaders/material_ps"),
        Name("ps"),
        updatedPixelSourceChecksum
    ));
    EXPECT_NE(updatedPixelSourceChecksum, initialPixelSourceChecksum);
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));
}

TEST(AssetsGraphics, MaterialBindDiscoveryValidation){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    Path root(testArena.arena);
    Path outputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_valid",
        testArena,
        root,
        outputDirectory
    ));

    const Path generatedIncludePath = root / "cache" / "tests" / "material_bind_includes" / "project" / "material_interfaces" / "test_surface.bind";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_TRUE(ReadTextFile(generatedIncludePath, generatedSource));
    const AStringView generatedSourceView(generatedSource.data(), generatedSource.size());
    CheckGeneratedMaterialBindSource(generatedSourceView);

    const Path shaderIncludeProbePath = root / "shader_include_probe.slang";
    EXPECT_TRUE(AssetsGraphicsFixture::WriteTextFile(
        shaderIncludeProbePath,
        "#include \"project/material_interfaces/test_surface.bind\"\n"
    ));
    NWB::Impl::ShaderCook shaderCook(testArena.arena);
    NWB::Impl::ShaderCook::CookVector<Path> includeDirectories(testArena.arena);
    includeDirectories.push_back(root / "cache" / "tests" / "material_bind_includes");
    NWB::Impl::ShaderCook::CookVector<Path> dependencies(testArena.arena);
    NWB::Core::Alloc::ScratchArena scratchArena(AssetsGraphicsFixture::s_MaterialCookScratchArena);
    EXPECT_TRUE(shaderCook.gatherShaderDependencies(
        shaderIncludeProbePath,
        includeDirectories,
        dependencies,
        scratchArena
    ));
    EXPECT_TRUE(ContainsCanonicalPath(dependencies, generatedIncludePath));
    EXPECT_EQ(logger.errorCount(), 0u);

    ErrorCode errorCode;
    EXPECT_TRUE(RemoveAllIfExists(root, errorCode));

    Path shaderProbeRoot(testArena.arena);
    Path shaderProbeOutputDirectory(testArena.arena);
    EXPECT_TRUE(AssetsGraphicsFixture::CookMaterialBindShaderProbe(
        AssetsGraphicsFixture::s_MinimalMaterialBindSource,
        "material_bind_shader_probe",
        testArena,
        shaderProbeRoot,
        shaderProbeOutputDirectory
    ));
    EXPECT_EQ(logger.errorCount(), 0u);

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(shaderProbeRoot, errorCode));

#if defined(NWB_FINAL)
    Path duplicateIncludeRoot(testArena.arena);
    Path duplicateIncludeOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookDuplicateGeneratedMaterialBindIncludePath(
        "material_bind_duplicate_include_path",
        testArena,
        duplicateIncludeRoot,
        duplicateIncludeOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
        "duplicate material bind include path 'project/material_interfaces/test_surface.bind'"
    )));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(duplicateIncludeRoot, errorCode));

    Path invalidRoot(testArena.arena);
    Path invalidOutputDirectory(testArena.arena);
    EXPECT_FALSE(AssetsGraphicsFixture::CookMinimalMeshWithMaterialBind(
        AssetsGraphicsFixture::s_DuplicateFieldMaterialBindSource,
        "material_bind_duplicate_field",
        testArena,
        invalidRoot,
        invalidOutputDirectory
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate struct field declaration")));

    errorCode.clear();
    EXPECT_TRUE(RemoveAllIfExists(invalidRoot, errorCode));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

