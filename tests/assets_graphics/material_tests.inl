// limztudio@gmail.com
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

    const Path assetRoot = AssetsGraphicsTestCaseRoot(testArena, "material_meta") / "assets";
    const Path nwbFilePath = assetRoot / "materials" / "test_material.nwb";
    return NWB::Impl::ParseMaterialCookMetadata(assetRoot, "project", nwbFilePath, doc, outEntry, scratchArena);
}

static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
}

template<usize SnippetCount>
static void CheckGeneratedSourceContainsAll(
    TestContext& context,
    const AStringView generatedSourceView,
    const AStringView (&expectedSnippets)[SnippetCount]
){
    for(const AStringView expectedSnippet : expectedSnippets)
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsText(generatedSourceView, expectedSnippet));
}

static void CheckGeneratedSourceHasNoMutableLoads(TestContext& context, const AStringView generatedSourceView){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ContainsText(generatedSourceView, "nwbMaterialLoadMutable"));
}

static void CheckGeneratedSourceHasNoImplicitInstanceAccessors(TestContext& context, const AStringView generatedSourceView){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ContainsText(generatedSourceView, "nwbMeshLoadInstance()"));
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
    TestContext& context,
    const NWB::Impl::MaterialBindStruct& bindStruct,
    const NWB::Impl::MaterialBlockClass::Enum expectedBlockClass
){
    const bool hasConstantAttribute = bindStruct.findAttribute("material_constant") != nullptr;
    const bool hasMutableAttribute = bindStruct.findAttribute("material_mutable") != nullptr;
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        hasConstantAttribute == (expectedBlockClass == NWB::Impl::MaterialBlockClass::MaterialConstant)
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(
        context,
        hasMutableAttribute == (expectedBlockClass == NWB::Impl::MaterialBlockClass::MaterialMutable)
    );
}

static void CheckGeneratedMaterialBindSource(TestContext& context, const AStringView generatedSourceView){
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
    CheckGeneratedSourceContainsAll(context, generatedSourceView, expectedSnippets);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ContainsText(
        generatedSourceView,
        "nwbMaterialFind"
    ));
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(context, generatedSourceView);
}

static void CheckGeneratedHalfMaterialBindSource(TestContext& context, const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 20u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 0u;",
        "static const half NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_DEFAULT = half(0.5);",
        "static const half2 NWB_MATERIAL_BIND_SURFACE_RANGE_DEFAULT = half2(0.0, 1.0);",
        "static const half3 NWB_MATERIAL_BIND_SURFACE_TINT_DEFAULT = half3(0.25, 0.5, 0.75);",
        "static const half4 NWB_MATERIAL_BIND_SURFACE_BASE_COLOR_DEFAULT = half4(1.0, 1.0, 1.0, 1.0);",
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
    CheckGeneratedSourceContainsAll(context, generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(context, generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(context, generatedSourceView);
}

static void CheckGeneratedMixedHalfMaterialBindSource(TestContext& context, const AStringView generatedSourceView){
    const AStringView expectedSnippets[] = {
        "static const uint NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE = 24u;",
        "static const uint NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_ROUGHNESS_BYTE_OFFSET = 0u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_METALLIC_BYTE_OFFSET = 4u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_TINT_BYTE_OFFSET = 8u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_FLAGS_BYTE_OFFSET = 16u;",
        "static const uint NWB_MATERIAL_BIND_SURFACE_TAIL_BYTE_OFFSET = 20u;",
    };
    CheckGeneratedSourceContainsAll(context, generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(context, generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(context, generatedSourceView);
}

static void CheckGeneratedCompactIntegerMaterialBindSource(TestContext& context, const AStringView generatedSourceView){
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
    CheckGeneratedSourceContainsAll(context, generatedSourceView, expectedSnippets);
    CheckGeneratedSourceHasNoMutableLoads(context, generatedSourceView);
    CheckGeneratedSourceHasNoImplicitInstanceAccessors(context, generatedSourceView);
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
    TestContext& context,
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName,
    const NWB::Impl::MaterialLayoutFieldType::Enum expectedFieldType,
    const u32 expectedOffset
){
    const NWB::Impl::MaterialTypedLayoutField* field = FindMaterialTypedLayoutField(material, block, fieldName);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, field != nullptr);
    if(field){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, field->fieldType == expectedFieldType);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, field->offset == expectedOffset);
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
    TestContext& context,
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const ExpectedMaterialLayoutField (&expectedFields)[ExpectedFieldCount]
){
    for(const ExpectedMaterialLayoutField& expectedField : expectedFields)
        CheckMaterialTypedLayoutField(
            context,
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
    TestContext& context,
    const NWB::Impl::MaterialTypedLayoutField& field,
    const ValueType (&expectedDefaults)[ExpectedDefaultCount]
){
    for(usize componentIndex = 0u; componentIndex < ExpectedDefaultCount; ++componentIndex){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            LoadMaterialTypedLayoutDefaultPOD<ValueType>(field, static_cast<u32>(componentIndex)) == expectedDefaults[componentIndex]
        );
    }
}

template<usize ExpectedDefaultCount>
static void CheckMaterialTypedLayoutHalfField(
    TestContext& context,
    const NWB::Impl::Material& material,
    const NWB::Impl::MaterialTypedLayoutBlock& block,
    const AStringView fieldName,
    const NWB::Impl::MaterialLayoutFieldType::Enum expectedFieldType,
    const u32 expectedOffset,
    const f32 (&expectedDefaults)[ExpectedDefaultCount]
){
    const NWB::Impl::MaterialTypedLayoutField* field = CheckMaterialTypedLayoutField(
        context,
        material,
        block,
        fieldName,
        expectedFieldType,
        expectedOffset
    );
    if(!field)
        return;

    for(usize componentIndex = 0u; componentIndex < ExpectedDefaultCount; ++componentIndex){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(
            context,
            ConvertHalfToFloat(LoadMaterialTypedLayoutDefaultPOD<Half>(*field, static_cast<u32>(componentIndex)))
            == expectedDefaults[componentIndex]
        );
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
    TestContext& context,
    const NWB::Impl::Material& material,
    const AStringView blockName,
    const ExpectedHalfBlockValue (&expectedValues)[ExpectedValueCount]
){
    Half rawValue = 0u;
    for(const ExpectedHalfBlockValue& expectedValue : expectedValues){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
            LoadMaterialTypedBlockPOD(material, blockName, expectedValue.byteOffset, rawValue)
            && rawValue == ConvertFloatToHalf(expectedValue.value)
        );
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
    TestContext& context,
    const NWB::Impl::Material& material,
    const ExpectedTypedBlockValue<ValueType> (&expectedValues)[ExpectedValueCount]
){
    ValueType loadedValue = {};
    for(const ExpectedTypedBlockValue<ValueType>& expectedValue : expectedValues){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context,
            LoadMaterialTypedBlockPOD(material, expectedValue.blockName, expectedValue.byteOffset, loadedValue)
            && loadedValue == expectedValue.value
        );
    }
}

static void CheckMinimalMaterialTypedLayout(
    TestContext& context,
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutHash() != 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutBlocks().size() == 2u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutFields().size() == 6u);

    const NWB::Impl::MaterialTypedLayoutBlock* runtimeBlock = FindMaterialTypedLayoutBlock(material, "runtime");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock != nullptr);
    if(runtimeBlock){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialMutable);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->fieldCount == 1u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeBlock->byteSize == 4u);

        const NWB::Impl::MaterialTypedLayoutField* fadeAlpha = CheckMaterialTypedLayoutField(
            context,
            material,
            *runtimeBlock,
            "fade_alpha",
            NWB::Impl::MaterialLayoutFieldType::Float,
            0u
        );
        if(fadeAlpha){
            const f32 fadeAlphaDefaults[] = { 1.0f };
            CheckMaterialTypedLayoutDefaultPODValues(context, *fadeAlpha, fadeAlphaDefaults);
        }
    }

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock != nullptr);
    if(surfaceBlock){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->fieldCount == 5u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->byteSize == 44u);

        const NWB::Impl::MaterialTypedLayoutField* baseColor = CheckMaterialTypedLayoutField(
            context,
            material,
            *surfaceBlock,
            "base_color",
            NWB::Impl::MaterialLayoutFieldType::Float4,
            0u
        );
        if(baseColor){
            const f32 baseColorDefaults[] = { 1.0f, 1.0f, 1.0f, 1.0f };
            CheckMaterialTypedLayoutDefaultPODValues(context, *baseColor, baseColorDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* roughness = CheckMaterialTypedLayoutField(
            context,
            material,
            *surfaceBlock,
            "roughness",
            NWB::Impl::MaterialLayoutFieldType::Float,
            16u
        );
        if(roughness){
            const f32 roughnessDefaults[] = { 0.5f };
            CheckMaterialTypedLayoutDefaultPODValues(context, *roughness, roughnessDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* layerIds = CheckMaterialTypedLayoutField(
            context,
            material,
            *surfaceBlock,
            "layer_ids",
            NWB::Impl::MaterialLayoutFieldType::Int2,
            20u
        );
        if(layerIds){
            const u32 layerIdDefaults[] = { 1u, 2u };
            CheckMaterialTypedLayoutDefaultPODValues(context, *layerIds, layerIdDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* featureMask = CheckMaterialTypedLayoutField(
            context,
            material,
            *surfaceBlock,
            "feature_mask",
            NWB::Impl::MaterialLayoutFieldType::UInt3,
            28u
        );
        if(featureMask){
            const u32 featureMaskDefaults[] = { expectedFeatureMaskX, expectedFeatureMaskY, expectedFeatureMaskZ };
            CheckMaterialTypedLayoutDefaultPODValues(context, *featureMask, featureMaskDefaults);
        }

        const NWB::Impl::MaterialTypedLayoutField* channelEnabled = CheckMaterialTypedLayoutField(
            context,
            material,
            *surfaceBlock,
            "channel_enabled",
            NWB::Impl::MaterialLayoutFieldType::Bool4,
            40u
        );
        if(channelEnabled){
            const u8 channelEnabledDefaults[] = { 1u, 0u, 1u, 0u };
            CheckMaterialTypedLayoutDefaultPODValues(context, *channelEnabled, channelEnabledDefaults);
        }
    }
}

static void CheckMinimalMaterialTypedBlockBytes(
    TestContext& context,
    const NWB::Impl::Material& material,
    const u32 expectedFeatureMaskX = 4u,
    const u32 expectedFeatureMaskY = 5u,
    const u32 expectedFeatureMaskZ = 6u
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedBlockBytes().size() == 48u);

    const ExpectedTypedBlockFloatValue expectedFloatValues[] = {
        { "runtime", 0u, 0.75f },
        { "surface", 0u, 0.25f },
        { "surface", 4u, 0.5f },
        { "surface", 8u, 0.75f },
        { "surface", 12u, 1.0f },
        { "surface", 16u, 0.25f },
    };
    CheckMaterialTypedBlockValues(context, material, expectedFloatValues);

    const ExpectedTypedBlockU32Value expectedU32Values[] = {
        { "surface", 20u, 1u },
        { "surface", 24u, 2u },
        { "surface", 28u, expectedFeatureMaskX },
        { "surface", 32u, expectedFeatureMaskY },
        { "surface", 36u, expectedFeatureMaskZ },
    };
    CheckMaterialTypedBlockValues(context, material, expectedU32Values);

    const ExpectedTypedBlockU8Value expectedU8Values[] = {
        { "surface", 40u, 1u },
        { "surface", 41u, 0u },
        { "surface", 42u, 1u },
        { "surface", 43u, 0u },
    };
    CheckMaterialTypedBlockValues(context, material, expectedU8Values);
}

static void CheckHalfMaterialTypedLayoutAndBlockBytes(TestContext& context, const NWB::Impl::Material& material){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutHash() != 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutBlocks().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutFields().size() == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedBlockBytes().size() == 20u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock != nullptr);
    if(!surfaceBlock)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->fieldCount == 4u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->byteSize == 20u);

    const f32 roughnessDefaults[] = { 0.5f };
    CheckMaterialTypedLayoutHalfField(
        context,
        material,
        *surfaceBlock,
        "roughness",
        NWB::Impl::MaterialLayoutFieldType::Half,
        0u,
        roughnessDefaults
    );

    const f32 rangeDefaults[] = { 0.0f, 1.0f };
    CheckMaterialTypedLayoutHalfField(
        context,
        material,
        *surfaceBlock,
        "range",
        NWB::Impl::MaterialLayoutFieldType::Half2,
        2u,
        rangeDefaults
    );

    const f32 tintDefaults[] = { 0.25f, 0.5f, 0.75f };
    CheckMaterialTypedLayoutHalfField(
        context,
        material,
        *surfaceBlock,
        "tint",
        NWB::Impl::MaterialLayoutFieldType::Half3,
        6u,
        tintDefaults
    );

    const f32 baseColorDefaults[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    CheckMaterialTypedLayoutHalfField(
        context,
        material,
        *surfaceBlock,
        "base_color",
        NWB::Impl::MaterialLayoutFieldType::Half4,
        12u,
        baseColorDefaults
    );

    const ExpectedHalfBlockValue expectedHalfValues[] = {
        { 0u, 0.25f },
        { 2u, 0.125f },
        { 4u, 0.5f },
        { 6u, 1.0f },
        { 8u, 0.75f },
        { 10u, 0.5f },
        { 12u, 1.0f },
        { 14u, 0.5f },
        { 16u, 0.25f },
        { 18u, 0.0f },
    };
    CheckMaterialTypedBlockHalfRawValues(context, material, "surface", expectedHalfValues);
}

static void CheckMixedHalfMaterialTypedLayoutAndBlockBytes(TestContext& context, const NWB::Impl::Material& material){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutHash() != 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutBlocks().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutFields().size() == 5u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedBlockBytes().size() == 24u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock != nullptr);
    if(!surfaceBlock)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->fieldCount == 5u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->byteSize == 24u);

    const ExpectedMaterialLayoutField expectedFields[] = {
        { "roughness", NWB::Impl::MaterialLayoutFieldType::Half, 0u },
        { "metallic", NWB::Impl::MaterialLayoutFieldType::Float, 4u },
        { "tint", NWB::Impl::MaterialLayoutFieldType::Half3, 8u },
        { "flags", NWB::Impl::MaterialLayoutFieldType::UInt, 16u },
        { "tail", NWB::Impl::MaterialLayoutFieldType::Half, 20u },
    };
    CheckMaterialTypedLayoutFields(context, material, *surfaceBlock, expectedFields);

    const auto& bytes = material.typedBlockBytes();
    if(bytes.size() < 24u)
        return;
    const u32 expectedZeroByteOffsets[] = { 2u, 3u, 14u, 15u, 22u, 23u };
    for(const u32 byteOffset : expectedZeroByteOffsets)
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, bytes[byteOffset] == 0u);

    const ExpectedHalfBlockValue expectedHalfValues[] = {
        { 0u, 0.25f },
        { 8u, 1.0f },
        { 10u, 0.5f },
        { 12u, 0.25f },
        { 20u, 0.875f },
    };
    CheckMaterialTypedBlockHalfRawValues(context, material, "surface", expectedHalfValues);

    const ExpectedTypedBlockFloatValue expectedFloatValues[] = {
        { "surface", 4u, 0.75f },
    };
    CheckMaterialTypedBlockValues(context, material, expectedFloatValues);

    const ExpectedTypedBlockU32Value expectedU32Values[] = {
        { "surface", 16u, 42u },
    };
    CheckMaterialTypedBlockValues(context, material, expectedU32Values);
}

static void CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(TestContext& context, const NWB::Impl::Material& material){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutHash() != 0u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutBlocks().size() == 1u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedLayoutFields().size() == 5u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.typedBlockBytes().size() == 20u);

    const NWB::Impl::MaterialTypedLayoutBlock* surfaceBlock = FindMaterialTypedLayoutBlock(material, "surface");
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock != nullptr);
    if(!surfaceBlock)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->blockClass == NWB::Impl::MaterialBlockClass::MaterialConstant);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->fieldCount == 5u);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceBlock->byteSize == 20u);

    const ExpectedMaterialLayoutField expectedFields[] = {
        { "enabled", NWB::Impl::MaterialLayoutFieldType::Bool4, 0u },
        { "signed_bytes", NWB::Impl::MaterialLayoutFieldType::Char4, 4u },
        { "bytes", NWB::Impl::MaterialLayoutFieldType::UChar4, 8u },
        { "signed_words", NWB::Impl::MaterialLayoutFieldType::Short2, 12u },
        { "words", NWB::Impl::MaterialLayoutFieldType::UShort2, 16u },
    };
    CheckMaterialTypedLayoutFields(context, material, *surfaceBlock, expectedFields);

    const NWB::Impl::MaterialTypedLayoutField* enabled = FindMaterialTypedLayoutField(material, *surfaceBlock, "enabled");
    if(enabled){
        const u8 enabledDefaults[] = { 1u, 0u, 1u, 0u };
        CheckMaterialTypedLayoutDefaultPODValues(context, *enabled, enabledDefaults);
    }

    const NWB::Impl::MaterialTypedLayoutField* signedBytes =
        FindMaterialTypedLayoutField(material, *surfaceBlock, "signed_bytes");
    if(signedBytes){
        const u8 signedByteDefaults[] = { 0xffu, 0u, 1u, 127u };
        CheckMaterialTypedLayoutDefaultPODValues(context, *signedBytes, signedByteDefaults);
    }

    const NWB::Impl::MaterialTypedLayoutField* signedWords =
        FindMaterialTypedLayoutField(material, *surfaceBlock, "signed_words");
    if(signedWords){
        const u16 signedWordDefaults[] = { 0x8000u, 0x7fffu };
        CheckMaterialTypedLayoutDefaultPODValues(context, *signedWords, signedWordDefaults);
    }

    const ExpectedTypedBlockU8Value expectedU8Values[] = {
        { "surface", 0u, 0u },
        { "surface", 1u, 1u },
        { "surface", 2u, 0u },
        { "surface", 3u, 1u },
        { "surface", 4u, 0x80u },
        { "surface", 5u, 0xfeu },
        { "surface", 6u, 2u },
        { "surface", 7u, 64u },
        { "surface", 8u, 3u },
        { "surface", 9u, 4u },
        { "surface", 10u, 5u },
        { "surface", 11u, 6u },
    };
    CheckMaterialTypedBlockValues(context, material, expectedU8Values);

    const ExpectedTypedBlockI16Value expectedI16Values[] = {
        { "surface", 12u, -1234 },
        { "surface", 14u, 2345 },
    };
    CheckMaterialTypedBlockValues(context, material, expectedI16Values);

    const ExpectedTypedBlockU16Value expectedU16Values[] = {
        { "surface", 16u, 7u },
        { "surface", 18u, 65534u },
    };
    CheckMaterialTypedBlockValues(context, material, expectedU16Values);
}

static void CheckGeneratedMaterialBindBinaryConstants(
    TestContext& context,
    const AStringView generatedSourceView,
    const NWB::Impl::Material& material
){
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUint2Constant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_LAYOUT_HASH",
        material.typedLayoutHash()
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_BLOCK_COUNT",
        static_cast<u32>(material.typedLayoutBlocks().size())
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
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
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_CONSTANT_BYTE_SIZE",
        constantByteSize
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUintConstant(
        generatedSourceView,
        "NWB_MATERIAL_BIND_MUTABLE_BYTE_SIZE",
        mutableByteSize
    ));

    const NameHash& materialInterfaceHash = material.materialInterface().hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        AString symbol("NWB_MATERIAL_BIND_INTERFACE_HASH_");
        char laneDigits[16u];
        symbol += FormatDecimal(static_cast<usize>(lane), laneDigits);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ContainsGeneratedUint2Constant(
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
    if(ParseMaterialBindFromText(testArena, bindText, caseName, bindEntry, bindRoot, scratchArena)){
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
        static_cast<void>(RemoveAllIfExists(bindRoot, errorCode));
    }
    return built;
}

static bool RoundTripMaterialAssetCodec(
    TestContext& context,
    TestArena& testArena,
    NWB::Impl::MaterialAssetCodec& codec,
    const NWB::Impl::Material& material,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset
){
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
    const bool serialized = codec.serialize(material, binary);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, serialized);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !binary.empty());
    if(!serialized || binary.empty())
        return false;

    const bool deserialized = codec.deserialize(
        testArena.arena,
        material.virtualPath(),
        binary,
        outLoadedAsset
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, deserialized);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, static_cast<bool>(outLoadedAsset));
    return deserialized && static_cast<bool>(outLoadedAsset);
}

static void TestMaterialBindHalfTypedLayoutValues(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        s_HalfMaterialBindSource,
        s_HalfMaterialMeta,
        "material_bind_half_typed_layout_values",
        testArena,
        material,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
    if(built)
        CheckHalfMaterialTypedLayoutAndBlockBytes(context, material);

    NWB::Impl::Material mixedMaterial(testArena.arena);
    const bool builtMixed = BuildMaterialFromBindAndMeta(
        s_MixedHalfMaterialBindSource,
        s_MixedHalfMaterialMeta,
        "material_bind_mixed_half_typed_layout_values",
        testArena,
        mixedMaterial,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, builtMixed);
    if(builtMixed)
        CheckMixedHalfMaterialTypedLayoutAndBlockBytes(context, mixedMaterial);

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        testArena,
        s_HalfMaterialBindSource,
        "material_bind_half_generated_text",
        bindEntry,
        bindRoot,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedHalfMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    }

    Path mixedBindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry mixedBindEntry(testArena.arena);
    const bool parsedMixed = ParseMaterialBindFromText(
        testArena,
        s_MixedHalfMaterialBindSource,
        "material_bind_mixed_half_generated_text",
        mixedBindEntry,
        mixedBindRoot,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsedMixed);
    if(parsedMixed){
        mixedBindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            mixedBindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedMixedHalfMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(bindRoot, errorCode));
    static_cast<void>(RemoveAllIfExists(mixedBindRoot, errorCode));
}

static void TestMaterialBindCompactIntegerTypedLayoutValues(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);
    const bool built = BuildMaterialFromBindAndMeta(
        s_CompactIntegerMaterialBindSource,
        s_CompactIntegerMaterialMeta,
        "material_bind_compact_integer_typed_layout_values",
        testArena,
        material,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
    if(built)
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(context, material);

    Path bindRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        testArena,
        s_CompactIntegerMaterialBindSource,
        "material_bind_compact_integer_generated_text",
        bindEntry,
        bindRoot,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        bindEntry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            bindEntry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedCompactIntegerMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(bindRoot, errorCode));
}

static void TestMaterialMetadataInterfaceAndBlockParameters(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    const bool parsed = ParseMaterialEntryFromMetaText(s_BlockScopedMaterialMeta, testArena, materialEntry, scratchArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(!parsed)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, materialEntry.materialInterface == Name("project/material_interfaces/test_surface"));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, materialEntry.parameters.size() == 3u);

    NWB::Impl::MaterialBindEntry bindEntry(testArena.arena);
    Path bindRoot(testArena.arena);
    const bool parsedBind = ParseMaterialBindFromText(
        testArena,
        s_MinimalMaterialBindSource,
        "material_meta_bind_validation",
        bindEntry,
        bindRoot,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsedBind);
    if(!parsedBind)
        return;
    bindEntry.virtualPath = "project/material_interfaces/test_surface";

    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialBindEntry> bindEntries(testArena.arena);
    bindEntries.push_back(Move(bindEntry));
    NWB::Impl::ShaderCook::CookVector<NWB::Impl::MaterialCookEntry> materialEntries(testArena.arena);
    materialEntries.push_back(Move(materialEntry));
    const bool validated = NWB::Impl::ValidateMaterialCookInterfaces(bindEntries, materialEntries, scratchArena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, validated);
    if(!validated)
        return;

    NWB::Impl::Material material(testArena.arena);
    const bool built = NWB::Impl::BuildMaterialAsset(materialEntries[0u], material);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
    if(!built)
        return;

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !material.transparent());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, material.materialInterface() == Name("project/material_interfaces/test_surface"));

    NWB::Impl::Material transparentMaterial(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, BuildMaterialFromBindAndMeta(
        s_MinimalMaterialBindSource,
        s_TransparentMaterialMeta,
        "material_meta_explicit_transparent",
        testArena,
        transparentMaterial,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, transparentMaterial.transparent());

    NWB::Impl::Material twoSidedMaterial(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, BuildMaterialFromBindAndMeta(
        s_MinimalMaterialBindSource,
        s_TwoSidedMaterialMeta,
        "material_meta_explicit_two_sided",
        testArena,
        twoSidedMaterial,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, twoSidedMaterial.twoSided());
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !twoSidedMaterial.transparent());

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
}

static void TestMaterialCodecTypedLayoutBoundary(TestContext& context){
    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::Material material(testArena.arena);

    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        const bool built = BuildMaterialFromBindAndMeta(
            s_MinimalMaterialBindSource,
            s_BlockScopedMaterialMeta,
            "material_codec_typed_layout_boundary",
            testArena,
            material,
            scratchArena
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, built);
        if(!built)
            return;

        material.setTransparent(true);

        NWB::Impl::MaterialAssetCodec codec;
        UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
        if(RoundTripMaterialAssetCodec(context, testArena, codec, material, loadedAsset)){
            const NWB::Impl::Material& loadedMaterial = static_cast<const NWB::Impl::Material&>(*loadedAsset);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMaterial.materialInterface() == material.materialInterface());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMaterial.typedLayoutHash() == material.typedLayoutHash());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedMaterial.transparent());
            CheckMinimalMaterialTypedLayout(context, loadedMaterial);
            CheckMinimalMaterialTypedBlockBytes(context, loadedMaterial);
        }

        NWB::Impl::Material halfMaterial(testArena.arena);
        const bool builtHalfMaterial = BuildMaterialFromBindAndMeta(
            s_HalfMaterialBindSource,
            s_HalfMaterialMeta,
            "material_codec_half_typed_layout",
            testArena,
            halfMaterial,
            scratchArena
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, builtHalfMaterial);
        UniquePtr<NWB::Core::Assets::IAsset> loadedHalfAsset;
        if(builtHalfMaterial && RoundTripMaterialAssetCodec(context, testArena, codec, halfMaterial, loadedHalfAsset)){
            const NWB::Impl::Material& loadedHalfMaterial = static_cast<const NWB::Impl::Material&>(*loadedHalfAsset);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedHalfMaterial.materialInterface() == halfMaterial.materialInterface());
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, loadedHalfMaterial.typedLayoutHash() == halfMaterial.typedLayoutHash());
            CheckHalfMaterialTypedLayoutAndBlockBytes(context, loadedHalfMaterial);
        }

        NWB::Impl::Material mixedHalfMaterial(testArena.arena);
        const bool builtMixedHalfMaterial = BuildMaterialFromBindAndMeta(
            s_MixedHalfMaterialBindSource,
            s_MixedHalfMaterialMeta,
            "material_codec_mixed_half_typed_layout",
            testArena,
            mixedHalfMaterial,
            scratchArena
        );
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, builtMixedHalfMaterial);
        UniquePtr<NWB::Core::Assets::IAsset> loadedMixedHalfAsset;
        if(
            builtMixedHalfMaterial
            && RoundTripMaterialAssetCodec(context, testArena, codec, mixedHalfMaterial, loadedMixedHalfAsset)
        ){
            const NWB::Impl::Material& loadedMixedHalfMaterial =
                static_cast<const NWB::Impl::Material&>(*loadedMixedHalfAsset);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(
                context,
                loadedMixedHalfMaterial.materialInterface() == mixedHalfMaterial.materialInterface()
            );
            NWB_ASSETS_GRAPHICS_TEST_CHECK(
                context,
                loadedMixedHalfMaterial.typedLayoutHash() == mixedHalfMaterial.typedLayoutHash()
            );
            CheckMixedHalfMaterialTypedLayoutAndBlockBytes(context, loadedMixedHalfMaterial);
        }

        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);
    }

#if defined(NWB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, codec.serialize(material, binary));

        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, FindMaterialBinaryTypedLayoutOffsets(
            binary,
            layoutHashOffset,
            blockByteCountOffset
        ));

        NWB::Core::Assets::AssetBytes hashMismatchBinary = binary;
        const u64 invalidLayoutHash = material.typedLayoutHash() == Limit<u64>::s_Max ? material.typedLayoutHash() - 1u : material.typedLayoutHash() + 1u;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwritePOD(hashMismatchBinary, layoutHashOffset, invalidLayoutHash));
        CheckCodecRejectsBinary(context, testArena, codec, material.virtualPath(), hashMismatchBinary);

        NWB::Core::Assets::AssetBytes byteSizeMismatchBinary = binary;
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !material.typedBlockBytes().empty());
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, OverwritePOD(
            byteSizeMismatchBinary,
            blockByteCountOffset,
            static_cast<u32>(material.typedBlockBytes().size() - 1u)
        ));
        CheckCodecRejectsBinary(context, testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT(
            "typed block byte count does not match typed layout"
        )));
    }
#endif
}

static void TestMaterialBindSchemaValidation(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    Path root(testArena.arena);
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        testArena,
        s_MinimalMaterialBindSource,
        "material_bind_schema_valid",
        entry,
        root,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entry.structs.size() == 2u);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, entry.instances.size() == 2u);

        const NWB::Impl::MaterialBindStruct* surfaceStruct = entry.findStruct("NwbTestSurfaceMaterial");
        const NWB::Impl::MaterialBindStruct* runtimeStruct = entry.findStruct("NwbTestRuntimeMaterial");
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceStruct != nullptr);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeStruct != nullptr);
        if(surfaceStruct){
            CheckMaterialBindStructBlockClass(
                context,
                *surfaceStruct,
                NWB::Impl::MaterialBlockClass::MaterialConstant
            );
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceStruct->fields.size() == 5u);

            const NWB::Impl::MaterialBindField* baseColorField = surfaceStruct->findField("base_color");
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, baseColorField != nullptr);
            if(baseColorField){
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(baseColorField->type) == "float4");
                const NWB::Impl::MaterialBindAttribute* defaultAttribute = baseColorField->findAttribute("default");
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, defaultAttribute != nullptr);
                NWB_ASSETS_GRAPHICS_TEST_CHECK(context, defaultAttribute && defaultAttribute->arguments.size() == 1u);
                if(defaultAttribute && defaultAttribute->arguments.size() == 1u){
                    NWB_ASSETS_GRAPHICS_TEST_CHECK(
                        context,
                        AStringView(defaultAttribute->arguments[0u]) == "float4(1.0, 1.0, 1.0, 1.0)"
                    );
                }
            }
        }
        if(runtimeStruct){
            CheckMaterialBindStructBlockClass(
                context,
                *runtimeStruct,
                NWB::Impl::MaterialBlockClass::MaterialMutable
            );
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeStruct->fields.size() == 1u);
        }

        const NWB::Impl::MaterialBindInstance* surfaceInstance = entry.findInstance("surface");
        const NWB::Impl::MaterialBindInstance* runtimeInstance = entry.findInstance("runtime");
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, surfaceInstance != nullptr);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, runtimeInstance != nullptr);
        if(surfaceInstance)
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(surfaceInstance->type) == "NwbTestSurfaceMaterial");
        if(runtimeInstance)
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, AStringView(runtimeInstance->type) == "NwbTestRuntimeMaterial");
    }

    Path halfRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry halfEntry(testArena.arena);
    const bool parsedHalf = ParseMaterialBindFromText(
        testArena,
        s_HalfMaterialBindSource,
        "material_bind_half_schema_valid",
        halfEntry,
        halfRoot,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsedHalf);
    if(parsedHalf){
        const NWB::Impl::MaterialBindStruct* halfStruct = halfEntry.findStruct("NwbTestSurfaceMaterial");
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, halfStruct != nullptr);
        if(halfStruct){
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, halfStruct->findField("roughness") != nullptr);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, halfStruct->findField("range") != nullptr);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, halfStruct->findField("tint") != nullptr);
            NWB_ASSETS_GRAPHICS_TEST_CHECK(context, halfStruct->findField("base_color") != nullptr);
        }
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
    static_cast<void>(RemoveAllIfExists(halfRoot, errorCode));

#if defined(NWB_FINAL)
    auto expectParseFailure = [&](
        const AStringView bindText,
        const AStringView caseName,
        const tchar* expectedError
    ){
        Path invalidRoot(testArena.arena);
        NWB::Impl::MaterialBindEntry invalidEntry(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !ParseMaterialBindFromText(
            testArena,
            bindText,
            caseName,
            invalidEntry,
            invalidRoot,
            scratchArena
        ));
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(expectedError));

        ErrorCode removeErrorCode;
        static_cast<void>(RemoveAllIfExists(invalidRoot, removeErrorCode));
    };

    expectParseFailure(
        s_UnknownBlockClassMaterialBindSource,
        "material_bind_unknown_block_class",
        NWB_TEXT("unsupported attribute 'material_project'")
    );
    expectParseFailure(
        s_UnsupportedFieldTypeMaterialBindSource,
        "material_bind_unsupported_field_type",
        NWB_TEXT("unsupported type 'double'")
    );
    expectParseFailure(
        s_InvalidDefaultMaterialBindSource,
        "material_bind_invalid_default",
        NWB_TEXT("default attribute requires one non-empty string argument")
    );
    expectParseFailure(
        s_MissingDefaultMaterialBindSource,
        "material_bind_missing_default",
        NWB_TEXT("must declare a default attribute")
    );
    expectParseFailure(
        s_DuplicateInstanceMaterialBindSource,
        "material_bind_duplicate_instance",
        NWB_TEXT("duplicate struct instance declaration")
    );
    expectParseFailure(
        s_InstanceOverrideMaterialBindSource,
        "material_bind_instance_override",
        NWB_TEXT("unsupported asset field 'instance_override'")
    );

    Path float1DefaultRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry float1DefaultEntry(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, ParseMaterialBindFromText(
        testArena,
        s_Float1DefaultMaterialBindSource,
        "material_bind_float1_default",
        float1DefaultEntry,
        float1DefaultRoot,
        scratchArena
    ));
    float1DefaultEntry.virtualPath = "project/material_interfaces/test_surface";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, !NWB::Impl::BuildMaterialBindIncludeSource(
        testArena.arena,
        float1DefaultEntry,
        generatedSource,
        scratchArena
    ));
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));

    ErrorCode removeErrorCode;
    static_cast<void>(RemoveAllIfExists(float1DefaultRoot, removeErrorCode));
#endif
}

static void TestMaterialBindGeneratedSlangText(TestContext& context){
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    Path root(testArena.arena);
    NWB::Impl::MaterialBindEntry entry(testArena.arena);
    const bool parsed = ParseMaterialBindFromText(
        testArena,
        s_MinimalMaterialBindSource,
        "material_bind_generated_text",
        entry,
        root,
        scratchArena
    );
    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, parsed);
    if(parsed){
        entry.virtualPath = "project/material_interfaces/test_surface";

        NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
        NWB_ASSETS_GRAPHICS_TEST_CHECK(context, NWB::Impl::BuildMaterialBindIncludeSource(
            testArena.arena,
            entry,
            generatedSource,
            scratchArena
        ));
        CheckGeneratedMaterialBindSource(context, AStringView(generatedSource.data(), generatedSource.size()));
    }

    NWB_ASSETS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 0u);

    ErrorCode errorCode;
    static_cast<void>(RemoveAllIfExists(root, errorCode));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

