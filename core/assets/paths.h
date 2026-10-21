// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "ref.h"

#include <core/common/log.h>

#include <global/allocation_size.h>
#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetsDirectoryName = "assets";
static constexpr AStringView s_EngineVirtualRoot = "engine";
static constexpr AStringView s_ProjectVirtualRoot = "project";
static constexpr AStringView s_NwbExtension = ".nwb";
static constexpr AStringView s_MetadataAssetVariable = "asset";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ConvertedPathByteCounter{
    usize byteCount = 0u;


public:
    ConvertedPathByteCounter& operator*()noexcept{ return *this; }

    ConvertedPathByteCounter& operator++(int)noexcept{ return *this; }

    ConvertedPathByteCounter& operator=(const char){
        byteCount = AddSize(byteCount, 1u);
        return *this;
    }
};

template<typename PathArenaT>
struct RelativeAssetPathLayout{
    typename ::Path<PathArenaT>::const_iterator acceptedEnd;
    usize byteCount = 0u;
    bool accepted = false;
};


template<typename PathArenaT>
[[nodiscard]] inline RelativeAssetPathLayout<PathArenaT> MeasureRelativeAssetPathText(const ::Path<PathArenaT>& relativePath){
    using NativeStringView = typename ::Path<PathArenaT>::native_string_view;
    constexpr NativeStringView s_PathSeparatorChars{ NWB_TEXT("/\\") };

    RelativeAssetPathLayout<PathArenaT> layout{ relativePath.end() };
    ConvertedPathByteCounter counter;
    for(auto componentIt = relativePath.begin(); componentIt != relativePath.end(); ++componentIt){
        const auto component = componentIt.nativeComponent();
        if(component.empty() || GlobalFilesystemPathDetail::IsDot(component))
            continue;
        if(GlobalFilesystemPathDetail::IsDotDot(component) || component.find_first_of(s_PathSeparatorChars) != NativeStringView::npos){
            layout.acceptedEnd = componentIt;
            layout.byteCount = counter.byteCount;
            return layout;
        }

        if(counter.byteCount != 0u)
            counter.byteCount = AddSize(counter.byteCount, 1u);
        BasicStringDetail::WriteConvertedText<char>(counter, component);
    }
    layout.byteCount = counter.byteCount;
    layout.accepted = counter.byteCount != 0u;
    return layout;
}

template<typename PathArenaT>
inline void WriteRelativeAssetPathText(const ::Path<PathArenaT>& relativePath, const RelativeAssetPathLayout<PathArenaT>& layout, const NotNull<char*> output)noexcept{
    char* cursor = output.get();
    bool hasComponent = false;
    for(auto componentIt = relativePath.begin(); componentIt != layout.acceptedEnd; ++componentIt){
        const auto component = componentIt.nativeComponent();
        if(component.empty() || GlobalFilesystemPathDetail::IsDot(component))
            continue;
        if(hasComponent)
            *cursor++ = '/';
        BasicStringDetail::WriteConvertedText<char>(cursor, component);
        hasComponent = true;
    }
    NWB_ASSERT(static_cast<usize>(cursor - output.get()) == layout.byteCount);
    for(char* character = output.get(); character != cursor; ++character)
        *character = Canonicalize(*character);
}

template<typename StringT, typename PathArenaT>
[[nodiscard]] inline bool BuildRelativeAssetPathText(const ::Path<PathArenaT>& relativePath, StringT& outRelativePath){
    outRelativePath.clear();
    const auto layout = MeasureRelativeAssetPathText(relativePath);
    outRelativePath.resize(layout.byteCount);
    WriteRelativeAssetPathText(relativePath, layout, MakeNotNull(outRelativePath.data()));
    return layout.accepted;
}

[[nodiscard]] inline Expected<ACompactString> ExtractAssetVirtualRoot(const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    ACompactString virtualRoot;

    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return MakeUnexpected(Failure{});

    const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
    if(!virtualRoot.assign(AStringView(componentText)) || virtualRoot.empty())
        return MakeUnexpected(Failure{});
    return virtualRoot;
}

template<typename ArenaT, typename RootArenaT, typename SourceArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> BuildDerivedAssetVirtualPathText(ArenaT& arena, const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<SourceArenaT>& sourceOrMetaPath, Alloc::ScratchArena& scratchArena){
    AString<ArenaT> virtualPath{arena};
    usize virtualPathSize = 0u;
    for(const bool writeOutput : { false, true }){
        {
            const ::Path<Alloc::ScratchArena> sourcePath(scratchArena, sourceOrMetaPath);
            const ::Path<Alloc::ScratchArena> rootPath(scratchArena, assetRoot);
            const ::Path<Alloc::ScratchArena> relativePath = sourcePath.lexicallyRelative(rootPath);
            if(relativePath.empty()){
                NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset path from '{}' relative to asset root '{}'"), PathToString<tchar>(sourceOrMetaPath), PathToString<tchar>(assetRoot));
                return MakeUnexpected(Failure{});
            }

            ::Path<Alloc::ScratchArena> logicalPath = relativePath;
            logicalPath.replaceExtension();
            const auto layout = MeasureRelativeAssetPathText(logicalPath);
            if(!layout.accepted){
                NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset '{}' is not under asset root '{}'"), PathToString<tchar>(sourceOrMetaPath), PathToString<tchar>(assetRoot));
                return MakeUnexpected(Failure{});
            }

            if(writeOutput){
                NWB_ASSERT(virtualPathSize == virtualRoot.size() + 1u + layout.byteCount);
                WriteRelativeAssetPathText(logicalPath, layout, MakeNotNull(virtualPath.data() + virtualRoot.size() + 1u));
            }
            else{
                if(virtualRoot.size() > Limit<usize>::s_Max - 1u || layout.byteCount > Limit<usize>::s_Max - virtualRoot.size() - 1u){
                    NWB_LOGGER_ERROR(NWB_TEXT("Assets: derived asset virtual path size overflows for '{}'"), PathToString<tchar>(sourceOrMetaPath));
                    return MakeUnexpected(Failure{});
                }
                virtualPathSize = virtualRoot.size() + 1u + layout.byteCount;
            }
        }

        if(!writeOutput){
            virtualPath.resize(virtualPathSize);
            if(!virtualRoot.empty())
                NWB_MEMCPY(virtualPath.data(), virtualPathSize, virtualRoot.data(), virtualRoot.size());
            virtualPath[virtualRoot.size()] = '/';
        }
    }
    return virtualPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT, typename RootArenaT, typename SourceArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> BuildDerivedAssetVirtualPath(ArenaT& arena, const ::Path<RootArenaT>& assetRoot, const AStringView virtualRoot, const ::Path<SourceArenaT>& sourceOrMetaPath, Alloc::ScratchArena& scratchArena){
    return AssetPathsDetail::BuildDerivedAssetVirtualPathText(arena, assetRoot, virtualRoot, sourceOrMetaPath, scratchArena);
}

[[nodiscard]] inline Expected<Name> BuildDerivedAssetVirtualPath(const Path& assetRoot, const AStringView virtualRoot, const Path& sourceOrMetaPath, Alloc::ScratchArena& scratchArena){
    const auto virtualPathText = AssetPathsDetail::BuildDerivedAssetVirtualPathText(scratchArena, assetRoot, virtualRoot, sourceOrMetaPath, scratchArena);
    if(!virtualPathText)
        return MakeUnexpected(Failure{});

    const auto virtualPath = Name::FromText(AStringView(*virtualPathText));
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset name from '{}'"), PathToString<tchar>(sourceOrMetaPath));
        return MakeUnexpected(Failure{});
    }

    return *virtualPath;
}

[[nodiscard]] inline Expected<Name> BuildDerivedAssetVirtualPath(const Path& assetRoot, const ACompactString& virtualRoot, const Path& sourceOrMetaPath, Alloc::ScratchArena& scratchArena){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), sourceOrMetaPath, scratchArena);
}

[[nodiscard]] inline bool HasReservedAssetVirtualRoot(const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    const auto virtualRoot = AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, scratchArena);
    if(!virtualRoot)
        return false;

    return virtualRoot->view() == s_EngineVirtualRoot || virtualRoot->view() == s_ProjectVirtualRoot;
}

template<typename AssetRootVector>
[[nodiscard]] inline Expected<Path> ResolveVirtualAssetPath(AssetArena& arena, const AssetRootVector& assetRoots, const AStringView virtualPath, Alloc::ScratchArena& scratchArena){
    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return MakeUnexpected(Failure{});

    ACompactString requestedVirtualRoot;
    {
        const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
        if(!requestedVirtualRoot.assign(AStringView(componentText)) || requestedVirtualRoot.empty())
            return MakeUnexpected(Failure{});
    }

    for(const auto& assetRoot : assetRoots){
        if(assetRoot.virtualRoot != requestedVirtualRoot)
            continue;

        Path resolvedPath(arena, assetRoot.path);
        ++componentIt;
        for(; componentIt != virtualPathPath.end(); ++componentIt){
            AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
            CanonicalizeTextInPlace(componentText);
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AString<Alloc::ScratchArena>::npos){
                NWB_LOGGER_ERROR(NWB_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators"), StringConvert(virtualPath));
                return MakeUnexpected(Failure{});
            }

            resolvedPath /= componentText;
        }
        resolvedPath = resolvedPath.lexicallyNormal();
        return resolvedPath;
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline Expected<AssetString> ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, AssetArena& arena, Alloc::ScratchArena& scratchArena){
    const ::Path<Alloc::ScratchArena> metadataPath(scratchArena, nwbFilePath);
    const ::Path<Alloc::ScratchArena> parentDirectory = metadataPath.parentPath();
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata directory is empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    auto nwbStem = PathToString(scratchArena, metadataPath.stem());
    CanonicalizeTextInPlace(nwbStem);
    if(nwbStem.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata filename stem is empty"), PathToString<tchar>(nwbFilePath));
        return MakeUnexpected(Failure{});
    }

    const ::Path<Alloc::ScratchArena>* matchedSourcePath = nullptr;
    usize matchCount = 0;
    const auto entries = DirectoryIterator<Alloc::ScratchArena>::Create(parentDirectory);
    if(!entries){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to scan metadata directory '{}': {}"), PathToString<tchar>(nwbFilePath), PathToString<tchar>(parentDirectory), StringConvert(entries.error().message()));
        return MakeUnexpected(Failure{});
    }
    for(const auto& dirEntry : *entries){
        const auto isRegularFile = dirEntry.isRegularFile();
        if(!isRegularFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to inspect '{}' while resolving paired source: {}"), PathToString<tchar>(nwbFilePath), PathToString<tchar>(dirEntry.path()), StringConvert(isRegularFile.error().message()));
            return MakeUnexpected(Failure{});
        }
        if(!*isRegularFile)
            continue;

        const ::Path<Alloc::ScratchArena>& candidatePath = dirEntry.path();
        auto candidateExtension = PathToString(scratchArena, candidatePath.extension());
        CanonicalizeTextInPlace(candidateExtension);
        if(candidateExtension == s_NwbExtension)
            continue;
        auto candidateStem = PathToString(scratchArena, candidatePath.stem());
        CanonicalizeTextInPlace(candidateStem);
        if(candidateStem != nwbStem)
            continue;

        matchedSourcePath = &candidatePath;
        ++matchCount;
        if(matchCount > 1){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': paired source is ambiguous; multiple source files share stem '{}'"), PathToString<tchar>(nwbFilePath), StringConvert(nwbStem));
            return MakeUnexpected(Failure{});
        }
    }

    if(matchCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to find a paired source file with stem '{}'"), PathToString<tchar>(nwbFilePath), StringConvert(nwbStem));
        return MakeUnexpected(Failure{});
    }

    const ::Path<Alloc::ScratchArena> normalizedSourcePath = matchedSourcePath->lexicallyNormal();
    return PathToString(arena, normalizedSourcePath);
}

[[nodiscard]] inline bool IsListedMetadataAssetField(const AStringView fieldName, const InitializerList<AStringView> allowedFields)noexcept{
    for(const AStringView allowedField : allowedFields){
        if(fieldName == allowedField)
            return true;
    }
    return false;
}

template<typename MetadataValue, typename IsAllowedField>
[[nodiscard]] inline bool ValidateMetadataAssetFields(const Path& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix, IsAllowedField&& isAllowedField){
    for(const auto& field : asset.asMap()){
        const auto& fieldName = field.first;
        const AStringView fieldNameText(fieldName.data(), fieldName.size());
        if(isAllowedField(fieldNameText))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unsupported asset field '{}'"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldNameText));
        return false;
    }
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool ValidateMetadataAssetFields(const Path& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix, const InitializerList<AStringView> allowedFields){
    return ValidateMetadataAssetFields(nwbFilePath, asset, diagnosticPrefix, [allowedFields](const AStringView fieldName)noexcept{
        return IsListedMetadataAssetField(fieldName, allowedFields);
    }
    );
}

struct MetadataStringField{
    AStringView text;
    bool present = false;
};


template<typename MetadataValue>
[[nodiscard]] inline Expected<MetadataStringField> ReadMetadataStringField(const Path& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const bool required){
    MetadataStringField result;

    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return result;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }
    result.present = true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a string"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }

    const auto text = fieldValue->asString();
    result.text = AStringView(text.data(), text.size());
    if(required && result.text.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }
    return result;
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> TryDecodeMetadataFiniteF32(const MetadataValue& value)noexcept(noexcept(!value.isNumeric() ? true : false) && noexcept(DeclVal<void(*)(f64)noexcept>()(value.toDouble()))){
    if(!value.isNumeric())
        return MakeUnexpected(Failure{});

    const f64 numericValue = value.toDouble();
    if(!IsFinite(numericValue) || numericValue < static_cast<f64>(Limit<f32>::s_Min) || numericValue > static_cast<f64>(Limit<f32>::s_Max))
        return MakeUnexpected(Failure{});

    return static_cast<f32>(numericValue);
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Value(const Path& nwbFilePath, const MetadataValue& value, const AStringView diagnosticPrefix, const AStringView fieldName){
    const auto decoded = TryDecodeMetadataFiniteF32(value);
    if(decoded)
        return decoded;

    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be numeric"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is outside the supported float range"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
    return MakeUnexpected(Failure{});
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Field(const Path& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const bool required){
    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return 0.0f;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }
    return ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldName);
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<Name> ReadMetadataNameField(const Path& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const bool required){
    const auto text = ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!text)
        return MakeUnexpected(text.error());
    // An absent optional field must remain s_NameNone. Name("") is a valid, non-null hash.
    if(!text->present)
        return s_NameNone;

    const auto name = Name::FromText(text->text);
    if(!name){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' contains an embedded null"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
        return MakeUnexpected(Failure{});
    }
    return *name;
}

template<typename AssetT, typename MetadataValue>
[[nodiscard]] inline Expected<AssetRef<AssetT>> ReadMetadataAssetRefField(const Path& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const bool required){
    const auto assetName = ReadMetadataNameField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!assetName)
        return MakeUnexpected(assetName.error());

    AssetRef<AssetT> ref;
    ref.virtualPath = *assetName;
    if(required && !ref.valid())
        return MakeUnexpected(Failure{});
    return ref;
}

template<typename SourceStringT, typename MetadataDocument, typename ParseDocument>
[[nodiscard]] inline bool ParseMetadataDocumentText(const Path& filePath, const AStringView diagnosticPrefix, SourceStringT& ioText, MetadataDocument& outDoc, ParseDocument&& parseDoc){
    if(!ReadTextFile(filePath, ioText)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read source text"), StringConvert(diagnosticPrefix), PathToString<tchar>(filePath));
        return false;
    }
    StripUtf8Bom(ioText);

    if(!parseDoc(AStringView(ioText))){
        for(const auto& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}' parse error at {}:{}: {}"), StringConvert(diagnosticPrefix), PathToString<tchar>(filePath), err.line, err.column, StringConvert(AStringView(err.message.data(), err.message.size())));
        }
        return false;
    }
    return true;
}

template<typename ScratchArenaT, typename SourceStringT>
[[nodiscard]] inline bool CheckPairedSourceExtension(const Path& nwbFilePath, const SourceStringT& sourcePath, const AStringView expectedExtension, const AStringView diagnosticPrefix, ScratchArenaT& scratchArena){
    const ::Path<ScratchArenaT> sourcePathValue(scratchArena, sourcePath);
    AString<ScratchArenaT> extension(PathToString(scratchArena, sourcePathValue.extension()));
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) == expectedExtension)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': paired source '{}' must use '{}' extension"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(sourcePath), StringConvert(expectedExtension));
    return false;
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataListField(const Path& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName){
    const auto* fieldValue = object.findField(fieldName);
    if(fieldValue && fieldValue->isList())
        return MakeNotNull(fieldValue);

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName));
    return MakeUnexpected(Failure{});
}

template<typename CookEntryT>
[[nodiscard]] inline bool AssignCookEntryVirtualPath(CookEntryT& outEntry, const Name& virtualPath, const Path& nwbFilePath, const AStringView diagnosticPrefix){
    outEntry.virtualPath = virtualPath;
    if(outEntry.virtualPath)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': virtual path must not be empty"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

template<typename MetadataValue>
[[nodiscard]] inline bool CheckMetadataAssetMap(const Path& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix){
    if(asset.isMap())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

template<typename MetadataDocument>
[[nodiscard]] inline const typename MetadataDocument::Declaration* FindMetadataAssetDeclaration(const MetadataDocument& doc)noexcept{
    return doc.findDeclaration(s_MetadataAssetVariable);
}

template<typename MetadataDocument, typename MetadataValue>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataAssetValue(const Path& nwbFilePath, const MetadataDocument& doc, const AStringView diagnosticPrefix){
    const auto* asset = doc.findVariable(s_MetadataAssetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': missing asset declaration '{}'"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(s_MetadataAssetVariable));
        return MakeUnexpected(Failure{});
    }
    return MakeNotNull(asset);
}

template<typename MetadataDocument, typename MetadataValue>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataAssetMapValue(const Path& nwbFilePath, const MetadataDocument& doc, const AStringView diagnosticPrefix){
    const auto asset = FindMetadataAssetValue<MetadataDocument, MetadataValue>(nwbFilePath, doc, diagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    if(!CheckMetadataAssetMap(nwbFilePath, **asset, diagnosticPrefix))
        return MakeUnexpected(Failure{});
    return *asset;
}

template<typename NamedEnumT, typename MetadataValue>
[[nodiscard]] inline Expected<NamedEnumT> ParseNamedMetadataEnumField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const ::NamedEnumCase<NamedEnumT>* cases,
    const usize caseCount,
    const AStringView errorDetailText
){
    const auto text = ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, true);
    if(!text)
        return MakeUnexpected(text.error());
    const auto value = ParseNamedEnumText<NamedEnumT>(text->text, cases, caseCount);
    if(value)
        return value;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' {}"), StringConvert(diagnosticPrefix), PathToString<tchar>(nwbFilePath), StringConvert(fieldName), StringConvert(errorDetailText));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

