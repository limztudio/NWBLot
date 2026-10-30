// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/assert.h>
#include <global/auto_registration.h>
#include <global/binary.h>
#include <global/sha1.h>
#include <global/sha256.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoMetadataParser{
    AssetDocumentMetadataParseFunction documentFunction = nullptr;
    AssetValueMetadataParseFunction valueFunction = nullptr;
};

::AutoRegistrationQueue<AutoMetadataParser, AssetArena>& QueryAutoMetadataParserQueue(){
    static ::AutoRegistrationQueue<AutoMetadataParser, AssetArena> s_Queue(AssetsArenaScope::s_MetadataParserQueueArena);
    return s_Queue;
}

::AutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetArena>& QueryAutoAssetBunchExpanderQueue(){
    static ::AutoRegistrationQueue<NotNull<AssetBunchExpandFunction>, AssetArena> s_Queue(AssetsArenaScope::s_BunchExpanderQueueArena);
    return s_Queue;
}

::AutoRegistrationQueue<Name, AssetArena>& QueryAutoCompositeRootTypeQueue(){
    static ::AutoRegistrationQueue<Name, AssetArena> s_Queue(AssetsArenaScope::s_CompositeRootTypeQueueArena);
    return s_Queue;
}

[[nodiscard]] static Expected<Core::Metascript::Document> ParseMetascriptDocumentText(CookArena& cookArena, const Path& nwbFilePath, const AStringView metaText){
    Core::Metascript::Document doc(cookArena);
    if(!doc.parse(metaText)){
        for(const Core::Metascript::ParseError& err : doc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' parse error at {}:{}: {}"), PathToString<tchar>(nwbFilePath), err.line, err.column, ConvertString(AStringView(err.message.data(), err.message.size())));
        }
        return MakeUnexpected(Failure{});
    }
    return doc;
}

[[nodiscard]] static b8 IsNonStandaloneAssetType(const Name& assetType)noexcept{
    static constexpr Name s_MaterialType("material");
    static constexpr Name s_MeshSubsystemType("mesh_subsystem");
    return assetType == s_MaterialType || assetType == s_MeshSubsystemType;
}

[[nodiscard]] static AStringView ExtractFirstIdentifierToken(const AStringView text)noexcept{
    usize index = 0u;
    while(index < text.size()){
        const char ch = text[index];
        if(ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n'){
            ++index;
            continue;
        }
        if(ch == '/' && index + 1u < text.size()){
            if(text[index + 1u] == '/'){
                index += 2u;
                while(index < text.size() && text[index] != '\n')
                    ++index;
                continue;
            }
            if(text[index + 1u] == '*'){
                index += 2u;
                while(index + 1u < text.size() && !(text[index] == '*' && text[index + 1u] == '/'))
                    ++index;
                if(index + 1u < text.size())
                    index += 2u;
                else
                    return {};
                continue;
            }
        }
        break;
    }

    const usize start = index;
    while(index < text.size()){
        const char ch = text[index];
        const b8 isIdent = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_';
        if(!isIdent)
            break;
        ++index;
    }
    return text.substr(start, index - start);
}

[[nodiscard]] static b8 IsLikelyNonStandaloneFirstToken(const AStringView firstToken)noexcept{
    return firstToken == "material" || firstToken == "mesh_subsystem" || firstToken == "include" || firstToken == "shader" || firstToken.ends_with("_shader");
}

static void BuildNwbRelativePathText(const Path& repoRoot, const DiscoveredNwbFile& discoveredNwbFile, ScratchArena& scratchArena, ScratchString& outRelativePathText){
    const ::Path<ScratchArena> sourcePath(scratchArena, discoveredNwbFile.filePath.lexicallyNormal());
    if(!repoRoot.empty()){
        const ::Path<ScratchArena> rootPath(scratchArena, repoRoot.lexicallyNormal());
        const ::Path<ScratchArena> relativePath = sourcePath.lexicallyRelative(rootPath);
        if(AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outRelativePathText))
            return;
    }

    const ::Path<ScratchArena> assetRootPath(scratchArena, discoveredNwbFile.assetRoot.lexicallyNormal());
    const ::Path<ScratchArena> assetRelativePath = sourcePath.lexicallyRelative(assetRootPath);
    if(AssetPathsDetail::BuildRelativeAssetPathText(assetRelativePath, outRelativePathText))
        return;

    outRelativePathText = discoveredNwbFile.normalizedPathText;
    for(char& ch : outRelativePathText){
        if(ch == '\\')
            ch = '/';
        else
            ch = Canonicalize(ch);
    }
}

[[nodiscard]] static b8 TryInspectPairedSidecar(const Path& nwbFilePath, const AStringView metaText, const AStringView firstToken, ScratchArena& scratchArena, Optional<Sha256Digest>& outSidecarSha256){
    static constexpr usize s_MaxSmallMetaScanBytes = 65536u;
    static constexpr AStringView s_TexExtension = ".tex";
    static constexpr AStringView s_FontExtension = ".font";

    outSidecarSha256.reset();
    const b8 requiresTex = (firstToken == "texture");
    const b8 requiresFont = (firstToken == "font" || firstToken == "font_atlas");
    const b8 mayHaveBunchFont = !requiresFont && metaText.size() <= s_MaxSmallMetaScanBytes && metaText.find("font") != AStringView::npos;
    if(!requiresTex && !requiresFont && !mayHaveBunchFont)
        return true;

    ::Path<ScratchArena> sidecarPath(scratchArena, nwbFilePath);
    sidecarPath.replaceExtension(requiresTex ? s_TexExtension : s_FontExtension);
    if(mayHaveBunchFont){
        const auto exists = FileExists(sidecarPath);
        if(!exists || !*exists)
            return true;
    }

    Vector<u8, ScratchArena> sidecarBytes(scratchArena);
    if(!ReadBinaryFile(sidecarPath, sidecarBytes))
        return false;

    outSidecarSha256 = ComputeSha256(BinaryByteView{ sidecarBytes.data(), sidecarBytes.size() });
    return true;
}

[[nodiscard]] static Sha256Digest ComputeNwbParamChecksum(
    const DiscoveredNwbFile& discoveredNwbFile,
    const AStringView relativePathText,
    const AStringView metaText,
    const AStringView firstToken,
    ScratchArena& scratchArena,
    b8& outCanUseCache
){
    outCanUseCache = true;
    const auto derivedVirtualPath = AssetPathsDetail::BuildDerivedAssetVirtualPathText(
        scratchArena,
        discoveredNwbFile.assetRoot,
        discoveredNwbFile.virtualRoot.view(),
        discoveredNwbFile.filePath,
        scratchArena
    );
    if(!derivedVirtualPath)
        outCanUseCache = false;
    const AStringView derivedVirtualPathView = derivedVirtualPath ? AStringView(*derivedVirtualPath) : AStringView{};

    Optional<Sha256Digest> sidecarSha256;
    if(!TryInspectPairedSidecar(discoveredNwbFile.filePath, metaText, firstToken, scratchArena, sidecarSha256))
        outCanUseCache = false;

    const Sha256Digest metaSha256 = ComputeSha256(BinaryByteView{ reinterpret_cast<const u8*>(metaText.data()), metaText.size() });

    Vector<u8, ScratchArena> shaInput(scratchArena);
    shaInput.reserve(sizeof(u16) + relativePathText.size() + discoveredNwbFile.virtualRoot.view().size() + derivedVirtualPathView.size() + sizeof(Sha256Digest) * 2u + 16u);
    AppendPOD(shaInput, s_NwbAssetCacheVersion);
    if(
        !AppendString(shaInput, relativePathText)
        || !AppendString(shaInput, discoveredNwbFile.virtualRoot.view())
        || !AppendString(shaInput, derivedVirtualPathView)
    )
        outCanUseCache = false;
    AppendPOD(shaInput, metaSha256);
    const u8 sidecarFlag = sidecarSha256.has_value() ? 1u : 0u;
    AppendPOD(shaInput, sidecarFlag);
    if(sidecarSha256)
        AppendPOD(shaInput, *sidecarSha256);

    return ComputeSha256(BinaryByteView{ shaInput.data(), shaInput.size() });
}

namespace NwbCacheLoadResult{
    enum Enum : u8{
        Miss,
        Hit,
        Error
    };
};

struct DecodedCachedAssetEntry{
    u32 nwbSubIndex = 0u;
    Name assetType = s_NameNone;
    Name virtualPath = s_NameNone;
    Span<const u8> payloadBytes;
};

[[nodiscard]] static NwbCacheLoadResult::Enum TryLoadNwbFileAssetCache(
    CookArena& cookArena,
    const NwbFileAssetCacheRecord& record,
    const usize nwbFileIndex,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata
){
    Vector<u8, ScratchArena> cacheBytes(scratchArena);
    if(!ReadBinaryFile(record.cacheFilePath, cacheBytes))
        return NwbCacheLoadResult::Miss;

    usize cursor = 0u;
    const auto header = ReadPOD<NwbAssetCacheFileHeader>(cacheBytes, cursor);
    if(!header)
        return NwbCacheLoadResult::Miss;
    if(header->magic != s_NwbAssetCacheMagic || header->version != s_NwbAssetCacheVersion || header->headerSize != sizeof(NwbAssetCacheFileHeader))
        return NwbCacheLoadResult::Miss;
    if((header->flags & s_NwbAssetCacheFlagStandalone) == 0u || header->entryCount == 0u)
        return NwbCacheLoadResult::Miss;
    if(header->paramSha256 != record.paramSha256)
        return NwbCacheLoadResult::Miss;
    if(header->payloadSectionSize > static_cast<u64>(Limit<usize>::s_Max))
        return NwbCacheLoadResult::Miss;

    const usize payloadSectionSize = static_cast<usize>(header->payloadSectionSize);
    if(cursor > cacheBytes.size() || cacheBytes.size() - cursor != payloadSectionSize)
        return NwbCacheLoadResult::Miss;
    if(ComputeFnv64Bytes(cacheBytes.data() + cursor, payloadSectionSize) != header->payloadSectionHashFnv64)
        return NwbCacheLoadResult::Miss;

    const auto cachedRelativePath = ReadString(cacheBytes, cursor);
    if(!cachedRelativePath || *cachedRelativePath != AStringView(record.relativePathText))
        return NwbCacheLoadResult::Miss;

    Vector<DecodedCachedAssetEntry, ScratchArena> decodedEntries(scratchArena);
    decodedEntries.reserve(header->entryCount);
    for(u32 entryIndex = 0u; entryIndex < header->entryCount; ++entryIndex){
        const auto nwbSubIndex = ReadPOD<u32>(cacheBytes, cursor);
        const auto assetTypeHash = ReadPOD<NameHash>(cacheBytes, cursor);
        const auto virtualPathHash = ReadPOD<NameHash>(cacheBytes, cursor);
        const auto virtualPathText = ReadString(cacheBytes, cursor);
        const auto payloadSize = ReadPOD<u64>(cacheBytes, cursor);
        const auto payloadHashFnv64 = ReadPOD<u64>(cacheBytes, cursor);
        if(!nwbSubIndex || !assetTypeHash || !virtualPathHash || !virtualPathText || !payloadSize || !payloadHashFnv64)
            return NwbCacheLoadResult::Miss;
        if(*payloadSize > static_cast<u64>(Limit<usize>::s_Max))
            return NwbCacheLoadResult::Miss;

        const usize payloadByteCount = static_cast<usize>(*payloadSize);
        if(!CanReadBytes(cacheBytes, cursor, payloadByteCount))
            return NwbCacheLoadResult::Miss;
        const u8* const payloadData = cacheBytes.data() + cursor;
        cursor += payloadByteCount;
        if(ComputeFnv64Bytes(payloadData, payloadByteCount) != *payloadHashFnv64)
            return NwbCacheLoadResult::Miss;

        const Name assetType(*assetTypeHash);
        if(!assetType || IsNonStandaloneAssetType(assetType) || !outMetadata.entryRegistry.has(assetType))
            return NwbCacheLoadResult::Miss;

        Name virtualPath(*virtualPathHash);
        if(!virtualPathText->empty()){
            const auto parsedVirtualPath = Name::FromText(*virtualPathText);
            if(parsedVirtualPath && parsedVirtualPath->identityHash() == *virtualPathHash)
                virtualPath = *parsedVirtualPath;
        }
        if(!virtualPath)
            return NwbCacheLoadResult::Miss;

        decodedEntries.push_back(DecodedCachedAssetEntry{
            .nwbSubIndex = *nwbSubIndex,
            .assetType = assetType,
            .virtualPath = virtualPath,
            .payloadBytes = Span<const u8>(payloadData, payloadByteCount),
        });
    }
    if(cursor != cacheBytes.size())
        return NwbCacheLoadResult::Miss;

    for(const DecodedCachedAssetEntry& decoded : decodedEntries){
        CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, decoded.nwbSubIndex };
        if(!outMetadata.entryRegistry.appendCachedEntry(decoded.assetType, CookEntrySourceLocation{ nwbFileIndex, decoded.nwbSubIndex }, decoded.virtualPath, decoded.payloadBytes, parseContext))
            return NwbCacheLoadResult::Error;
    }
    return NwbCacheLoadResult::Hit;
}

[[nodiscard]] static b8 ParseDeclaredAssetItem(
    CookArena& cookArena,
    const DiscoveredNwbFile& discoveredNwbFile,
    const usize nwbFileIndex,
    const u32 nwbSubIndex,
    const Name assetType,
    const Name virtualPath,
    const Core::Metascript::Value& asset,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    AssetValueMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, virtualPath, asset, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedValueMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(IsNonStandaloneAssetType(assetType))
        inOutStandalone = false;

    CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, nwbSubIndex };
    return outMetadata.entryRegistry.parseValue(assetType, virtualPath, discoveredNwbFile.filePath, asset, parseContext);
}

[[nodiscard]] static b8 ParseSingleAssetDocument(
    CookArena& cookArena,
    const DiscoveredNwbFile& discoveredNwbFile,
    const usize nwbFileIndex,
    const Core::Metascript::Document& doc,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    CookEntryPathHashSet& seenPropertyAssetPathHashes,
    ParsedAssetMetadata& outMetadata,
    b8& inOutStandalone
){
    const auto* assetDeclaration = FindMetadataAssetDeclaration(doc);
    if(!assetDeclaration){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' is missing asset declaration '{}'"), PathToString<tchar>(discoveredNwbFile.filePath), ConvertString(s_MetadataAssetVariable));
        return false;
    }
    const AStringView rawAssetTypeText(assetDeclaration->type.data(), assetDeclaration->type.size());
    const auto assetTypeResult = ToName(rawAssetTypeText);
    if(!assetTypeResult){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset type contains an embedded null in meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
        return false;
    }
    const Name assetType = *assetTypeResult ? **assetTypeResult : s_NameNone;
    AssetDocumentMetadataParseContext metadataParseContext{ cookArena, discoveredNwbFile, assetType, doc, outMetadata, scratchArena };
    const AssetMetadataParseResult::Enum metadataParseResult = TryAutoCollectedDocumentMetadataParsers(metadataParseContext);
    if(metadataParseResult == AssetMetadataParseResult::Parsed){
        inOutStandalone = false;
        return true;
    }
    if(metadataParseResult == AssetMetadataParseResult::Error)
        return false;

    if(outMetadata.entryRegistry.has(assetType)){
        if(IsNonStandaloneAssetType(assetType))
            inOutStandalone = false;

        CookEntryParseContext parseContext{ cookArena, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, nwbFileIndex, 0u };
        return outMetadata.entryRegistry.parseDocument(assetType, discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, parseContext);
    }

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: unsupported asset type '{}' in meta '{}'"), ConvertString(rawAssetTypeText), PathToString<tchar>(discoveredNwbFile.filePath));
    return false;
}


template<typename ContextT, typename FunctionT>
[[nodiscard]] static AssetMetadataParseResult::Enum TryAutoCollectedMetadataParsers(ContextT& context, FunctionT AutoMetadataParser::* const member){
    Vector<AutoMetadataParser, Core::Alloc::ScratchArena> parsers{context.scratchArena};
    QueryAutoMetadataParserQueue().copyTo(parsers);

    for(const AutoMetadataParser& parser : parsers){
        const FunctionT function = parser.*member;
        if(function == nullptr)
            continue;

        const AssetMetadataParseResult::Enum result = function(context);
        if(result == AssetMetadataParseResult::Unsupported)
            continue;
        return result;
    }

    return AssetMetadataParseResult::Unsupported;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetMetadataParserAutoRegistrar::AssetMetadataParserAutoRegistrar(const AssetDocumentMetadataParseFunction documentFunction, const AssetValueMetadataParseFunction valueFunction){
    if(documentFunction == nullptr && valueFunction == nullptr)
        return;

    auto& queue = __hidden_cook_metadata::QueryAutoMetadataParserQueue();
    queue.appendUnique(
        __hidden_cook_metadata::AutoMetadataParser{ documentFunction, valueFunction },
        [](const __hidden_cook_metadata::AutoMetadataParser& lhs, const __hidden_cook_metadata::AutoMetadataParser& rhs)noexcept{
            return lhs.documentFunction == rhs.documentFunction && lhs.valueFunction == rhs.valueFunction;
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetBunchExpanderAutoRegistrar::AssetBunchExpanderAutoRegistrar(const AssetBunchExpandFunction function){
    ::RegisterAutoFactory(__hidden_cook_metadata::QueryAutoAssetBunchExpanderQueue(), function);
}

CompositeRootAssetTypeAutoRegistrar::CompositeRootAssetTypeAutoRegistrar(const Name& assetType){
    RegisterCompositeRootAssetType(assetType);
}

void RegisterCompositeRootAssetType(const Name& assetType){
    if(!assetType)
        return;

    __hidden_cook_metadata::QueryAutoCompositeRootTypeQueue().appendUnique(
        assetType,
        [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
    );
}

b8 IsCompositeRootAssetType(const Name& assetType){
    if(!assetType)
        return false;

    return __hidden_cook_metadata::QueryAutoCompositeRootTypeQueue().contains(
        assetType,
        [](const Name& lhs, const Name& rhs)noexcept{ return lhs == rhs; }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<DiscoveredNwbFileVector> DiscoverFilesWithExtension(CookArena& cookArena, const Span<const ResolvedAssetRoot> assetRoots, const AStringView expectedExtension, ScratchArena& scratchArena){
    DiscoveredNwbFileVector files(cookArena);
    HashSet<u64, ScratchArena, Hasher<u64>, EqualTo<u64>> seenPathHashes(0, Hasher<u64>(), EqualTo<u64>(), scratchArena);

    for(const ResolvedAssetRoot& assetRoot : assetRoots){
        const auto directory = IsDirectory(assetRoot.path);
        if(!directory || !*directory){
            if(!directory){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to query asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(directory.error().message()));
                return MakeUnexpected(Failure{});
            }

            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: asset root is not a directory: '{}'"), PathToString<tchar>(assetRoot.path));
            return MakeUnexpected(Failure{});
        }

        const ::Path<ScratchArena> scratchRoot(scratchArena, assetRoot.path);
        const auto entries = RecursiveDirectoryIterator<ScratchArena>::Create(scratchRoot);
        if(!entries){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: error scanning asset root '{}': {}"), PathToString<tchar>(assetRoot.path), ConvertString(entries.error().message()));
            return MakeUnexpected(Failure{});
        }
        files.reserve(AddSize(files.size(), static_cast<usize>(entries->end() - entries->begin())));
        seenPathHashes.reserve(AddSize(seenPathHashes.size(), static_cast<usize>(entries->end() - entries->begin())));
        for(const auto& dirEntry : *entries){
            const auto isRegularFile = dirEntry.isRegularFile();
            if(!isRegularFile){
                NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to inspect '{}' while scanning '{}': {}"), PathToString<tchar>(dirEntry.path()), PathToString<tchar>(assetRoot.path), ConvertString(isRegularFile.error().message()));
                return MakeUnexpected(Failure{});
            }
            if(!*isRegularFile)
                continue;

            const ::Path<ScratchArena>& filePath = dirEntry.path();
            ScratchString extension = PathToString(scratchArena, filePath.extensionView());
            CanonicalizeTextInPlace(extension);
            if(extension != expectedExtension)
                continue;

            ScratchString normalizedPath = PathToString(scratchArena, filePath.lexicallyNormal());
#if defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif
            if(!seenPathHashes.insert(ComputeFnv64Text(normalizedPath)).second)
                continue;
#if !defined(NWB_PLATFORM_WINDOWS)
            CanonicalizeTextInPlace(normalizedPath);
#endif

            files.emplace_back(cookArena, assetRoot.path, filePath, normalizedPath, assetRoot.virtualRoot);
        }
    }

    Sort(files.begin(), files.end(), [](const DiscoveredNwbFile& lhs, const DiscoveredNwbFile& rhs)noexcept{
        return lhs.normalizedPathText < rhs.normalizedPathText;
    });

    return files;
}

b8 AddPlannedFileCount(const u64 additionalFileCount, u64& inOutPlannedFileCount){
    if(inOutPlannedFileCount > Limit<u64>::s_Max - additionalFileCount){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: planned file count overflow"));
        return false;
    }

    inOutPlannedFileCount += additionalFileCount;
    return true;
}

AssetMetadataParseResult::Enum TryAutoCollectedDocumentMetadataParsers(AssetDocumentMetadataParseContext& context){
    return __hidden_cook_metadata::TryAutoCollectedMetadataParsers(context, &__hidden_cook_metadata::AutoMetadataParser::documentFunction);
}

AssetMetadataParseResult::Enum TryAutoCollectedValueMetadataParsers(AssetValueMetadataParseContext& context){
    return __hidden_cook_metadata::TryAutoCollectedMetadataParsers(context, &__hidden_cook_metadata::AutoMetadataParser::valueFunction);
}

Expected<ExpandedAssetMetadataVector, AssetBunchExpandFailure::Enum> TryAutoCollectedAssetBunchExpanders(const AssetBunchExpandContext& context){
    Vector<NotNull<AssetBunchExpandFunction>, Core::Alloc::ScratchArena> functions{context.scratchArena};
    __hidden_cook_metadata::QueryAutoAssetBunchExpanderQueue().copyTo(functions);

    for(const NotNull<AssetBunchExpandFunction> function : functions){
        auto result = (*function)(context);
        if(!result && result.error() == AssetBunchExpandFailure::Unsupported)
            continue;
        return result;
    }

    return MakeUnexpected(AssetBunchExpandFailure::Unsupported);
}

b8 ParseAssetMetadata(
    CookArena& cookArena,
    const Span<const DiscoveredNwbFile> nwbFiles,
    Core::CpuTaskScheduler& cpuScheduler,
    ScratchArena& scratchArena,
    ParsedAssetMetadata& outMetadata,
    const Path& repoRoot,
    const Path& assetCacheDirectory
){
    CookEntryPathHashSet seenPropertyAssetPathHashes(0, Hasher<NameHash>(), EqualTo<NameHash>(), scratchArena);

    seenPropertyAssetPathHashes.reserve(nwbFiles.size());

    const b8 useAssetCache = !assetCacheDirectory.empty();
    outMetadata.fileCacheRecords.clear();
    if(useAssetCache){
        outMetadata.fileCacheRecords.reserve(nwbFiles.size());
        for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex)
            outMetadata.fileCacheRecords.emplace_back(cookArena);
    }

    ScratchString metaText{scratchArena};
    ScratchString relativePathText{scratchArena};
    ScratchString cacheFileName{scratchArena};

    b8 parsedAnyMetadata = false;
    for(usize fileIndex = 0u; fileIndex < nwbFiles.size(); ++fileIndex){
        const DiscoveredNwbFile& discoveredNwbFile = nwbFiles[fileIndex];
        metaText.clear();
        if(!ReadTextFile(discoveredNwbFile.filePath, metaText)){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to read meta '{}'"), PathToString<tchar>(discoveredNwbFile.filePath));
            return false;
        }
        StripUtf8Bom(metaText);

        if(useAssetCache){
            NwbFileAssetCacheRecord& cacheRecord = outMetadata.fileCacheRecords[fileIndex];
            __hidden_cook_metadata::BuildNwbRelativePathText(repoRoot, discoveredNwbFile, scratchArena, relativePathText);
            cacheRecord.relativePathText.assign(relativePathText.data(), relativePathText.size());

            const Sha1Digest pathSha1 = ComputeSha1Text(AStringView(relativePathText));
            cacheFileName.clear();
            cacheFileName.reserve(Sha1Detail::s_HexTextLength);
            AppendSha1Hex(pathSha1, cacheFileName);
            cacheRecord.cacheFilePath = assetCacheDirectory / cacheFileName;

            const AStringView firstToken = __hidden_cook_metadata::ExtractFirstIdentifierToken(AStringView(metaText));
            b8 canUseCache = false;
            cacheRecord.paramSha256 = __hidden_cook_metadata::ComputeNwbParamChecksum(
                discoveredNwbFile,
                AStringView(relativePathText),
                AStringView(metaText),
                firstToken,
                scratchArena,
                canUseCache
            );
            cacheRecord.canWriteCache = canUseCache;

#if !defined(NWB_BUILDMODE)
            if(canUseCache && !__hidden_cook_metadata::IsLikelyNonStandaloneFirstToken(firstToken)){
                const auto cacheResult = __hidden_cook_metadata::TryLoadNwbFileAssetCache(
                    cookArena,
                    cacheRecord,
                    fileIndex,
                    cpuScheduler,
                    scratchArena,
                    seenPropertyAssetPathHashes,
                    outMetadata
                );
                if(cacheResult == __hidden_cook_metadata::NwbCacheLoadResult::Error)
                    return false;
                if(cacheResult == __hidden_cook_metadata::NwbCacheLoadResult::Hit){
                    cacheRecord.standalone = true;
                    cacheRecord.loadedFromCache = true;
                    parsedAnyMetadata = true;
                    continue;
                }
            }
#endif
        }

        const auto parsedDoc = __hidden_cook_metadata::ParseMetascriptDocumentText(cookArena, discoveredNwbFile.filePath, AStringView(metaText));
        if(!parsedDoc)
            return false;
        const auto& doc = *parsedDoc;

        b8 fileIsStandalone = true;
        AssetBunchExpandContext assetBunchExpandContext{ discoveredNwbFile.assetRoot, discoveredNwbFile.virtualRoot.view(), discoveredNwbFile.filePath, doc, scratchArena };
        auto expandedAssets = TryAutoCollectedAssetBunchExpanders(assetBunchExpandContext);
        if(!expandedAssets && expandedAssets.error() == AssetBunchExpandFailure::Error)
            return false;
        if(expandedAssets){
            if(expandedAssets->size() > static_cast<usize>(Limit<u32>::s_Max))
                return false;
            for(usize itemIndex = 0u; itemIndex < expandedAssets->size(); ++itemIndex){
                const ExpandedAssetMetadata& expandedAsset = (*expandedAssets)[itemIndex];
                if(!__hidden_cook_metadata::ParseDeclaredAssetItem(cookArena, discoveredNwbFile, fileIndex, static_cast<u32>(itemIndex), expandedAsset.assetType, expandedAsset.virtualPath, expandedAsset.value, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, outMetadata, fileIsStandalone))
                    return false;
                parsedAnyMetadata = true;
            }
            if(useAssetCache)
                outMetadata.fileCacheRecords[fileIndex].standalone = fileIsStandalone;
            continue;
        }

        if(doc.declarations().size() > 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: meta '{}' declares multiple asset objects without an asset_bunch object"), PathToString<tchar>(discoveredNwbFile.filePath));
            return false;
        }

        if(!__hidden_cook_metadata::ParseSingleAssetDocument(cookArena, discoveredNwbFile, fileIndex, doc, cpuScheduler, scratchArena, seenPropertyAssetPathHashes, outMetadata, fileIsStandalone))
            return false;
        if(useAssetCache)
            outMetadata.fileCacheRecords[fileIndex].standalone = fileIsStandalone;
        parsedAnyMetadata = true;
    }

    if(!parsedAnyMetadata){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: no asset metadata found in asset roots"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

