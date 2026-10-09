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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ConvertedPathByteCounter{
    usize byteCount = 0u;

    ConvertedPathByteCounter& operator*()noexcept{ return *this; }
    ConvertedPathByteCounter& operator++(int)noexcept{ return *this; }
    ConvertedPathByteCounter& operator=(const char){
        byteCount = AddSize(byteCount, 1u);
        return *this;
    }
};

struct RelativeAssetPathLayout{
    Path::const_iterator acceptedEnd;
    usize byteCount = 0u;
    bool accepted = false;
};


[[nodiscard]] inline RelativeAssetPathLayout MeasureRelativeAssetPathText(const Path& relativePath){
    RelativeAssetPathLayout layout{ relativePath.end() };
    ConvertedPathByteCounter counter;
    for(auto componentIt = relativePath.begin(); componentIt != relativePath.end(); ++componentIt){
        const auto component = componentIt.nativeComponent();
        if(component.empty() || GlobalFilesystemPathDetail::IsDot(component))
            continue;
        if(
            GlobalFilesystemPathDetail::IsDotDot(component)
            || component.find_first_of(Path::native_string_view{ NWB_TEXT("/\\"), 2u }) != Path::native_string_view::npos
        ){
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

inline void WriteRelativeAssetPathText(
    const Path& relativePath,
    const RelativeAssetPathLayout& layout,
    const NotNull<char*> output
){
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

template<typename StringT>
[[nodiscard]] inline bool BuildRelativeAssetPathText(const Path& relativePath, StringT& outRelativePath){
    outRelativePath.clear();
    const RelativeAssetPathLayout layout = MeasureRelativeAssetPathText(relativePath);
    outRelativePath.resize(layout.byteCount);
    WriteRelativeAssetPathText(relativePath, layout, MakeNotNull(outRelativePath.data()));
    return layout.accepted;
}

[[nodiscard]] inline Expected<ACompactString> ExtractAssetVirtualRoot(
    const AStringView virtualPath,
    Alloc::ScratchArena& scratchArena
){
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

template<typename ArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> BuildDerivedAssetVirtualPathText(
    ArenaT& arena,
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath
){
    AString<ArenaT> virtualPath{arena};

    const Path relativePath = sourceOrMetaPath.lexicallyRelative(assetRoot);
    if(relativePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset path from '{}' relative to asset root '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
            , PathToString<tchar>(assetRoot)
        );
        return MakeUnexpected(Failure{});
    }

    Path logicalPath = relativePath;
    logicalPath.replaceExtension();

    const RelativeAssetPathLayout layout = MeasureRelativeAssetPathText(logicalPath);
    if(!layout.accepted){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset '{}' is not under asset root '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
            , PathToString<tchar>(assetRoot)
        );
        return MakeUnexpected(Failure{});
    }

    if(
        virtualRoot.size() > Limit<usize>::s_Max - 1u
        || layout.byteCount > Limit<usize>::s_Max - virtualRoot.size() - 1u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: derived asset virtual path size overflows for '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
        );
        return MakeUnexpected(Failure{});
    }

    const usize virtualPathSize = virtualRoot.size() + 1u + layout.byteCount;
    virtualPath.resize(virtualPathSize);
    if(!virtualRoot.empty())
        NWB_MEMCPY(virtualPath.data(), virtualPathSize, virtualRoot.data(), virtualRoot.size());
    virtualPath[virtualRoot.size()] = '/';
    WriteRelativeAssetPathText(logicalPath, layout, MakeNotNull(virtualPath.data() + virtualRoot.size() + 1u));
    return virtualPath;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ArenaT>
[[nodiscard]] inline Expected<AString<ArenaT>> BuildDerivedAssetVirtualPath(
    ArenaT& arena,
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath
){
    return AssetPathsDetail::BuildDerivedAssetVirtualPathText(arena, assetRoot, virtualRoot, sourceOrMetaPath);
}

[[nodiscard]] inline Expected<Name> BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath,
    Alloc::ScratchArena& scratchArena
){
    const auto virtualPathText = AssetPathsDetail::BuildDerivedAssetVirtualPathText(scratchArena, assetRoot, virtualRoot, sourceOrMetaPath);
    if(!virtualPathText)
        return MakeUnexpected(Failure{});

    const Name virtualPath{ AStringView(*virtualPathText) };
    if(!virtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset name from '{}'"), PathToString<tchar>(sourceOrMetaPath));
        return MakeUnexpected(Failure{});
    }

    return virtualPath;
}

[[nodiscard]] inline Expected<Name> BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const ACompactString& virtualRoot,
    const Path& sourceOrMetaPath,
    Alloc::ScratchArena& scratchArena
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), sourceOrMetaPath, scratchArena);
}

[[nodiscard]] inline bool HasReservedAssetVirtualRoot(
    const AStringView virtualPath,
    Alloc::ScratchArena& scratchArena
){
    const auto virtualRoot = AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, scratchArena);
    if(!virtualRoot)
        return false;

    return virtualRoot->view() == s_EngineVirtualRoot || virtualRoot->view() == s_ProjectVirtualRoot;
}

template<typename AssetRootVector>
[[nodiscard]] inline Expected<Path> ResolveVirtualAssetPath(
    AssetArena& arena,
    const AssetRootVector& assetRoots,
    const AStringView virtualPath,
    Alloc::ScratchArena& scratchArena
){
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
                NWB_LOGGER_ERROR(NWB_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators")
                    , StringConvert(virtualPath)
                );
                return MakeUnexpected(Failure{});
            }

            resolvedPath /= componentText;
        }
        resolvedPath = resolvedPath.lexicallyNormal();
        return resolvedPath;
    }

    return MakeUnexpected(Failure{});
}

[[nodiscard]] inline Expected<AssetString> ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, AssetArena& arena){
    const Path parentDirectory = nwbFilePath.parentPath();
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata directory is empty")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    auto nwbStem = PathToString(arena, nwbFilePath.stem());
    CanonicalizeTextInPlace(nwbStem);
    if(nwbStem.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata filename stem is empty")
            , PathToString<tchar>(nwbFilePath)
        );
        return MakeUnexpected(Failure{});
    }

    Path matchedSourcePath(arena);
    usize matchCount = 0;
    const auto entries = DirectoryIterator<AssetArena>::Create(parentDirectory);
    if(!entries){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to scan metadata directory '{}': {}")
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(parentDirectory)
            , StringConvert(entries.error().message())
        );
        return MakeUnexpected(Failure{});
    }
    for(const auto& dirEntry : *entries){
        const auto isRegularFile = dirEntry.isRegularFile();
        if(!isRegularFile){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to inspect '{}' while resolving paired source: {}")
                , PathToString<tchar>(nwbFilePath)
                , PathToString<tchar>(dirEntry.path())
                , StringConvert(isRegularFile.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        if(!*isRegularFile)
            continue;

        const Path& candidatePath = dirEntry.path();
        auto candidateExtension = PathToString(arena, candidatePath.extension());
        CanonicalizeTextInPlace(candidateExtension);
        if(candidateExtension == s_NwbExtension)
            continue;
        auto candidateStem = PathToString(arena, candidatePath.stem());
        CanonicalizeTextInPlace(candidateStem);
        if(candidateStem != nwbStem)
            continue;

        matchedSourcePath = candidatePath.lexicallyNormal();
        ++matchCount;
        if(matchCount > 1){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': paired source is ambiguous; multiple source files share stem '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(nwbStem)
            );
            return MakeUnexpected(Failure{});
        }
    }

    if(matchCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to find a paired source file with stem '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(nwbStem)
        );
        return MakeUnexpected(Failure{});
    }

    return PathToString(arena, matchedSourcePath);
}

[[nodiscard]] inline bool IsListedMetadataAssetField(
    const AStringView fieldName,
    const InitializerList<AStringView> allowedFields
)noexcept{
    for(const AStringView allowedField : allowedFields){
        if(fieldName == allowedField)
            return true;
    }
    return false;
}

template<typename MetadataValue, typename IsAllowedField>
[[nodiscard]] inline bool ValidateMetadataAssetFields(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix,
    IsAllowedField&& isAllowedField
){
    for(const auto& field : asset.asMap()){
        const auto& fieldName = field.first;
        const AStringView fieldNameText(fieldName.data(), fieldName.size());
        if(isAllowedField(fieldNameText))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': unsupported asset field '{}'")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldNameText)
        );
        return false;
    }
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool ValidateMetadataAssetFields(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix,
    const InitializerList<AStringView> allowedFields
){
    return ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        diagnosticPrefix,
        [allowedFields](const AStringView fieldName){
            return IsListedMetadataAssetField(fieldName, allowedFields);
        }
    );
}

struct MetadataStringField{
    AStringView text;
    bool present = false;
};


template<typename MetadataValue>
[[nodiscard]] inline Expected<MetadataStringField> ReadMetadataStringField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required
){
    MetadataStringField result;

    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return result;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    result.present = true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a string")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    const auto text = fieldValue->asString();
    result.text = AStringView(text.data(), text.size());
    if(required && result.text.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return result;
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> TryDecodeMetadataFiniteF32(const MetadataValue& value){
    if(!value.isNumeric())
        return MakeUnexpected(Failure{});

    const f64 numericValue = value.toDouble();
    if(
        !IsFinite(numericValue)
        || numericValue < static_cast<f64>(Limit<f32>::s_Min)
        || numericValue > static_cast<f64>(Limit<f32>::s_Max)
    )
        return MakeUnexpected(Failure{});

    return static_cast<f32>(numericValue);
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Value(
    const Path& nwbFilePath,
    const MetadataValue& value,
    const AStringView diagnosticPrefix,
    const AStringView fieldName
){
    const auto decoded = TryDecodeMetadataFiniteF32(value);
    if(decoded)
        return decoded;

    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be numeric")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is outside the supported float range")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return MakeUnexpected(Failure{});
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Field(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required
){
    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return 0.0f;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldName);
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<Name> ReadMetadataNameField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required
){
    const auto text = ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!text)
        return MakeUnexpected(text.error());
    // An absent optional field must remain s_NameNone. Name("") is a valid, non-null hash.
    if(!text->present)
        return s_NameNone;

    const Name name(text->text);
    if(required && !name){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return MakeUnexpected(Failure{});
    }
    return name;
}

template<typename AssetT, typename MetadataValue>
[[nodiscard]] inline Expected<AssetRef<AssetT>> ReadMetadataAssetRefField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required
){
    const auto assetName = ReadMetadataNameField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!assetName)
        return MakeUnexpected(assetName.error());

    AssetRef<AssetT> ref;
    ref.virtualPath = *assetName;
    if(required && !ref.valid())
        return MakeUnexpected(Failure{});
    return ref;
}

[[nodiscard]] inline Expected<Name> BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    Alloc::ScratchArena& scratchArena
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, scratchArena);
}

[[nodiscard]] inline Expected<Name> BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const ACompactString& virtualRoot,
    const Path& nwbFilePath,
    Alloc::ScratchArena& scratchArena
){
    return BuildMetadataDerivedAssetVirtualPath(
        assetRoot,
        virtualRoot.view(),
        nwbFilePath,
        scratchArena
    );
}

template<typename SourceStringT, typename MetadataDocument, typename ParseDocument>
[[nodiscard]] inline bool ParseMetadataDocumentText(
    const Path& filePath,
    const AStringView diagnosticPrefix,
    SourceStringT& ioText,
    MetadataDocument& outDoc,
    ParseDocument&& parseDoc
){
    if(!ReadTextFile(filePath, ioText)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read source text")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(filePath)
        );
        return false;
    }
    StripUtf8Bom(ioText);

    if(!parseDoc(AStringView(ioText))){
        for(const auto& err : outDoc.errors()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}' parse error at {}:{}: {}")
                , StringConvert(diagnosticPrefix)
                , PathToString<tchar>(filePath)
                , err.line
                , err.column
                , StringConvert(AStringView(err.message.data(), err.message.size()))
            );
        }
        return false;
    }
    return true;
}

template<typename ScratchArenaT, typename SourceStringT>
[[nodiscard]] inline bool CheckPairedSourceExtension(
    const Path& nwbFilePath,
    const SourceStringT& sourcePath,
    const AStringView expectedExtension,
    const AStringView diagnosticPrefix,
    ScratchArenaT& scratchArena
){
    const Path sourcePathValue(nwbFilePath.arena(), sourcePath);
    AString<ScratchArenaT> extension(PathToString(scratchArena, sourcePathValue.extension()));
    CanonicalizeTextInPlace(extension);
    if(AStringView(extension) == expectedExtension)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': paired source '{}' must use '{}' extension")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(sourcePath)
        , StringConvert(expectedExtension)
    );
    return false;
}

template<typename MetadataValue>
[[nodiscard]] inline const MetadataValue* FindMetadataListField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName
){
    const auto* fieldValue = object.findField(fieldName);
    if(fieldValue && fieldValue->isList())
        return fieldValue;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return nullptr;
}

template<typename CookEntryT>
[[nodiscard]] inline bool AssignCookEntryVirtualPath(
    CookEntryT& outEntry,
    const Name& virtualPath,
    const Path& nwbFilePath,
    const AStringView diagnosticPrefix
){
    outEntry.virtualPath = virtualPath;
    if(outEntry.virtualPath)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': virtual path must not be empty")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
    );
    return false;
}

template<typename MetadataValue>
[[nodiscard]] inline bool CheckMetadataAssetMap(
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView diagnosticPrefix
){
    if(asset.isMap())
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
    );
    return false;
}

template<typename MetadataDocument, typename MetadataValue>
[[nodiscard]] inline const MetadataValue* FindMetadataAssetMapValue(
    const Path& nwbFilePath,
    const MetadataDocument& doc,
    const AStringView diagnosticPrefix
){
    const auto assetVariable = doc.assetVariable();
    const auto* asset = doc.findVariable(assetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset variable '{}' has no assignments")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(AStringView(assetVariable.data(), assetVariable.size()))
        );
        return nullptr;
    }
    if(!CheckMetadataAssetMap(nwbFilePath, *asset, diagnosticPrefix))
        return nullptr;
    return asset;
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

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' {}")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
        , StringConvert(errorDetailText)
    );
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

