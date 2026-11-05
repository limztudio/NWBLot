// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "bind_private.h"
#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Expected<ScratchString> BuildMaterialBindIncludeVirtualPathImpl(const MaterialBindEntry& entry, ScratchArena& arena){
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'"), ConvertString(entry.source));
        return MakeUnexpected(Failure{});
    }

    ScratchString includePath(AStringView(entry.virtualPath), arena);
    includePath += s_MaterialBindSourceExtensionText;
    return includePath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static ScratchString BuildMaterialBindIncludeGuard(const AStringView includePath, ScratchArena& arena){
    ScratchString guard(s_MaterialBindIncludeGuardPrefix, arena);
    AppendAsciiUpperIdentifier(AStringView(includePath), s_MaterialBindFallbackUpperIdentifier.data(), guard);
    return guard;
}

static b8 RegisterGeneratedMaterialBindSymbol(const AStringView includePath, const AStringView symbol, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous"), ConvertString(includePath), ConvertString(symbol));
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static b8 AppendMaterialBindU32Constant(const AStringView includePath, const AStringView symbol, const u32 value, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, symbol, scratchArena, inOutSymbols))
        return false;

    AppendMaterialBindU32ConstantStatement(symbol, value, inOutSource);
    return true;
}

static b8 AppendMaterialBindU64Constant(const AStringView includePath, const AStringView symbol, const u64 value, ScratchArena& scratchArena, ScratchHashSet<ScratchString>& inOutSymbols, CookString& inOutSource){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, symbol, scratchArena, inOutSymbols))
        return false;

    AppendMaterialBindU64ConstantStatement(symbol, value, inOutSource);
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


static b8 AppendMaterialBindLayoutConstants(
    const AStringView includePath,
    const MaterialBindEntry& entry,
    const Name& interfaceName,
    const MaterialBindTypedLayout& layout,
    ScratchArena& scratchArena,
    ScratchHashSet<ScratchString>& inOutSymbols,
    CookString& inOutSource
){
    const Expected<MaterialBindStorageByteSizes> storageByteSizes = TryComputeMaterialBindStorageByteSizes(layout.typedLayoutBlocks);
    if(!storageByteSizes){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout storage byte size exceeds u32"), ConvertString(includePath));
        return false;
    }
    const u32 constantByteSize = storageByteSizes->constantBytes;
    const u32 mutableByteSize = storageByteSizes->mutableBytes;

    const NameHash& interfaceHash = interfaceName.hash();
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        ScratchString laneSuffix(s_MaterialBindInterfaceHashSymbolPrefix, scratchArena);
        Array<char, s_DecimalTextBufferBytes> laneDigits{};
        laneSuffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const ScratchString symbol = BuildMaterialBindGeneratedSymbol({}, AStringView(laneSuffix), scratchArena);
        if(!AppendMaterialBindU64Constant(includePath, symbol, interfaceHash.qwords[lane], scratchArena, inOutSymbols, inOutSource))
            return false;
    }

    const ScratchString layoutHashSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindLayoutHashSymbol, scratchArena);
    const ScratchString blockCountSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindBlockCountSymbol, scratchArena);
    const ScratchString fieldCountSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindFieldCountSymbol, scratchArena);
    const ScratchString storageConstantSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindStorageConstantSymbol, scratchArena);
    const ScratchString storageMutableSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindStorageMutableSymbol, scratchArena);
    const ScratchString constantByteSizeSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindConstantByteSizeSymbol, scratchArena);
    const ScratchString mutableByteSizeSymbol = BuildMaterialBindGeneratedSymbol({}, s_MaterialBindMutableByteSizeSymbol, scratchArena);

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
        NWB_ASSERT(generatedBlock.entry.blockIndex < layout.typedLayoutBlocks.size());
        const u32 blockStorageByteBegin = ComputeMaterialBindBlockStorageByteBegin(layout.typedLayoutBlocks, generatedBlock.entry.blockIndex);

        const ScratchString storageSymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name) }, s_MaterialBindStorageSymbolSuffix, scratchArena);
        const ScratchString offsetSymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name) }, s_MaterialBindByteOffsetSymbolSuffix, scratchArena);
        const ScratchString sizeSymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name) }, s_MaterialBindByteSizeSymbolSuffix, scratchArena);
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
static b8 AppendMaterialBindFieldConstants(
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

    const Expected<ACompactString> keyText = BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(field.name));
    if(!keyText){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': failed to build key for '{}.{}'"), ConvertString(includePath), ConvertString(instance.name), ConvertString(field.name));
        return false;
    }
    const u64 keyHash = ComputeFnv64AsciiLowerText(keyText->view());

    if(!AppendMaterialBindU64Constant(includePath, keySymbol, keyHash, scratchArena, inOutSymbols, inOutSource))
        return false;

    if(!RegisterGeneratedMaterialBindSymbol(includePath, defaultSymbol, scratchArena, inOutSymbols))
        return false;

    AppendMaterialBindDefaultConstantStatement(defaultSymbol, AStringView(field.type), defaultAttribute, inOutSource);
    return true;
}

static b8 AppendMaterialBindGeneratedInstance(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    ScratchArena& scratchArena,
    ScratchHashSet<ScratchString>& inOutSymbols,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(s_MaterialBindGeneratedSeparatorNewlineCount, inOutSource);

    const MaterialBindGeneratedLayoutBlock generatedBlock = ResolveMaterialBindGeneratedLayoutBlock(instance, layout);
    const MaterialTypedLayoutBlock& layoutBlock = generatedBlock.block;
    NWB_ASSERT(layoutBlock.fieldCount == bindStruct.fields.size());
    NWB_ASSERT(generatedBlock.entry.blockIndex < layout.typedLayoutBlocks.size());

    const u32 layoutBlockStorageByteBegin = ComputeMaterialBindBlockStorageByteBegin(layout.typedLayoutBlocks, generatedBlock.entry.blockIndex);
    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock.fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock.fieldBegin) + fieldOffset;
        NWB_ASSERT(layoutFieldIndex < layout.typedLayoutFields.size());

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layout.typedLayoutFields[layoutFieldIndex];
        NWB_ASSERT(Name::FromText(AStringView(field.name)) == layoutField.fieldName);
        NWB_ASSERT(layoutField.offset <= Limit<u32>::s_Max - layoutBlockStorageByteBegin);

        const b8 isResourceField = IsMaterialLayoutResourceFieldType(layoutField.fieldType);
        const ScratchString byteOffsetSymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name), AStringView(field.name) }, s_MaterialBindByteOffsetSymbolSuffix, scratchArena);
        const ScratchString functionName = BuildMaterialBindAccessorName({ AStringView(instance.name), AStringView(field.name) }, scratchArena);
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

        const auto loadFunctionName = BuildMaterialBindFieldLookupFunctionName(layoutField.fieldType, layoutBlock.blockClass);
        NWB_ASSERT(!loadFunctionName.empty());
        const ScratchString keySymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name), AStringView(field.name) }, s_MaterialBindKeySymbolSuffix, scratchArena);
        const ScratchString defaultSymbol = BuildMaterialBindGeneratedSymbol({ AStringView(instance.name), AStringView(field.name) }, s_MaterialBindDefaultSymbolSuffix, scratchArena);
        if(!AppendMaterialBindFieldConstants(includePath, instance, field, keySymbol, defaultSymbol, scratchArena, inOutSymbols, inOutSource))
            return false;
        AppendMaterialBindFieldAccessor(AStringView(field.type), byteOffsetSymbol, functionName, AStringView(loadFunctionName), inOutSource);
        inOutSource += '\n';
    }

    const ScratchString blockFunctionName = BuildMaterialBindAccessorName({ AStringView(instance.name) }, scratchArena);
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(blockFunctionName), scratchArena, inOutSymbols))
        return false;

    AppendMaterialBindBlockAccessorHeader(AStringView(bindStruct.name), AStringView(blockFunctionName), inOutSource);
    for(const MaterialBindField& field : bindStruct.fields){
        if(TryParseMaterialBindResourceFieldTypeText(AStringView(field.type)).has_value())
            continue;

        const ScratchString functionName = BuildMaterialBindAccessorName({ AStringView(instance.name), AStringView(field.name) }, scratchArena);
        AppendMaterialBindBlockAccessorFieldAssign(AStringView(field.name), AStringView(functionName), inOutSource);
    }
    AppendMaterialBindBlockAccessorFooter(inOutSource);

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialCookString> BuildMaterialBindIncludeSource(const MaterialBindEntry& entry, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    CookString source(arena);

    auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(entry, scratchArena);
    if(!includePathResult)
        return MakeUnexpected(Failure{});
    ScratchString includePath = Move(*includePathResult);

    const ScratchString includeGuard = BuildMaterialBindIncludeGuard(AStringView(includePath), scratchArena);

    const auto interfaceName = Name::FromText(AStringView(entry.virtualPath));
    if(!interfaceName)
        return MakeUnexpected(Failure{});
    const auto layoutResult = BuildMaterialBindTypedLayout(entry, *interfaceName, arena, scratchArena);
    if(!layoutResult)
        return MakeUnexpected(Failure{});
    const MaterialBindTypedLayout& layout = *layoutResult;

    ScratchHashSet<ScratchString> generatedSymbols{ 0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena };

    AppendMaterialBindIncludeHeader(AStringView(includeGuard), source);

    if(!AppendMaterialBindLayoutConstants(AStringView(includePath), entry, *interfaceName, layout, scratchArena, generatedSymbols, source))
        return MakeUnexpected(Failure{});

    source += "\n";
    AppendMaterialBindGeneratedSeparator(s_MaterialBindGeneratedSeparatorNewlineCount, source);

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

        AppendMaterialBindStructHeader(AStringView(bindStruct.name), source);
        for(const MaterialBindField& field : bindStruct.fields){
            if(TryParseMaterialBindResourceFieldTypeText(AStringView(field.type)).has_value())
                continue;

            AppendMaterialBindStructField(AStringView(field.type), AStringView(field.name), source);
        }
        AppendMaterialBindStructFooter(source);
    }

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindStruct& bindStruct = *MakeNotNull(entry.findStruct(AStringView(instance.type)));
        if(!AppendMaterialBindGeneratedInstance(AStringView(includePath), instance, bindStruct, layout, scratchArena, generatedSymbols, source))
            return MakeUnexpected(Failure{});
    }

    AppendMaterialBindIncludeFooter(source);
    return source;
}


Expected<Path> EmitMaterialBindIncludes(const Path& cacheDirectory, const AStringView configurationSafeName, const Span<const MaterialBindEntry> materialBindEntries, MaterialCookArena& arena, Core::Alloc::ScratchArena& scratchArena){
    using namespace MaterialCookDetail;
    Path includeRoot = cacheDirectory / configurationSafeName / s_MaterialBindGeneratedIncludeCacheDirectoryText;
    if(!Core::Assets::PrepareGeneratedIncludeRoot(includeRoot, s_MaterialBindIncludeGenerationLabel))
        return MakeUnexpected(Failure{});
    if(materialBindEntries.empty())
        return includeRoot;

    ScratchHashSet<ScratchString> seenIncludePaths{0, Hasher<ScratchString>(), EqualTo<ScratchString>(), scratchArena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        auto includePathResult = BuildMaterialBindIncludeVirtualPathImpl(bindEntry, scratchArena);
        if(!includePathResult)
            return MakeUnexpected(Failure{});
        ScratchString includePath = Move(*includePathResult);
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'"), ConvertString(includePath));
            return MakeUnexpected(Failure{});
        }

        auto generatedSourceResult = BuildMaterialBindIncludeSource(bindEntry, arena, scratchArena);
        if(!generatedSourceResult)
            return MakeUnexpected(Failure{});
        const CookString& generatedSource = *generatedSourceResult;

        const ScratchPath outputPath = ScratchPath(scratchArena, includeRoot) / AStringView(includePath);
        auto ensureDirectoriesResult = EnsureDirectories(outputPath.parentPath());
        if(!ensureDirectoriesResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}"), PathToString<tchar>(outputPath.parentPathView()), ConvertString(ensureDirectoriesResult.error().message()));
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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

