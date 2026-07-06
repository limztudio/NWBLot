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
    EXPECT_EQ(material.typedLayoutBlocks().size(), 2u);
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
            const u32 layerIdDefaults[] = { 1u, 2u };
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
        { "surface", 24u, 2u },
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
        2u,
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
    const u32 expectedZeroByteOffsets[] = { 2u, 3u, 14u, 15u, 22u, 23u };
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
    NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
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

TEST(AssetsGraphics, MaterialBindHalfTypedLayoutValues){
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
    EXPECT_TRUE(built);
    if(built)
        CheckHalfMaterialTypedLayoutAndBlockBytes(material);

    NWB::Impl::Material mixedMaterial(testArena.arena);
    const bool builtMixed = BuildMaterialFromBindAndMeta(
        s_MixedHalfMaterialBindSource,
        s_MixedHalfMaterialMeta,
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
    const bool parsed = ParseMaterialBindFromText(
        testArena,
        s_HalfMaterialBindSource,
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
    const bool parsedMixed = ParseMaterialBindFromText(
        testArena,
        s_MixedHalfMaterialBindSource,
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
    EXPECT_TRUE(built);
    if(built)
        CheckCompactIntegerMaterialTypedLayoutAndBlockBytes(material);

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
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
    NWB::Impl::MaterialCookEntry materialEntry(testArena.arena);
    const bool parsed = ParseMaterialEntryFromMetaText(s_BlockScopedMaterialMeta, testArena, materialEntry, scratchArena);
    EXPECT_TRUE(parsed);
    if(!parsed)
        return;

    EXPECT_EQ(AStringView(materialEntry.materialInterface), AStringView("project/material_interfaces/test_surface"));
    EXPECT_EQ(materialEntry.parameters.size(), 3u);

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
    EXPECT_EQ(material.materialInterface(), Name("project/material_interfaces/test_surface"));

    // A material declaring no `refractive` flag keeps the backward-compatible default (not a refractive caster).
    EXPECT_FALSE(material.refractive());

    NWB::Impl::Material transparentMaterial(testArena.arena);
    EXPECT_TRUE(BuildMaterialFromBindAndMeta(
        s_MinimalMaterialBindSource,
        s_TransparentMaterialMeta,
        "material_meta_explicit_transparent",
        testArena,
        transparentMaterial,
        scratchArena
    ));
    EXPECT_TRUE(transparentMaterial.transparent());

    NWB::Impl::Material twoSidedMaterial(testArena.arena);
    EXPECT_TRUE(BuildMaterialFromBindAndMeta(
        s_MinimalMaterialBindSource,
        s_TwoSidedMaterialMeta,
        "material_meta_explicit_two_sided",
        testArena,
        twoSidedMaterial,
        scratchArena
    ));
    EXPECT_TRUE(twoSidedMaterial.twoSided());
    EXPECT_FALSE(twoSidedMaterial.transparent());

    NWB::Impl::Material refractiveMaterial(testArena.arena);
    EXPECT_TRUE(BuildMaterialFromBindAndMeta(
        s_MinimalMaterialBindSource,
        s_RefractiveMaterialMeta,
        "material_meta_explicit_refractive",
        testArena,
        refractiveMaterial,
        scratchArena
    ));
    EXPECT_TRUE(refractiveMaterial.transparent());
    EXPECT_TRUE(refractiveMaterial.refractive());

    EXPECT_EQ(logger.errorCount(), 0u);
}

TEST(AssetsGraphics, MaterialMetadataRejectsEngineRootedPolicySelectors){
#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    static constexpr AStringView s_EngineRootedInterfaceMeta = R"NWB_META(material asset;

asset.interface = "engine/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";

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

)NWB_META";

    static constexpr AStringView s_EngineRootedBxdfMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "engine/shaders/material_bxdf.bxdf";

asset.shaders = {
    "mesh": "project/shaders/material_mesh",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    static constexpr AStringView s_EngineRootedStageShaderMeta = R"NWB_META(material asset;

asset.interface = "project/material_interfaces/test_surface.bind";
asset.bxdf = "project/shaders/material_bxdf.bxdf";

asset.shaders = {
    "mesh": "engine/graphics/mesh/shared_ms",
    "ps": "project/shaders/material_ps",
};
asset.shader_variant = "default";

)NWB_META";

    TestArena testArena;
    NWB::Core::Alloc::ScratchArena scratchArena(s_MaterialScratchArena);
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
        EXPECT_TRUE(built);
        if(!built)
            return;

        material.setTransparent(true);
        material.setRefractive(true);

        NWB::Impl::MaterialAssetCodec codec;
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
            s_HalfMaterialBindSource,
            s_HalfMaterialMeta,
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
            s_MixedHalfMaterialBindSource,
            s_MixedHalfMaterialMeta,
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

        EXPECT_EQ(logger.errorCount(), 0u);
    }

#if defined(NWB_FINAL)
    {
        CapturingLogger logger;
        NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

        NWB::Impl::MaterialAssetCodec codec;
        NWB::Core::Assets::AssetBytes binary = MakeAssetBytes(testArena);
        EXPECT_TRUE(codec.serialize(material, binary));

        usize layoutHashOffset = 0u;
        usize blockByteCountOffset = 0u;
        EXPECT_TRUE(FindMaterialBinaryTypedLayoutOffsets(
            binary,
            layoutHashOffset,
            blockByteCountOffset
        ));

        NWB::Core::Assets::AssetBytes hashMismatchBinary = binary;
        const u64 invalidLayoutHash = material.typedLayoutHash() == Limit<u64>::s_Max ? material.typedLayoutHash() - 1u : material.typedLayoutHash() + 1u;
        EXPECT_TRUE(OverwritePOD(hashMismatchBinary, layoutHashOffset, invalidLayoutHash));
        CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), hashMismatchBinary);

        NWB::Core::Assets::AssetBytes byteSizeMismatchBinary = binary;
        EXPECT_FALSE(material.typedBlockBytes().empty());
        EXPECT_TRUE(OverwritePOD(
            byteSizeMismatchBinary,
            blockByteCountOffset,
            static_cast<u32>(material.typedBlockBytes().size() - 1u)
        ));
        CheckCodecRejectsBinary(testArena, codec, material.virtualPath(), byteSizeMismatchBinary);

        EXPECT_EQ(logger.errorCount(), 2u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("typed layout hash mismatch")));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT(
            "typed block byte count does not match typed layout"
        )));
    }
#endif
}

TEST(AssetsGraphics, MaterialBindSchemaValidation){
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
    EXPECT_TRUE(parsed);
    if(parsed){
        EXPECT_EQ(entry.structs.size(), 2u);
        EXPECT_EQ(entry.instances.size(), 2u);

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
    const bool parsedHalf = ParseMaterialBindFromText(
        testArena,
        s_HalfMaterialBindSource,
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

#if defined(NWB_FINAL)
    auto expectParseFailure = [&](
        const AStringView bindText,
        const AStringView caseName,
        const tchar* expectedError
    ){
        Path invalidRoot(testArena.arena);
        NWB::Impl::MaterialBindEntry invalidEntry(testArena.arena);
        EXPECT_FALSE(ParseMaterialBindFromText(
            testArena,
            bindText,
            caseName,
            invalidEntry,
            invalidRoot,
            scratchArena
        ));
        EXPECT_TRUE(logger.sawErrorContaining(expectedError));

        ErrorCode removeErrorCode;
        EXPECT_TRUE(RemoveAllIfExists(invalidRoot, removeErrorCode));
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
    EXPECT_TRUE(ParseMaterialBindFromText(
        testArena,
        s_Float1DefaultMaterialBindSource,
        "material_bind_float1_default",
        float1DefaultEntry,
        float1DefaultRoot,
        scratchArena
    ));
    float1DefaultEntry.virtualPath = "project/material_interfaces/test_surface";
    NWB::Impl::ShaderCook::CookString generatedSource(testArena.arena);
    EXPECT_FALSE(NWB::Impl::BuildMaterialBindIncludeSource(
        testArena.arena,
        float1DefaultEntry,
        generatedSource,
        scratchArena
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("default 'float1(1.0)'")));

    const Name cacheInterface("project/material_interfaces/test_surface");
    NWB::Impl::MaterialBindTypedLayoutCache layoutCache(testArena.arena);
    const NWB::Impl::MaterialBindTypedLayout* cachedLayout = nullptr;
    EXPECT_FALSE(NWB::Impl::FindOrBuildMaterialBindTypedLayout(
        cacheInterface,
        float1DefaultEntry,
        layoutCache,
        cachedLayout,
        scratchArena
    ));
    EXPECT_EQ(cachedLayout, nullptr);
    EXPECT_TRUE(layoutCache.entries.empty());
    EXPECT_TRUE(layoutCache.lookup.empty());

    Path validCacheRoot(testArena.arena);
    NWB::Impl::MaterialBindEntry validCacheEntry(testArena.arena);
    EXPECT_TRUE(ParseMaterialBindFromText(
        testArena,
        s_MinimalMaterialBindSource,
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

    ErrorCode removeErrorCode;
    EXPECT_TRUE(RemoveAllIfExists(float1DefaultRoot, removeErrorCode));
    EXPECT_TRUE(RemoveAllIfExists(validCacheRoot, removeErrorCode));
#endif
}

TEST(AssetsGraphics, MaterialBindGeneratedSlangText){
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

