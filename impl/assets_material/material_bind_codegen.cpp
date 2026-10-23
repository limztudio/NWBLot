// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialBindDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialLayoutFieldType::Enum> ParseMaterialBindResourceFieldTypeText(AStringView typeText)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_MaterialBindGeneratedSeparatorChunkRepeatCount = 8u;
static constexpr u32 s_MaterialBindByteBitCount = 8u;
static constexpr u64 s_MaterialBindU64LowWordMask = static_cast<u64>(Limit<u32>::s_Max);
static constexpr u32 s_MaterialBindU64HighWordBitShift = static_cast<u32>(sizeof(u32) * s_MaterialBindByteBitCount);
static constexpr usize s_MaterialBindGeneratedStructReserveBytes = 32u;
static constexpr usize s_MaterialBindGeneratedFieldReserveBytes = 8u;
static constexpr usize s_MaterialBindGeneratedInstanceReserveBytes = 64u;
static constexpr u32 s_MaterialBindGeneratedSeparatorNewlineCount = 3u;
static constexpr AStringView s_MaterialBindAccessorInstanceParameter = "(const NwbMeshInstanceData instance){\n";
static constexpr AStringView s_MaterialBindRequirementError = "#error \"generated material bind includes require mesh/authoring.slangi\"\n";
static constexpr AStringView s_GeneratedEndifBlock = "#endif\n\n";
static constexpr AStringView s_GeneratedEndifBlockBeforeSeparator = "#endif\n\n\n";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildMaterialBindIncludeRoot(const Path& cacheDirectory, const AStringView configurationSafeName){
    return cacheDirectory / configurationSafeName / MaterialBindNames::GeneratedIncludeCacheDirectoryText();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchString> BuildMaterialBindIncludeVirtualPathImpl(ScratchArena& arena, const MaterialBindEntry& entry){
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'"), StringConvert(entry.source));
        return MakeUnexpected(Failure{});
    }

    ScratchString includePath(AStringView(entry.virtualPath), arena);
    includePath += MaterialBindNames::SourceExtensionText();
    return includePath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendMaterialBindGeneratedSeparator(CookString& inOutSource, const u32 newlineCount){
    static constexpr AStringView s_SeparatorChunk = "////////////////";
    for(u32 i = 0u; i < s_MaterialBindGeneratedSeparatorChunkRepeatCount; ++i)
        inOutSource += s_SeparatorChunk;
    for(u32 i = 0u; i < newlineCount; ++i)
        inOutSource += '\n';
}

static void AppendU32Slang(const u32 value, CookString& inOutText){
    char digits[TextDetail::s_DecimalTextBufferBytes];
    inOutText += FormatDecimal(static_cast<usize>(value), digits);
    inOutText += 'u';
}

static void AppendU64AsUint2Slang(const u64 value, CookString& inOutText){
    inOutText += "uint2(";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value & s_MaterialBindU64LowWordMask), inOutText);
    inOutText += ", ";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value >> s_MaterialBindU64HighWordBitShift), inOutText);
    inOutText += ")";
}


static ScratchString BuildMaterialBindIncludeGuard(ScratchArena& arena, const AStringView includePath){
    ScratchString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendMaterialBindGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}

static bool RegisterGeneratedMaterialBindSymbol(const AStringView includePath, const AStringView symbol, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous"), StringConvert(includePath), StringConvert(symbol));
    return false;
}


static AStringView MaterialBindFieldLookupFunctionStorageName(const MaterialBlockClass::Enum blockClass)noexcept{
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return "Constant";
    case MaterialBlockClass::MaterialMutable: return "Mutable";
    default: return AStringView();
    }
}

static AStringView MaterialBindFieldLookupFunctionTypeName(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    static constexpr AStringView s_TypeNames[] = {
        "Bool",
        "Bool2",
        "Bool3",
        "Bool4",
        "Char",
        "Char2",
        "Char3",
        "Char4",
        "UChar",
        "UChar2",
        "UChar3",
        "UChar4",
        "Short",
        "Short2",
        "Short3",
        "Short4",
        "UShort",
        "UShort2",
        "UShort3",
        "UShort4",
        "Int",
        "Int2",
        "Int3",
        "Int4",
        "UInt",
        "UInt2",
        "UInt3",
        "UInt4",
        "Half",
        "Half2",
        "Half3",
        "Half4",
        "Float",
        "Float2",
        "Float3",
        "Float4"
    };
    static_assert(LengthOf(s_TypeNames) == static_cast<usize>(static_cast<u32>(MaterialLayoutFieldType::Float4) - static_cast<u32>(MaterialLayoutFieldType::Bool) + 1u));

    if(!IsMaterialLayoutNumericFieldType(fieldType))
        return AStringView();

    return s_TypeNames[static_cast<u32>(fieldType) - static_cast<u32>(MaterialLayoutFieldType::Bool)];
}

static AStringView MaterialBindResourceSlangTypeName(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    switch(fieldType){
    case MaterialLayoutFieldType::SampledImage2D: return "Texture2D<float4>";
    case MaterialLayoutFieldType::Sampler: return "SamplerState";
    default: return AStringView();
    }
}

static AStringView MaterialBindResourceHeapAccessorName(const MaterialLayoutFieldType::Enum fieldType)noexcept{
    switch(fieldType){
    case MaterialLayoutFieldType::SampledImage2D: return "nwbHeapSampledImage2DNonUniform";
    case MaterialLayoutFieldType::Sampler: return "nwbHeapSamplerNonUniform";
    default: return AStringView();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static ScratchString BuildMaterialBindFieldLookupFunctionName(ScratchArena& arena, const MaterialLayoutFieldType::Enum fieldType, const MaterialBlockClass::Enum blockClass){
    ScratchString functionName(arena);
    const AStringView storageName = MaterialBindFieldLookupFunctionStorageName(blockClass);
    const AStringView typeName = MaterialBindFieldLookupFunctionTypeName(fieldType);
    if(storageName.empty() || typeName.empty())
        return functionName;

    functionName += "nwbMaterialLoad";
    functionName += storageName;
    functionName += typeName;
    return functionName;
}


static bool AppendMaterialBindConstantPrefix(const AStringView includePath, const AStringView symbol, const AStringView type, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, symbol, scratchArena, inOutSymbols))
        return false;

    inOutSource += "static const ";
    inOutSource += type;
    inOutSource += ' ';
    inOutSource += symbol;
    inOutSource += " = ";
    return true;
}

static void AppendMaterialBindConstantSuffix(CookString& inOutSource){
    inOutSource += ";\n";
}

static bool AppendMaterialBindU32Constant(const AStringView includePath, const AStringView symbol, const u32 value, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint", scratchArena, inOutSymbols, inOutSource))
        return false;

    AppendU32Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static bool AppendMaterialBindU64Constant(const AStringView includePath, const AStringView symbol, const u64 value, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint2", scratchArena, inOutSymbols, inOutSource))
        return false;

    AppendU64AsUint2Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}


struct MaterialBindGeneratedLayoutBlock{
    MaterialBindTypedLayoutBlockLookupEntry entry;
    const MaterialTypedLayoutBlock& block;
};

// The layout was built from the same entry, so every instance has a block.
static MaterialBindGeneratedLayoutBlock ResolveMaterialBindGeneratedLayoutBlock(const MaterialBindInstance& instance, const MaterialBindTypedLayout& layout){
    const auto instanceName = Name::FromText(AStringView(instance.name));
    NWB_ASSERT(instanceName);
    const auto blockIt = layout.blockLookup.find(*instanceName);
    NWB_ASSERT(blockIt != layout.blockLookup.end());

    const auto& blockEntry = blockIt.value();
    NWB_ASSERT(blockEntry.blockIndex < layout.typedLayoutBlocks.size());
    return MaterialBindGeneratedLayoutBlock{ blockEntry, layout.typedLayoutBlocks[blockEntry.blockIndex] };
}


static u32& MaterialBindStorageByteSize(const MaterialBlockClass::Enum blockClass, u32& inOutConstantByteSize, u32& inOutMutableByteSize)noexcept{
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return inOutConstantByteSize;
    case MaterialBlockClass::MaterialMutable: return inOutMutableByteSize;
    default: NWB_FATAL_ASSERT(false); return inOutConstantByteSize;
    }
}


struct MaterialBindStorageByteSizes{
    u32 constantBytes = 0u;
    u32 mutableBytes = 0u;
};

static Expected<MaterialBindStorageByteSizes> ComputeMaterialBindStorageByteSizes(const AStringView includePath, const MaterialBindTypedLayout& layout){
    MaterialBindStorageByteSizes sizes;

    for(const MaterialTypedLayoutBlock& block : layout.typedLayoutBlocks){
        u32& storageByteSize = MaterialBindStorageByteSize(block.blockClass, sizes.constantBytes, sizes.mutableBytes);
        if(block.byteSize > Limit<u32>::s_Max - storageByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout storage byte size exceeds u32"), StringConvert(includePath));
            return MakeUnexpected(Failure{});
        }

        storageByteSize += block.byteSize;
    }

    return sizes;
}

// Callers first check that each storage class total fits u32, so every prefix fits too.
static u32 ComputeMaterialBindBlockStorageByteBegin(const MaterialBindTypedLayout& layout, const u32 blockIndex)noexcept{
    u32 byteBegin = 0u;
    NWB_ASSERT(blockIndex < layout.typedLayoutBlocks.size());

    const MaterialBlockClass::Enum blockClass = layout.typedLayoutBlocks[blockIndex].blockClass;
    for(u32 currentBlockIndex = 0u; currentBlockIndex < blockIndex; ++currentBlockIndex){
        const MaterialTypedLayoutBlock& block = layout.typedLayoutBlocks[currentBlockIndex];
        if(block.blockClass != blockClass)
            continue;
        NWB_ASSERT(block.byteSize <= Limit<u32>::s_Max - byteBegin);
        byteBegin += block.byteSize;
    }

    return byteBegin;
}


static bool AppendMaterialBindLayoutConstants(const AStringView includePath, const MaterialBindEntry& entry, const Name& interfaceName, const MaterialBindTypedLayout& layout, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    const auto storageByteSizes = ComputeMaterialBindStorageByteSizes(includePath, layout);
    if(!storageByteSizes)
        return false;
    const u32 constantByteSize = storageByteSizes->constantBytes;
    const u32 mutableByteSize = storageByteSizes->mutableBytes;

    const NameHash& interfaceHash = interfaceName.hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        ScratchString laneSuffix(s_MaterialBindInterfaceHashSymbolPrefix, scratchArena);
        char laneDigits[TextDetail::s_DecimalTextBufferBytes];
        laneSuffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, AStringView(laneSuffix));
        if(!AppendMaterialBindU64Constant(includePath, symbol, interfaceHash.qwords[lane], scratchArena, inOutSymbols, inOutSource))
            return false;
    }

    const ScratchString layoutHashSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindLayoutHashSymbol);
    const ScratchString blockCountSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindBlockCountSymbol);
    const ScratchString fieldCountSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindFieldCountSymbol);
    const ScratchString storageConstantSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindStorageConstantSymbol);
    const ScratchString storageMutableSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindStorageMutableSymbol);
    const ScratchString constantByteSizeSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindConstantByteSizeSymbol);
    const ScratchString mutableByteSizeSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, {}, s_MaterialBindMutableByteSizeSymbol);

    if(!AppendMaterialBindU64Constant(includePath, layoutHashSymbol, layout.layoutHash, scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, blockCountSymbol, static_cast<u32>(layout.typedLayoutBlocks.size()), scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, fieldCountSymbol, static_cast<u32>(layout.typedLayoutFields.size()), scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, storageConstantSymbol, static_cast<u32>(MaterialBlockClass::MaterialConstant), scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, storageMutableSymbol, static_cast<u32>(MaterialBlockClass::MaterialMutable), scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, constantByteSizeSymbol, constantByteSize, scratchArena, inOutSymbols, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(includePath, mutableByteSizeSymbol, mutableByteSize, scratchArena, inOutSymbols, inOutSource))
        return false;

    NWB_ASSERT(entry.instances.size() == layout.typedLayoutBlocks.size());

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindGeneratedLayoutBlock generatedBlock = ResolveMaterialBindGeneratedLayoutBlock(instance, layout);
        const MaterialTypedLayoutBlock& block = generatedBlock.block;
        const u32 blockStorageByteBegin = ComputeMaterialBindBlockStorageByteBegin(layout, generatedBlock.entry.blockIndex);

        const ScratchString storageSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name) }, s_MaterialBindStorageSymbolSuffix);
        const ScratchString offsetSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name) }, s_MaterialBindByteOffsetSymbolSuffix);
        const ScratchString sizeSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name) }, s_MaterialBindByteSizeSymbolSuffix);
        if(!AppendMaterialBindU32Constant(includePath, storageSymbol, static_cast<u32>(block.blockClass), scratchArena, inOutSymbols, inOutSource))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, offsetSymbol, blockStorageByteBegin, scratchArena, inOutSymbols, inOutSource))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, sizeSymbol, block.byteSize, scratchArena, inOutSymbols, inOutSource))
            return false;
    }

    return true;
}

// The typed layout build already parsed this default and key.
static bool AppendMaterialBindFieldConstants(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const AStringView keySymbol,
    const AStringView defaultSymbol,
    ScratchArena& scratchArena,
    ScratchHashSet<ScratchString>& inOutSymbols,
    CookString& inOutSource
){
    const AStringView defaultAttribute = field.defaultArgument();
    NWB_ASSERT(!defaultAttribute.empty());

    const auto keyResult = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(field.name));
    NWB_ASSERT(keyResult);
    const ACompactString& keyText = *keyResult;
    const u64 keyHash = ComputeMaterialBindParameterKeyHash(keyText.view());

    if(!AppendMaterialBindU64Constant(includePath, keySymbol, keyHash, scratchArena, inOutSymbols, inOutSource))
        return false;

    if(!AppendMaterialBindConstantPrefix(includePath, defaultSymbol, AStringView(field.type), scratchArena, inOutSymbols, inOutSource))
        return false;

    inOutSource += defaultAttribute;
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}

static void AppendMaterialBindFieldAccessor(const MaterialBindField& field, const AStringView byteOffsetSymbol, const AStringView functionName, const AStringView loadFunctionName, CookString& inOutSource){
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += s_MaterialBindAccessorInstanceParameter;
    inOutSource += s_GeneratedReturnPrefix;
    inOutSource += loadFunctionName;
    inOutSource += "(instance, ";
    inOutSource += byteOffsetSymbol;
    inOutSource += ");\n";
    inOutSource += s_GeneratedFunctionEnd;
}


// Opaque resources load directly from patched constant slots; generated aggregate structs must not copy handles.
static void AppendMaterialBindResourceFieldAccessor(const MaterialLayoutFieldType::Enum fieldType, const AStringView byteOffsetSymbol, const AStringView functionName, CookString& inOutSource){
    const AStringView slangTypeName = MaterialBindResourceSlangTypeName(fieldType);
    const AStringView heapAccessorName = MaterialBindResourceHeapAccessorName(fieldType);
    NWB_ASSERT(!slangTypeName.empty() && !heapAccessorName.empty());

    inOutSource += slangTypeName;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += s_MaterialBindAccessorInstanceParameter;
    inOutSource += s_GeneratedReturnPrefix;
    inOutSource += heapAccessorName;
    inOutSource += "(nwbMaterialLoadConstantUInt(instance, ";
    inOutSource += byteOffsetSymbol;
    inOutSource += "));\n";
    inOutSource += s_GeneratedFunctionEnd;
}

static bool AppendMaterialBindGeneratedInstance(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    ScratchArena& scratchArena,
    ScratchHashSet<ScratchString>& inOutSymbols,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(inOutSource, s_MaterialBindGeneratedSeparatorNewlineCount);

    const MaterialBindGeneratedLayoutBlock generatedBlock = ResolveMaterialBindGeneratedLayoutBlock(instance, layout);
    const MaterialTypedLayoutBlock& layoutBlock = generatedBlock.block;
    NWB_ASSERT(layoutBlock.fieldCount == bindStruct.fields.size());

    const u32 layoutBlockStorageByteBegin = ComputeMaterialBindBlockStorageByteBegin(layout, generatedBlock.entry.blockIndex);
    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock.fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock.fieldBegin) + fieldOffset;
        NWB_ASSERT(layoutFieldIndex < layout.typedLayoutFields.size());

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layout.typedLayoutFields[layoutFieldIndex];
        NWB_ASSERT(Name::FromText(AStringView(field.name)) == layoutField.fieldName);
        NWB_ASSERT(layoutField.offset <= Limit<u32>::s_Max - layoutBlockStorageByteBegin);

        const bool isResourceField = IsMaterialLayoutResourceFieldType(layoutField.fieldType);
        const ScratchString byteOffsetSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name), AStringView(field.name) }, s_MaterialBindByteOffsetSymbolSuffix);
        const ScratchString functionName = BuildMaterialBindAccessorName(scratchArena, { AStringView(instance.name), AStringView(field.name) });
        if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(functionName), scratchArena, inOutSymbols))
            return false;

        const u32 fieldByteOffset = layoutBlockStorageByteBegin + layoutField.offset;
        if(!AppendMaterialBindU32Constant(includePath, byteOffsetSymbol, fieldByteOffset, scratchArena, inOutSymbols, inOutSource))
            return false;
        inOutSource += '\n';
        if(isResourceField){
            NWB_ASSERT(layoutBlock.blockClass == MaterialBlockClass::MaterialConstant);
            AppendMaterialBindResourceFieldAccessor(layoutField.fieldType, byteOffsetSymbol, functionName, inOutSource);
            inOutSource += '\n';
            continue;
        }

        const ScratchString loadFunctionName = BuildMaterialBindFieldLookupFunctionName(scratchArena, layoutField.fieldType, layoutBlock.blockClass);
        NWB_ASSERT(!loadFunctionName.empty());
        const ScratchString keySymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name), AStringView(field.name) }, s_MaterialBindKeySymbolSuffix);
        const ScratchString defaultSymbol = BuildMaterialBindGeneratedSymbol(scratchArena, { AStringView(instance.name), AStringView(field.name) }, s_MaterialBindDefaultSymbolSuffix);
        if(!AppendMaterialBindFieldConstants(includePath, instance, field, keySymbol, defaultSymbol, scratchArena, inOutSymbols, inOutSource))
            return false;
        AppendMaterialBindFieldAccessor(field, byteOffsetSymbol, functionName, AStringView(loadFunctionName), inOutSource);
        inOutSource += '\n';
    }

    const ScratchString blockFunctionName = BuildMaterialBindAccessorName(scratchArena, { AStringView(instance.name) });
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(blockFunctionName), scratchArena, inOutSymbols))
        return false;

    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += s_MaterialBindAccessorInstanceParameter;
    inOutSource += "    ";
    inOutSource += bindStruct.name;
    inOutSource += " value;\n";
    for(const MaterialBindField& field : bindStruct.fields){
        if(MaterialBindDetail::ParseMaterialBindResourceFieldTypeText(AStringView(field.type)))
            continue;

        const ScratchString functionName = BuildMaterialBindAccessorName(scratchArena, { AStringView(instance.name), AStringView(field.name) });
        inOutSource += "    value.";
        inOutSource += field.name;
        inOutSource += " = ";
        inOutSource += functionName;
        inOutSource += "(instance);\n";
    }
    inOutSource += "    return value;\n";
    inOutSource += s_GeneratedFunctionEnd;

    return true;
}


Expected<CookString> BuildMaterialBindIncludeSourceImpl(CookArena& arena, const MaterialBindEntry& entry, ScratchArena& scratchArena){
    CookString source(arena);

    auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(scratchArena, entry);
    if(!includePathResult)
        return MakeUnexpected(Failure{});
    ScratchString includePath = Move(*includePathResult);

    const ScratchString includeGuard = BuildMaterialBindIncludeGuard(scratchArena, AStringView(includePath));

    const auto interfaceName = Name::FromText(AStringView(entry.virtualPath));
    if(!interfaceName)
        return MakeUnexpected(Failure{});
    const auto layoutResult = BuildMaterialBindTypedLayout(entry, *interfaceName, arena, scratchArena);
    if(!layoutResult)
        return MakeUnexpected(Failure{});
    const MaterialBindTypedLayout& layout = *layoutResult;

    ScratchHashSet<ScratchString> generatedSymbols{ 0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena };

    source += "// generated by NWBLot material bind cook\n";
    AppendMaterialBindGeneratedSeparator(source, s_MaterialBindGeneratedSeparatorNewlineCount);
    source += "#ifndef ";
    source += includeGuard;
    source += "\n#define ";
    source += includeGuard;
    source += "\n\n\n";
    AppendMaterialBindGeneratedSeparator(source, s_MaterialBindGeneratedSeparatorNewlineCount);
    source += "#ifndef NWB_MATERIAL_TYPED_BINDING\n";
    source += s_MaterialBindRequirementError;
    source += s_GeneratedEndifBlock;
    source += "#ifndef NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    source += s_MaterialBindRequirementError;
    source += s_GeneratedEndifBlock;
    source += "#if NWB_MATERIAL_TYPED_BINDING != NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    source += "#error \"generated material bind accessors require NWB_MATERIAL_TYPED_BINDING to match NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\"\n";
    source += s_GeneratedEndifBlockBeforeSeparator;
    AppendMaterialBindGeneratedSeparator(source, s_MaterialBindGeneratedSeparatorNewlineCount);

    if(!AppendMaterialBindLayoutConstants(AStringView(includePath), entry, *interfaceName, layout, scratchArena, generatedSymbols, source))
        return MakeUnexpected(Failure{});

    source += "\n";
    AppendMaterialBindGeneratedSeparator(source, s_MaterialBindGeneratedSeparatorNewlineCount);

    {
        usize estimatedStructBytes = 0u;
        for(const MaterialBindStruct& bindStruct : entry.structs){
            estimatedStructBytes += bindStruct.name.size() + s_MaterialBindGeneratedStructReserveBytes;
            for(const MaterialBindField& bindField : bindStruct.fields)
                estimatedStructBytes += bindField.type.size() + bindField.name.size() + s_MaterialBindGeneratedFieldReserveBytes;
        }
        for(const MaterialBindInstance& bindInstance : entry.instances)
            estimatedStructBytes += bindInstance.type.size() + bindInstance.name.size() + s_MaterialBindGeneratedInstanceReserveBytes;
        source.reserve(source.size() + estimatedStructBytes);
    }

    for(const MaterialBindStruct& bindStruct : entry.structs){
        if(!RegisterGeneratedMaterialBindSymbol(AStringView(includePath), AStringView(bindStruct.name), scratchArena, generatedSymbols))
            return MakeUnexpected(Failure{});

        source += "struct ";
        source += bindStruct.name;
        source += "{\n";
        for(const MaterialBindField& field : bindStruct.fields){
            if(MaterialBindDetail::ParseMaterialBindResourceFieldTypeText(AStringView(field.type)))
                continue;

            source += "    ";
            source += field.type;
            source += ' ';
            source += field.name;
            source += ";\n";
        }
        source += "};\n\n";
    }

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindStruct* bindStruct = entry.findStruct(AStringView(instance.type));
        NWB_ASSERT(bindStruct);
        if(!AppendMaterialBindGeneratedInstance(AStringView(includePath), instance, *bindStruct, layout, scratchArena, generatedSymbols, source))
            return MakeUnexpected(Failure{});
    }

    source += "\n";
    AppendMaterialBindGeneratedSeparator(source, s_MaterialBindGeneratedSeparatorNewlineCount);
    source += s_GeneratedEndifBlockBeforeSeparator;
    AppendMaterialBindGeneratedSeparator(source, 1u);
    return source;
}


Expected<Path> EmitMaterialBindIncludes(CookArena& arena, const Path& cacheDirectory, const AStringView configurationSafeName, const CookVector<MaterialBindEntry>& materialBindEntries, ScratchArena& scratchArena){
    Path includeRoot = BuildMaterialBindIncludeRoot(cacheDirectory, configurationSafeName);
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, "Material bind include generation"))
        return MakeUnexpected(Failure{});
    if(materialBindEntries.empty())
        return includeRoot;

    ScratchHashSet<ScratchString> seenIncludePaths{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(scratchArena, bindEntry);
        if(!includePathResult)
            return MakeUnexpected(Failure{});
        ScratchString includePath = Move(*includePathResult);
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'"), StringConvert(includePath));
            return MakeUnexpected(Failure{});
        }

        auto generatedSourceResult = BuildMaterialBindIncludeSourceImpl(arena, bindEntry, scratchArena);
        if(!generatedSourceResult)
            return MakeUnexpected(Failure{});
        const CookString& generatedSource = *generatedSourceResult;

        const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / AStringView(includePath);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPath()), StringConvert(ensureDirectoriesResult.error().message()));
            return MakeUnexpected(Failure{});
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'"), PathToString<tchar>(outputPath));
            return MakeUnexpected(Failure{});
        }
    }

    return includeRoot;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

