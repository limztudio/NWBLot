// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetsDirectoryName = "assets";
static constexpr AStringView s_ImplDirectoryName = "impl";
static constexpr AStringView s_EngineVirtualRoot = "engine";
static constexpr AStringView s_ProjectVirtualRoot = "project";
static constexpr AStringView s_NwbExtension = ".nwb";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetPathsDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ResolveAssetRootVirtualRootText(const Path& assetRoot, AStringView& outVirtualRootText){
    outVirtualRootText = {};

    Alloc::ScratchArena<> scratchArena;
    AString<Alloc::ScratchArena<>> assetRootName = PathToString(scratchArena, assetRoot.filename());
    CanonicalizeTextInPlace(assetRootName);
    if(assetRootName != s_AssetsDirectoryName){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset root must point to an 'assets' directory: '{}'")
            , PathToString<tchar>(assetRoot)
        );
        return false;
    }

    AString<Alloc::ScratchArena<>> parentDirectoryName = PathToString(scratchArena, assetRoot.parent_path().filename());
    CanonicalizeTextInPlace(parentDirectoryName);
    outVirtualRootText = parentDirectoryName == s_ImplDirectoryName
        ? s_EngineVirtualRoot
        : s_ProjectVirtualRoot
    ;
    return true;
}

template<typename StringT>
[[nodiscard]] inline bool BuildRelativeAssetPathText(const Path& relativePath, StringT& outRelativePath){
    outRelativePath.clear();
    auto& arena = outRelativePath.get_allocator().arena();

    bool hasComponent = false;
    for(const Path& component : relativePath){
        auto componentText = PathToString(arena, component);
        CanonicalizeTextInPlace(componentText);
        if(componentText.empty() || componentText == ".")
            continue;
        if(componentText == ".." || AStringView(componentText).find('/') != AStringView::npos)
            return false;

        if(hasComponent)
            outRelativePath += '/';
        outRelativePath += componentText;
        hasComponent = true;
    }

    return hasComponent;
}

[[nodiscard]] inline bool ExtractAssetVirtualRoot(const AStringView virtualPath, ACompactString& outVirtualRoot){
    outVirtualRoot.clear();

    const Path virtualPathPath(virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    Alloc::ScratchArena<> scratchArena;
    const AString<Alloc::ScratchArena<>> componentText = PathToString(scratchArena, *componentIt);
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

    StringT relativePathText{outVirtualPath.get_allocator()};
    if(!BuildRelativeAssetPathText(logicalPath, relativePathText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset '{}' is not under asset root '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
            , PathToString<tchar>(assetRoot)
        );
        return false;
    }

    if(
        virtualRoot.size() > Limit<usize>::s_Max - 1u
        || relativePathText.size() > Limit<usize>::s_Max - virtualRoot.size() - 1u
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: derived asset virtual path size overflows for '{}'")
            , PathToString<tchar>(sourceOrMetaPath)
        );
        return false;
    }

    const usize virtualPathSize = virtualRoot.size() + 1u + relativePathText.size();
    outVirtualPath.reserve(virtualPathSize);
    outVirtualPath.append(virtualRoot.data(), virtualRoot.size());
    outVirtualPath += '/';
    outVirtualPath += relativePathText;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BuildAssetRootVirtualRoot(const Path& assetRoot, ACompactString& outVirtualRoot){
    outVirtualRoot.clear();

    AStringView virtualRootText;
    if(!AssetPathsDetail::ResolveAssetRootVirtualRootText(assetRoot, virtualRootText))
        return false;
    if(!outVirtualRoot.assign(virtualRootText)){
        NWB_LOGGER_ERROR(NWB_TEXT("Assets: asset virtual root '{}' exceeds ACompactString capacity")
            , StringConvert(virtualRootText)
        );
        return false;
    }

    return true;
}

template<typename StringT>
[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(const Path& assetRoot, const AStringView virtualRoot, const Path& sourceOrMetaPath, StringT& outVirtualPath){
    return AssetPathsDetail::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, outVirtualPath);
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath,
    Name& outVirtualPath
){
    outVirtualPath = NAME_NONE;

    Alloc::ScratchArena<> scratchArena;
    AString<Alloc::ScratchArena<>> virtualPathText{scratchArena};
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
    Name& outVirtualPath
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), sourceOrMetaPath, outVirtualPath);
}

[[nodiscard]] inline bool HasReservedAssetVirtualRoot(const AStringView virtualPath){
    ACompactString virtualRoot;
    if(!AssetPathsDetail::ExtractAssetVirtualRoot(virtualPath, virtualRoot))
        return false;

    return
        virtualRoot.view() == s_EngineVirtualRoot
        || virtualRoot.view() == s_ProjectVirtualRoot
    ;
}

template<typename AssetRootVector>
[[nodiscard]] inline bool ResolveVirtualAssetPath(const AssetRootVector& assetRoots, const AStringView virtualPath, Path& outResolvedPath){
    outResolvedPath.clear();

    const Path virtualPathPath(virtualPath);
    auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    ACompactString requestedVirtualRoot;
    {
        Alloc::ScratchArena<> scratchArena;
        const AString<Alloc::ScratchArena<>> componentText = PathToString(scratchArena, *componentIt);
        if(!requestedVirtualRoot.assign(AStringView(componentText)) || requestedVirtualRoot.empty())
            return false;
    }

    for(const Path& assetRoot : assetRoots){
        ACompactString assetVirtualRoot;
        if(!BuildAssetRootVirtualRoot(assetRoot, assetVirtualRoot))
            return false;
        if(assetVirtualRoot != requestedVirtualRoot)
            continue;

        outResolvedPath = assetRoot;
        ++componentIt;
        for(; componentIt != virtualPathPath.end(); ++componentIt){
            Alloc::ScratchArena<> scratchArena;
            AString<Alloc::ScratchArena<>> componentText = PathToString(scratchArena, *componentIt);
            CanonicalizeTextInPlace(componentText);
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AString<Alloc::ScratchArena<>>::npos){
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
    Path matchedSourcePath;
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

template<typename MetadataValue>
[[nodiscard]] inline bool RejectVirtualPathOverrideField(const Path& nwbFilePath, const MetadataValue& asset, const AStringView assetLabel){
    if(!asset.findField("name"))
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} meta '{}': field 'name' is no longer supported; virtual paths are derived from the asset file hierarchy")
        , StringConvert(assetLabel)
        , PathToString<tchar>(nwbFilePath)
    );
    return false;
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

template<typename MetadataValue>
[[nodiscard]] inline bool BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView assetLabel,
    Name& outVirtualPath
){
    if(!RejectVirtualPathOverrideField(nwbFilePath, asset, assetLabel))
        return false;
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outVirtualPath);
}

template<typename MetadataValue>
[[nodiscard]] inline bool BuildMetadataDerivedAssetVirtualPath(
    const Path& assetRoot,
    const ACompactString& virtualRoot,
    const Path& nwbFilePath,
    const MetadataValue& asset,
    const AStringView assetLabel,
    Name& outVirtualPath
){
    return BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), nwbFilePath, asset, assetLabel, outVirtualPath);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

