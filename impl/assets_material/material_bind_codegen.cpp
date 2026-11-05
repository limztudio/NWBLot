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
    CookHashSet<CookString>& inOutSymbols,
    CookString& inOutSource
){
    if(!AppendMaterialBindGlobalLayoutConstants(
        includePath,
        interfaceName,
        layout.layoutHash,
        layout.typedLayoutBlocks,
        layout.typedLayoutFields.size(),
        inOutSymbols,
        inOutSource
    ))
        return false;

    NWB_ASSERT(entry.instances.size() == layout.typedLayoutBlocks.size());

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindGeneratedLayoutBlock generatedBlock = ResolveMaterialBindGeneratedLayoutBlock(instance, layout);
        NWB_ASSERT(generatedBlock.entry.blockIndex < layout.typedLayoutBlocks.size());
        if(!AppendMaterialBindBlockLayoutConstants(
            includePath,
            AStringView(instance.name),
            layout.typedLayoutBlocks,
            generatedBlock.entry.blockIndex,
            inOutSymbols,
            inOutSource
        ))
            return false;
    }

    return true;
}

static b8 AppendMaterialBindGeneratedInstance(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    CookHashSet<CookString>& inOutSymbols,
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
        if(!AppendMaterialBindGeneratedInstanceField(
            includePath,
            AStringView(instance.name),
            AStringView(field.name),
            AStringView(field.type),
            field.defaultArgument(),
            layoutBlock.blockClass,
            layoutBlockStorageByteBegin,
            layoutField,
            inOutSymbols,
            inOutSource
        ))
            return false;
    }

    const CookString blockFunctionName = BuildMaterialBindAccessorName(AStringView(instance.name));
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(blockFunctionName), inOutSymbols))
        return false;

    AppendMaterialBindBlockAccessorHeader(AStringView(bindStruct.name), AStringView(blockFunctionName), inOutSource);
    for(const MaterialBindField& field : bindStruct.fields){
        if(TryParseMaterialBindResourceFieldTypeText(AStringView(field.type)).has_value())
            continue;

        const CookString functionName = BuildMaterialBindAccessorName(AStringView(instance.name), AStringView(field.name));
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

    const CookString includeGuard = BuildMaterialBindIncludeGuard(AStringView(includePath));

    const auto interfaceName = Name::FromText(AStringView(entry.virtualPath));
    if(!interfaceName)
        return MakeUnexpected(Failure{});
    const auto layoutResult = BuildMaterialBindTypedLayout(entry, *interfaceName, arena, scratchArena);
    if(!layoutResult)
        return MakeUnexpected(Failure{});
    const MaterialBindTypedLayout& layout = *layoutResult;

    CookHashSet<CookString> generatedSymbols{ 0, Hasher<CookString>(), EqualTo<CookString>(), arena };

    AppendMaterialBindIncludeHeader(AStringView(includeGuard), source);

    if(!AppendMaterialBindLayoutConstants(AStringView(includePath), entry, *interfaceName, layout, generatedSymbols, source))
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
        if(!RegisterGeneratedMaterialBindSymbol(AStringView(includePath), AStringView(bindStruct.name), generatedSymbols))
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
        if(!AppendMaterialBindGeneratedInstance(AStringView(includePath), instance, bindStruct, layout, generatedSymbols, source))
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

