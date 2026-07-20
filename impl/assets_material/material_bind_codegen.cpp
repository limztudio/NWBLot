// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Path BuildMaterialBindIncludeRoot(
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    ScratchArena& scratchArena
){
    ScratchString configurationName(configurationSafeName, scratchArena);
    ScratchString includeDirectoryName(MaterialBindNames::GeneratedIncludeCacheDirectoryText(), scratchArena);
    return cacheDirectory / configurationName.c_str() / includeDirectoryName.c_str();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool BuildMaterialBindIncludeVirtualPathImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outIncludePath
){
    outIncludePath.clear();
    if(entry.virtualPath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation failed: virtual path is empty for '{}'")
            , StringConvert(entry.source)
        );
        return false;
    }

    CookString includePath(entry.virtualPath, arena);
    includePath += MaterialBindNames::SourceExtensionText();
    outIncludePath = Move(includePath);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool TryResolveMaterialBindDependencyInterface(
    const Path& normalizedMaterialBindIncludeRoot,
    const Path& dependency,
    CookString& outInterfacePath,
    ScratchArena& scratchArena
){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    ErrorCode errorCode;
    Path normalizedDependency = AbsolutePath(dependency, errorCode).lexically_normal();
    if(errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}")
            , PathToString<tchar>(dependency)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchString extension = PathToString(scratchArena, normalizedDependency.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != MaterialBindNames::SourceExtensionText())
        return true;

    Path relativePath = normalizedDependency.lexically_relative(normalizedMaterialBindIncludeRoot);
    relativePath.replace_extension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'")
            , PathToString<tchar>(normalizedDependency)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ResolveMaterialBindDependencyInterface(
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<Path>& dependencies,
    CookString& outInterfacePath,
    Name& outInterfaceName,
    bool& outDependsOnMaterialBind,
    ScratchArena& scratchArena
){
    outInterfacePath.clear();
    outInterfaceName = NAME_NONE;
    outDependsOnMaterialBind = false;

    Path normalizedMaterialBindIncludeRoot(materialBindIncludeRoot.arena());
    if(!materialBindIncludeRoot.empty()){
        ErrorCode errorCode;
        normalizedMaterialBindIncludeRoot = AbsolutePath(materialBindIncludeRoot, errorCode).lexically_normal();
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}")
                , PathToString<tchar>(materialBindIncludeRoot)
                , StringConvert(errorCode.message())
            );
            return false;
        }
    }

    CookArena& arena = outInterfacePath.get_allocator().arena();
    CookString dependencyInterfacePath{arena};
    bool dependsOnMultipleInterfaces = false;
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(
            normalizedMaterialBindIncludeRoot,
            dependency,
            dependencyInterfacePath,
            scratchArena
        ))
            return false;
        if(dependencyInterfacePath.empty())
            continue;

        const Name dependencyInterfaceName{ AStringView(dependencyInterfacePath) };
        if(!dependencyInterfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated "
                "material bind interface '{}'")
                , StringConvert(shaderName)
                , StringConvert(dependencyInterfacePath)
            );
            return false;
        }

        // The shader depends on at least one material's generated `.bind` interface, so it reads the typed
        // material constants and must receive the typed binding.
        outDependsOnMaterialBind = true;
        if(dependsOnMultipleInterfaces)
            continue;

        if(!outInterfaceName){
            outInterfacePath = dependencyInterfacePath;
            outInterfaceName = dependencyInterfaceName;
            continue;
        }

        if(outInterfaceName != dependencyInterfaceName){
            // More than one DISTINCT interface: this is a generic consumer of a cook-generated dispatch module,
            // not a per-material shader. The shadow-transmittance dispatch #includes every surface material's
            // `.bind`, namespace-isolated by EmitShadowTransmittanceDispatchModule, so it can evaluate each
            // occluder's transmittance hook by shading-model id. Such a shader still reads the typed binding above
            // but has NO single owning interface; only per-material pixel shaders (exactly one interface) carry one for
            // material_validation to match against the material's declaration.
            outInterfacePath.clear();
            outInterfaceName = NAME_NONE;
            dependsOnMultipleInterfaces = true;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// material bind generated Slang include helpers


static void AppendMaterialBindGeneratedSeparator(CookString& inOutSource, const u32 newlineCount){
    static constexpr AStringView s_SeparatorChunk = "////////////////";
    for(u32 i = 0u; i < 8u; ++i)
        inOutSource += s_SeparatorChunk;
    for(u32 i = 0u; i < newlineCount; ++i)
        inOutSource += '\n';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendGeneratedUpperIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    for(const char ch : text)
        inOutText += IsAsciiAlphaNumeric(ch) ? ToAsciiUpper(ch) : '_';
    if(inOutText.size() == beginSize)
        inOutText += "VALUE";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendGeneratedPascalIdentifier(const AStringView text, CookString& inOutText){
    const usize beginSize = inOutText.size();
    bool upperNext = true;
    for(const char ch : text){
        if(ch == '_'){
            upperNext = true;
            continue;
        }

        if(upperNext)
            inOutText += ToAsciiUpper(ch);
        else
            inOutText += ch;
        upperNext = false;
    }
    if(inOutText.size() == beginSize)
        inOutText += "Value";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendU32Slang(const u32 value, CookString& inOutText){
    char digits[16u];
    inOutText += FormatDecimal(static_cast<usize>(value), digits);
    inOutText += 'u';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendU64AsUint2Slang(const u64 value, CookString& inOutText){
    inOutText += "uint2(";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value & 0xffffffffull), inOutText);
    inOutText += ", ";
    AppendHexU32UnsignedLiteral(static_cast<u32>(value >> 32u), inOutText);
    inOutText += ")";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CookString BuildMaterialBindIncludeGuard(CookArena& arena, const AStringView includePath){
    CookString guard("NWB_GENERATED_MATERIAL_BIND_", arena);
    AppendGeneratedUpperIdentifier(AStringView(includePath), guard);
    return guard;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CookString BuildMaterialBindGeneratedSymbol(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments,
    const AStringView suffix
){
    CookString symbol("NWB_MATERIAL_BIND_", arena);
    bool firstSegment = true;
    for(const AStringView nameSegment : nameSegments){
        if(!firstSegment)
            symbol += '_';
        AppendGeneratedUpperIdentifier(nameSegment, symbol);
        firstSegment = false;
    }
    symbol += suffix;
    return symbol;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CookString BuildMaterialBindAccessorName(
    CookArena& arena,
    const InitializerList<AStringView> nameSegments
){
    CookString functionName("nwbMaterialBindLoad", arena);
    for(const AStringView nameSegment : nameSegments)
        AppendGeneratedPascalIdentifier(nameSegment, functionName);
    return functionName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool RegisterGeneratedMaterialBindSymbol(
    const AStringView includePath,
    const AStringView symbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena
){
    ScratchString scratchSymbol(symbol, scratchArena);
    if(inOutSymbols.insert(Move(scratchSymbol)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': generated symbol '{}' is ambiguous")
        , StringConvert(includePath)
        , StringConvert(symbol)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView MaterialBindFieldLookupFunctionStorageName(const MaterialBlockClass::Enum blockClass){
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return "Constant";
    case MaterialBlockClass::MaterialMutable: return "Mutable";
    default: return AStringView();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AStringView MaterialBindFieldLookupFunctionTypeName(const MaterialLayoutFieldType::Enum fieldType){
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
    static_assert(
        (sizeof(s_TypeNames) / sizeof(s_TypeNames[0]))
        == static_cast<usize>(
            static_cast<u32>(MaterialLayoutFieldType::Float4) - static_cast<u32>(MaterialLayoutFieldType::Bool) + 1u
        )
    );

    if(!IsValidMaterialLayoutFieldType(fieldType))
        return AStringView();

    return s_TypeNames[static_cast<u32>(fieldType) - static_cast<u32>(MaterialLayoutFieldType::Bool)];
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static CookString BuildMaterialBindFieldLookupFunctionName(
    CookArena& arena,
    const MaterialLayoutFieldType::Enum fieldType,
    const MaterialBlockClass::Enum blockClass
){
    CookString functionName(arena);
    const AStringView storageName = MaterialBindFieldLookupFunctionStorageName(blockClass);
    const AStringView typeName = MaterialBindFieldLookupFunctionTypeName(fieldType);
    if(storageName.empty() || typeName.empty())
        return functionName;

    functionName += "nwbMaterialLoad";
    functionName += storageName;
    functionName += typeName;
    return functionName;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindConstantPrefix(
    const AStringView includePath,
    const CookString& symbol,
    const AStringView type,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!RegisterGeneratedMaterialBindSymbol(includePath, AStringView(symbol), inOutSymbols, scratchArena))
        return false;

    inOutSource += "static const ";
    inOutSource += type;
    inOutSource += ' ';
    inOutSource += symbol;
    inOutSource += " = ";
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendMaterialBindConstantSuffix(CookString& inOutSource){
    inOutSource += ";\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindU32Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u32 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU32Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindU64Constant(
    const AStringView includePath,
    const CookString& symbol,
    const u64 value,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    if(!AppendMaterialBindConstantPrefix(includePath, symbol, "uint2", inOutSymbols, scratchArena, inOutSource))
        return false;

    AppendU64AsUint2Slang(value, inOutSource);
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ResolveMaterialBindGeneratedLayoutBlock(
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindTypedLayout& layout,
    MaterialBindTypedLayoutBlockLookupEntry& outBlockEntry,
    const MaterialTypedLayoutBlock*& outBlock
){
    outBlockEntry = {};
    outBlock = nullptr;

    const auto blockIt = layout.blockLookup.find(Name(AStringView(instance.name)));
    if(blockIt == layout.blockLookup.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' has no typed layout block")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlockEntry = blockIt.value();
    if(outBlockEntry.blockIndex >= layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout block index is out of range")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    outBlock = &layout.typedLayoutBlocks[outBlockEntry.blockIndex];
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static u32* MaterialBindStorageByteSizePointer(
    const MaterialBlockClass::Enum blockClass,
    u32& inOutConstantByteSize,
    u32& inOutMutableByteSize
){
    switch(blockClass){
    case MaterialBlockClass::MaterialConstant: return &inOutConstantByteSize;
    case MaterialBlockClass::MaterialMutable: return &inOutMutableByteSize;
    default: return nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ComputeMaterialBindStorageByteSizes(
    const AStringView includePath,
    const MaterialBindTypedLayout& layout,
    u32& outConstantByteSize,
    u32& outMutableByteSize
){
    outConstantByteSize = 0u;
    outMutableByteSize = 0u;

    for(const MaterialTypedLayoutBlock& block : layout.typedLayoutBlocks){
        u32* storageByteSize = MaterialBindStorageByteSizePointer(block.blockClass, outConstantByteSize, outMutableByteSize);
        if(!storageByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block has invalid storage class")
                , StringConvert(includePath)
            );
            return false;
        }
        if(block.byteSize > Limit<u32>::s_Max - *storageByteSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout storage byte size exceeds u32")
                , StringConvert(includePath)
            );
            return false;
        }

        *storageByteSize += block.byteSize;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ComputeMaterialBindBlockStorageByteBegin(
    const AStringView includePath,
    const MaterialBindTypedLayout& layout,
    const u32 blockIndex,
    u32& outByteBegin
){
    outByteBegin = 0u;
    if(blockIndex >= layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block index is out of range")
            , StringConvert(includePath)
        );
        return false;
    }

    const MaterialBlockClass::Enum blockClass = layout.typedLayoutBlocks[blockIndex].blockClass;
    for(u32 currentBlockIndex = 0u; currentBlockIndex < blockIndex; ++currentBlockIndex){
        const MaterialTypedLayoutBlock& block = layout.typedLayoutBlocks[currentBlockIndex];
        if(block.blockClass != blockClass)
            continue;
        if(block.byteSize > Limit<u32>::s_Max - outByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block byte offset exceeds u32")
                , StringConvert(includePath)
            );
            return false;
        }

        outByteBegin += block.byteSize;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindLayoutConstants(
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindEntry& entry,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    u32 constantByteSize = 0u;
    u32 mutableByteSize = 0u;
    if(!ComputeMaterialBindStorageByteSizes(includePath, layout, constantByteSize, mutableByteSize))
        return false;

    const Name interfaceName(AStringView(entry.virtualPath));
    const NameHash& interfaceHash = interfaceName.hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        CookString laneSuffix("INTERFACE_HASH_", arena);
        char laneDigits[16u];
        laneSuffix += FormatDecimal(static_cast<usize>(lane), laneDigits);
        const CookString symbol = BuildMaterialBindGeneratedSymbol(arena, {}, AStringView(laneSuffix));
        if(!AppendMaterialBindU64Constant(
            includePath,
            symbol,
            interfaceHash.qwords[lane],
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
    }

    const CookString layoutHashSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "LAYOUT_HASH");
    const CookString blockCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "BLOCK_COUNT");
    const CookString fieldCountSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "FIELD_COUNT");
    const CookString storageConstantSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "STORAGE_CONSTANT");
    const CookString storageMutableSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "STORAGE_MUTABLE");
    const CookString constantByteSizeSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "CONSTANT_BYTE_SIZE");
    const CookString mutableByteSizeSymbol = BuildMaterialBindGeneratedSymbol(arena, {}, "MUTABLE_BYTE_SIZE");

    if(!AppendMaterialBindU64Constant(includePath, layoutHashSymbol, layout.layoutHash, inOutSymbols, scratchArena, inOutSource))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        blockCountSymbol,
        static_cast<u32>(layout.typedLayoutBlocks.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        fieldCountSymbol,
        static_cast<u32>(layout.typedLayoutFields.size()),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        storageConstantSymbol,
        static_cast<u32>(MaterialBlockClass::MaterialConstant),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        storageMutableSymbol,
        static_cast<u32>(MaterialBlockClass::MaterialMutable),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        constantByteSizeSymbol,
        constantByteSize,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;
    if(!AppendMaterialBindU32Constant(
        includePath,
        mutableByteSizeSymbol,
        mutableByteSize,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    if(entry.instances.size() != layout.typedLayoutBlocks.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': typed layout block count mismatch"), StringConvert(includePath));
        return false;
    }

    for(const MaterialBindInstance& instance : entry.instances){
        MaterialBindTypedLayoutBlockLookupEntry blockEntry;
        const MaterialTypedLayoutBlock* block = nullptr;
        if(!ResolveMaterialBindGeneratedLayoutBlock(includePath, instance, layout, blockEntry, block))
            return false;

        u32 blockStorageByteBegin = 0u;
        if(!ComputeMaterialBindBlockStorageByteBegin(includePath, layout, blockEntry.blockIndex, blockStorageByteBegin))
            return false;

        const CookString storageSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_STORAGE");
        const CookString offsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BYTE_OFFSET");
        const CookString sizeSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name) }, "_BYTE_SIZE");
        if(!AppendMaterialBindU32Constant(
            includePath,
            storageSymbol,
            static_cast<u32>(block->blockClass),
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(
            includePath,
            offsetSymbol,
            blockStorageByteBegin,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        if(!AppendMaterialBindU32Constant(includePath, sizeSymbol, block->byteSize, inOutSymbols, scratchArena, inOutSource))
            return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindFieldConstants(
    const AStringView includePath,
    const MaterialBindStruct& bindStruct,
    const MaterialBindInstance& instance,
    const MaterialBindField& field,
    const CookString& keySymbol,
    const CookString& defaultSymbol,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    const AStringView defaultAttribute = field.defaultArgument();
    if(defaultAttribute.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' must declare a default attribute")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }

    ACompactString keyText;
    if(!BuildMaterialBindParameterKey(AStringView(instance.name), AStringView(field.name), keyText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' exceeds ACompactString capacity")
            , StringConvert(includePath)
            , StringConvert(bindStruct.name)
            , StringConvert(field.name)
        );
        return false;
    }
    const u64 keyHash = ComputeMaterialBindParameterKeyHash(keyText.view());

    if(!AppendMaterialBindU64Constant(
        includePath,
        keySymbol,
        keyHash,
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    if(!AppendMaterialBindConstantPrefix(
        includePath,
        defaultSymbol,
        AStringView(field.type),
        inOutSymbols,
        scratchArena,
        inOutSource
    ))
        return false;

    inOutSource += defaultAttribute;
    AppendMaterialBindConstantSuffix(inOutSource);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AppendMaterialBindFieldAccessor(
    const MaterialBindField& field,
    const CookString& byteOffsetSymbol,
    const CookString& functionName,
    const AStringView loadFunctionName,
    CookString& inOutSource
){
    inOutSource += field.type;
    inOutSource += ' ';
    inOutSource += functionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    return ";
    inOutSource += loadFunctionName;
    inOutSource += "(instance, ";
    inOutSource += byteOffsetSymbol;
    inOutSource += ");\n";
    inOutSource += "}\n\n";
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool AppendMaterialBindGeneratedInstance(
    CookArena& arena,
    const AStringView includePath,
    const MaterialBindInstance& instance,
    const MaterialBindStruct& bindStruct,
    const MaterialBindTypedLayout& layout,
    ScratchHashSet<ScratchString>& inOutSymbols,
    ScratchArena& scratchArena,
    CookString& inOutSource
){
    inOutSource += "\n";
    AppendMaterialBindGeneratedSeparator(inOutSource, 3u);

    MaterialBindTypedLayoutBlockLookupEntry layoutBlockEntry;
    const MaterialTypedLayoutBlock* layoutBlock = nullptr;
    if(!ResolveMaterialBindGeneratedLayoutBlock(
        includePath,
        instance,
        layout,
        layoutBlockEntry,
        layoutBlock
    ))
        return false;

    if(layoutBlock->fieldCount != bindStruct.fields.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field count mismatch")
            , StringConvert(includePath)
            , StringConvert(instance.name)
        );
        return false;
    }

    u32 layoutBlockStorageByteBegin = 0u;
    if(!ComputeMaterialBindBlockStorageByteBegin(
        includePath,
        layout,
        layoutBlockEntry.blockIndex,
        layoutBlockStorageByteBegin
    ))
        return false;

    for(u32 fieldOffset = 0u; fieldOffset < layoutBlock->fieldCount; ++fieldOffset){
        const usize layoutFieldIndex = static_cast<usize>(layoutBlock->fieldBegin) + fieldOffset;
        if(layoutFieldIndex >= layout.typedLayoutFields.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' typed layout field range exceeds layout")
                , StringConvert(includePath)
                , StringConvert(instance.name)
            );
            return false;
        }

        const MaterialBindField& field = bindStruct.fields[fieldOffset];
        const MaterialTypedLayoutField& layoutField = layout.typedLayoutFields[layoutFieldIndex];
        if(layoutField.fieldName != Name(AStringView(field.name))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' typed layout metadata mismatch")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }
        if(layoutField.offset > Limit<u32>::s_Max - layoutBlockStorageByteBegin){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' byte offset exceeds u32")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
            );
            return false;
        }

        const CookString loadFunctionName = BuildMaterialBindFieldLookupFunctionName(
            arena,
            layoutField.fieldType,
            layoutBlock->blockClass
        );
        if(loadFunctionName.empty()){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': field '{}.{}' has unsupported load type '{}'")
                , StringConvert(includePath)
                , StringConvert(bindStruct.name)
                , StringConvert(field.name)
                , StringConvert(field.type)
            );
            return false;
        }

        const CookString keySymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_KEY");
        const CookString defaultSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_DEFAULT");
        const CookString byteOffsetSymbol =
            BuildMaterialBindGeneratedSymbol(arena, { AStringView(instance.name), AStringView(field.name) }, "_BYTE_OFFSET");
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
        if(!RegisterGeneratedMaterialBindSymbol(
            includePath,
            AStringView(functionName),
            inOutSymbols,
            scratchArena
        ))
            return false;

        if(!AppendMaterialBindFieldConstants(
            includePath,
            bindStruct,
            instance,
            field,
            keySymbol,
            defaultSymbol,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        const u32 fieldByteOffset = layoutBlockStorageByteBegin + layoutField.offset;
        if(!AppendMaterialBindU32Constant(
            includePath,
            byteOffsetSymbol,
            fieldByteOffset,
            inOutSymbols,
            scratchArena,
            inOutSource
        ))
            return false;
        inOutSource += '\n';
        AppendMaterialBindFieldAccessor(field, byteOffsetSymbol, functionName, AStringView(loadFunctionName), inOutSource);
        inOutSource += '\n';
    }

    const CookString blockFunctionName = BuildMaterialBindAccessorName(arena, { AStringView(instance.name) });
    if(!RegisterGeneratedMaterialBindSymbol(
        includePath,
        AStringView(blockFunctionName),
        inOutSymbols,
        scratchArena
    ))
        return false;

    inOutSource += bindStruct.name;
    inOutSource += ' ';
    inOutSource += blockFunctionName;
    inOutSource += "(const NwbMeshInstanceData instance){\n";
    inOutSource += "    ";
    inOutSource += bindStruct.name;
    inOutSource += " value;\n";
    for(const MaterialBindField& field : bindStruct.fields){
        const CookString functionName =
            BuildMaterialBindAccessorName(arena, { AStringView(instance.name), AStringView(field.name) });
        inOutSource += "    value.";
        inOutSource += field.name;
        inOutSource += " = ";
        inOutSource += functionName;
        inOutSource += "(instance);\n";
    }
    inOutSource += "    return value;\n";
    inOutSource += "}\n\n";

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildMaterialBindIncludeSourceImpl(
    CookArena& arena,
    const MaterialBindEntry& entry,
    CookString& outSource,
    ScratchArena& scratchArena
){
    outSource.clear();

    CookString includePath(arena);
    if(!BuildMaterialBindIncludeVirtualPathImpl(arena, entry, includePath))
        return false;

    const CookString includeGuard = BuildMaterialBindIncludeGuard(arena, AStringView(includePath));

    MaterialBindTypedLayout layout(arena);
    if(!BuildMaterialBindTypedLayout(
        entry,
        Name(AStringView(entry.virtualPath)),
        layout,
        scratchArena
    ))
        return false;

    ScratchHashSet<ScratchString> generatedSymbols{
        0,
        Hasher<ScratchString>(),
        EqualTo<ScratchString>(),
        scratchArena
    };

    outSource += "// generated by NWBLot material bind cook\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef ";
    outSource += includeGuard;
    outSource += "\n#define ";
    outSource += includeGuard;
    outSource += "\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#ifndef NWB_MATERIAL_TYPED_BINDING\n";
    outSource += "#error \"generated material bind includes require mesh/authoring.slangi\"\n";
    outSource += "#endif\n\n";
    outSource += "#ifndef NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    outSource += "#error \"generated material bind includes require mesh/authoring.slangi\"\n";
    outSource += "#endif\n\n";
    outSource += "#if NWB_MATERIAL_TYPED_BINDING != NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\n";
    outSource += "#error \"generated material bind accessors require NWB_MATERIAL_TYPED_BINDING to match NWB_MATERIAL_TYPED_BINDING_REQUIRED_VALUE\"\n";
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    if(!AppendMaterialBindLayoutConstants(
        arena,
        AStringView(includePath),
        entry,
        layout,
        generatedSymbols,
        scratchArena,
        outSource
    ))
        return false;

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);

    for(const MaterialBindStruct& bindStruct : entry.structs){
        if(!RegisterGeneratedMaterialBindSymbol(
            AStringView(includePath),
            AStringView(bindStruct.name),
            generatedSymbols,
            scratchArena
        ))
            return false;

        outSource += "struct ";
        outSource += bindStruct.name;
        outSource += "{\n";
        for(const MaterialBindField& field : bindStruct.fields){
            outSource += "    ";
            outSource += field.type;
            outSource += ' ';
            outSource += field.name;
            outSource += ";\n";
        }
        outSource += "};\n\n";
    }

    for(const MaterialBindInstance& instance : entry.instances){
        const MaterialBindStruct* bindStruct = entry.findStruct(AStringView(instance.type));
        if(!bindStruct){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include '{}': instance '{}' references unknown struct type '{}'")
                , StringConvert(includePath)
                , StringConvert(instance.name)
                , StringConvert(instance.type)
            );
            return false;
        }

        if(!AppendMaterialBindGeneratedInstance(
            arena,
            AStringView(includePath),
            instance,
            *bindStruct,
            layout,
            generatedSymbols,
            scratchArena,
            outSource
        ))
            return false;
    }

    outSource += "\n";
    AppendMaterialBindGeneratedSeparator(outSource, 3u);
    outSource += "#endif\n\n\n";
    AppendMaterialBindGeneratedSeparator(outSource, 1u);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool PrepareMaterialBindIncludeRoot(const Path& includeRoot){
    ErrorCode errorCode;
    if(!RemoveAllIfExists(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to clear generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    errorCode.clear();
    if(!EnsureDirectories(includeRoot, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include directory '{}': {}")
            , PathToString<tchar>(includeRoot)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool EmitMaterialBindIncludes(
    CookArena& arena,
    const Path& cacheDirectory,
    const AStringView configurationSafeName,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    Path& outIncludeRoot,
    ScratchArena& scratchArena
){
    outIncludeRoot.clear();
    outIncludeRoot = BuildMaterialBindIncludeRoot(cacheDirectory, configurationSafeName, scratchArena);
    if(!PrepareMaterialBindIncludeRoot(outIncludeRoot))
        return false;
    if(materialBindEntries.empty())
        return true;

    CookHashSet<CookString> seenIncludePaths{arena};
    seenIncludePaths.reserve(materialBindEntries.size());

    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        CookString includePath(arena);
        if(!BuildMaterialBindIncludeVirtualPathImpl(arena, bindEntry, includePath))
            return false;
        if(!seenIncludePaths.insert(includePath).second){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: duplicate material bind include path '{}'")
                , StringConvert(includePath)
            );
            return false;
        }

        CookString generatedSource{arena};
        if(!BuildMaterialBindIncludeSourceImpl(arena, bindEntry, generatedSource, scratchArena))
            return false;

        const Path outputPath = outIncludeRoot / includePath.c_str();
        ErrorCode errorCode;
        if(!EnsureDirectories(outputPath.parent_path(), errorCode)){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to create generated include parent '{}': {}")
                , PathToString<tchar>(outputPath.parent_path())
                , StringConvert(errorCode.message())
            );
            return false;
        }

        if(!WriteTextFile(outputPath, AStringView(generatedSource))){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind include generation: failed to write generated include '{}'")
                , PathToString<tchar>(outputPath)
            );
            return false;
        }
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

