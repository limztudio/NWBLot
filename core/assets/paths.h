// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include "ref.h"

#include <core/common/log.h>

#include <global/allocation_size.h>


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
            || component.find(static_cast<Path::value_type>('/')) != Path::native_string_view::npos
            || component.find(static_cast<Path::value_type>('\\')) != Path::native_string_view::npos
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
    const NotNull<char*> output){
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

[[nodiscard]] inline bool ExtractAssetVirtualRoot(
    const AStringView virtualPath,
    ACompactString& outVirtualRoot,
    Alloc::ScratchArena& scratchArena
){
    outVirtualRoot.clear();

    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
    return outVirtualRoot.assign(AStringView(componentText)) && !outVirtualRoot.empty();
}

template<typename StringT>
[[nodiscard]] inline bool BuildDerivedAssetVirtualPathText(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath,
    StringT& outVirtualPath
){
    outVirtualPath.clear();

    const Path relativePath = sourceOrMetaPath.lexically_relative(assetRoot);
    if(relativePath.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset path from '{}' relative to asset root '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
            , PathToString<tchar>(assetRoot)
        );
        return false;
    }

    Path logicalPath = relativePath;
    logicalPath.replace_extension();

    const RelativeAssetPathLayout layout = MeasureRelativeAssetPathText(logicalPath);
    if(!layout.accepted){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset '{}' is not under asset root '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
            , PathToString<tchar>(assetRoot)
        );
        return false;
    }

    if(
        virtualRoot.size() > Limit<usize>::s_Max - 1u
        || layout.byteCount > Limit<usize>::s_Max - virtualRoot.size() - 1u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: derived asset virtual path size overflows for '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
        );
        return false;
    }

    const usize virtualPathSize = virtualRoot.size() + 1u + layout.byteCount;
    outVirtualPath.resize(virtualPathSize);
    if(!virtualRoot.empty())
        NWB_MEMCPY(outVirtualPath.data(), virtualPathSize, virtualRoot.data(), virtualRoot.size());
    outVirtualPath[virtualRoot.size()] = '/';
    WriteRelativeAssetPathText(logicalPath, layout, MakeNotNull(outVirtualPath.data() + virtualRoot.size() + 1u));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(const Path& assetRoot, const AStringView virtualRoot, const Path& sourceOrMetaPath, StringT& outVirtualPath){
    return AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, outVirtualPath);
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath,
    Name& outVirtualPath,
    Alloc::ScratchArena& scratchArena
){
    outVirtualPath = NAME_NONE;

    AString<Alloc::ScratchArena> virtualPathText{scratchArena};
    if(!AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, virtualPathText))
        return false;

    outVirtualPath = Name(AStringView(virtualPathText));
    if(!outVirtualPath){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: failed to derive asset name from '{}'"), PathToString<tchar>(sourceOrMetaPath));
        return false;
    }

    return true;
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const ACompactString& virtualRoot,
    const Path& sourceOrMetaPath,
    Name& outVirtualPath,
    Alloc::ScratchArena& scratchArena
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), sourceOrMetaPath, outVirtualPath, scratchArena);
}

[[nodiscard]] inline bool HasReservedAssetVirtualRoot(
    const AStringView virtualPath,
    Alloc::ScratchArena& scratchArena
){
    ACompactString virtualRoot;
    if(!AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, virtualRoot, scratchArena))
        return false;

    return virtualRoot.view() == s_EngineVirtualRoot || virtualRoot.view() == s_ProjectVirtualRoot;
}

template<typename AssetRootVector>
[[nodiscard]] inline bool ResolveVirtualAssetPath(
    const AssetRootVector& assetRoots,
    const AStringView virtualPath,
    Path& outResolvedPath,
    Alloc::ScratchArena& scratchArena
){
    outResolvedPath.clear();

    const ::Path<Alloc::ScratchArena> virtualPathPath(scratchArena, virtualPath);
    auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    ACompactString requestedVirtualRoot;
    {
        const AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
        if(!requestedVirtualRoot.assign(AStringView(componentText)) || requestedVirtualRoot.empty())
            return false;
    }

    for(const auto& assetRoot : assetRoots){
        if(assetRoot.virtualRoot != requestedVirtualRoot)
            continue;

        outResolvedPath = assetRoot.path;
        ++componentIt;
        for(; componentIt != virtualPathPath.end(); ++componentIt){
            AString<Alloc::ScratchArena> componentText = PathToString(scratchArena, *componentIt);
            CanonicalizeTextInPlace(componentText);
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AString<Alloc::ScratchArena>::npos){
                NWB_LOGGER_ERROR(NWB_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators")
                    , StringConvert(virtualPath)
                );
                outResolvedPath.clear();
                return false;
            }

            outResolvedPath /= componentText;
        }
        outResolvedPath = outResolvedPath.lexically_normal();
        return true;
    }

    return false;
}

template<typename StringT>
[[nodiscard]] inline bool ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, StringT& outSourcePath){
    outSourcePath.clear();
    auto& arena = outSourcePath.get_allocator().arena();

    const Path parentDirectory = nwbFilePath.parent_path();
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata directory is empty")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    auto nwbStem = PathToString(arena, nwbFilePath.stem());
    CanonicalizeTextInPlace(nwbStem);
    if(nwbStem.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata filename stem is empty")
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    ErrorCode errorCode;
    Path matchedSourcePath(arena);
    usize matchCount = 0;
    const auto logDirectoryScanError = [&](){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to scan metadata directory '{}': {}")
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(parentDirectory)
            , StringConvert(errorCode.message())
        );
    };
    for(const auto& dirEntry : DirectoryIterator(parentDirectory, errorCode)){
        if(errorCode){
            logDirectoryScanError();
            return false;
        }

        errorCode.clear();
        const bool isRegularFile = dirEntry.is_regular_file(errorCode);
        if(errorCode){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to inspect '{}' while resolving paired source: {}")
                , PathToString<tchar>(nwbFilePath)
                , PathToString<tchar>(dirEntry.path())
                , StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularFile)
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

        matchedSourcePath = candidatePath.lexically_normal();
        ++matchCount;
        if(matchCount > 1){
            NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': paired source is ambiguous; multiple source files share stem '{}'")
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(nwbStem)
            );
            return false;
        }
    }

    if(errorCode){
        logDirectoryScanError();
        return false;
    }

    if(matchCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Meta '{}': failed to find a paired source file with stem '{}'")
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(nwbStem)
        );
        return false;
    }

    const auto matchedSourcePathText = PathToString(arena, matchedSourcePath);
    outSourcePath.assign(matchedSourcePathText.data(), matchedSourcePathText.size());
    return true;
}

[[nodiscard]] inline bool IsListedMetadataAssetField(
    const AStringView fieldName,
    const InitializerList<AStringView> allowedFields
){
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

// Shared cooker-side metadata readers: consistent missing/type/nonempty validation for string, name,
// and typed-reference fields before a cooker consumes them.
template<typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataStringField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required,
    AStringView& outText,
    bool* const outPresent = nullptr
){
    outText = {};
    if(outPresent)
        *outPresent = false;

    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(outPresent)
        *outPresent = true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a string")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const auto text = fieldValue->asString();
    outText = AStringView(text.data(), text.size());
    if(required && outText.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool TryDecodeMetadataFiniteF32(const MetadataValue& value, f32& outValue){
    outValue = 0.0f;
    if(!value.isNumeric())
        return false;

    const f64 numericValue = value.toDouble();
    if(
        !IsFinite(numericValue)
        || numericValue < static_cast<f64>(Limit<f32>::s_Min)
        || numericValue > static_cast<f64>(Limit<f32>::s_Max)
    )
        return false;

    outValue = static_cast<f32>(numericValue);
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataFiniteF32Value(
    const Path& nwbFilePath,
    const MetadataValue& value,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    f32& outValue
){
    if(TryDecodeMetadataFiniteF32(value, outValue))
        return true;

    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be numeric")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is outside the supported float range")
        , StringConvert(diagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
    );
    return false;
}

template<typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataFiniteF32Field(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required,
    f32& outValue
){
    outValue = 0.0f;
    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return true;

        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldName, outValue);
}

template<typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataCompactStringField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required,
    ACompactString& outValue
){
    outValue = {};
    AStringView text;
    bool present = false;
    if(!ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, required, text, &present))
        return false;
    if(!present)
        return true;
    if(!outValue.assign(text)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' exceeds ACompactString capacity")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataNameField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required,
    Name& outName
){
    outName = NAME_NONE;

    AStringView text;
    bool present = false;
    if(!ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, required, text, &present))
        return false;
    // An absent optional field must remain NAME_NONE. Name("") is a valid, non-null hash.
    if(!present)
        return true;

    outName = Name(text);
    if(required && !outName){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(diagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename AssetT, typename MetadataValue>
[[nodiscard]] inline bool ReadMetadataAssetRefField(
    const Path& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const bool required,
    AssetRef<AssetT>& outRef
){
    Name assetName = NAME_NONE;
    if(!ReadMetadataNameField(nwbFilePath, object, diagnosticPrefix, fieldName, required, assetName))
        return false;

    outRef = {};
    outRef.virtualPath = assetName;
    return !required || outRef.valid();
}

[[nodiscard]] inline bool BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    Name& outVirtualPath,
    Alloc::ScratchArena& scratchArena
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outVirtualPath, scratchArena);
}

[[nodiscard]] inline bool BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const ACompactString& virtualRoot,
    const Path& nwbFilePath,
    Name& outVirtualPath,
    Alloc::ScratchArena& scratchArena
){
    return BuildMetadataDerivedAssetVirtualPath(
        assetRoot,
        virtualRoot.view(),
        nwbFilePath,
        outVirtualPath,
        scratchArena
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

