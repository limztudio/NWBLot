// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_AssetsDirectoryName = "assets";
static constexpr AStringView s_ImplDirectoryName = "impl";
static constexpr AStringView s_EngineVirtualRoot = "engine";
static constexpr AStringView s_ProjectVirtualRoot = "project";
static constexpr AStringView s_NwbExtension = ".nwb";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_paths{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool ResolveAssetRootVirtualRootText(const Path& assetRoot, AStringView& outVirtualRootText){
    outVirtualRootText = {};

    const AString assetRootName = CanonicalizeText(PathToString(assetRoot.filename()));
    if(assetRootName != s_AssetsDirectoryName){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Assets: asset root must point to an 'assets' directory: '{}'"),
            PathToString<tchar>(assetRoot)
        );
        return false;
    }

    const AString parentDirectoryName = CanonicalizeText(PathToString(assetRoot.parent_path().filename()));
    outVirtualRootText = parentDirectoryName == s_ImplDirectoryName
        ? s_EngineVirtualRoot
        : s_ProjectVirtualRoot
    ;
    return true;
}

[[nodiscard]] inline bool BuildRelativeAssetPathText(const Path& relativePath, AString& outRelativePath){
    outRelativePath.clear();

    bool hasComponent = false;
    for(const Path& component : relativePath){
        const AString componentText = CanonicalizeText(PathToString(component));
        if(componentText.empty() || componentText == ".")
            continue;
        if(componentText == ".." || componentText.find('/') != AString::npos)
            return false;

        if(hasComponent)
            outRelativePath += '/';
        outRelativePath += componentText;
        hasComponent = true;
    }

    return hasComponent;
}

[[nodiscard]] inline bool ExtractAssetVirtualRoot(const AStringView virtualPath, CompactString& outVirtualRoot){
    outVirtualRoot.clear();

    const Path virtualPathPath(virtualPath);
    const auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    return outVirtualRoot.assign(PathToString(*componentIt)) && !outVirtualRoot.empty();
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPathText(const Path& assetRoot, const AStringView virtualRoot, const Path& sourceOrMetaPath, AString& outVirtualPath){
    outVirtualPath.clear();

    const Path relativePath = sourceOrMetaPath.lexically_relative(assetRoot);
    if(relativePath.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Assets: failed to derive asset path from '{}' relative to asset root '{}'"),
            PathToString<tchar>(sourceOrMetaPath),
            PathToString<tchar>(assetRoot)
        );
        return false;
    }

    Path logicalPath = relativePath;
    logicalPath.replace_extension();

    AString relativePathText;
    if(!BuildRelativeAssetPathText(logicalPath, relativePathText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Assets: asset '{}' is not under asset root '{}'"),
            PathToString<tchar>(sourceOrMetaPath),
            PathToString<tchar>(assetRoot)
        );
        return false;
    }

    outVirtualPath = StringFormat("{}/{}", virtualRoot, relativePathText);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BuildAssetRootVirtualRoot(const Path& assetRoot, CompactString& outVirtualRoot){
    outVirtualRoot.clear();

    AStringView virtualRootText;
    if(!__hidden_asset_paths::ResolveAssetRootVirtualRootText(assetRoot, virtualRootText))
        return false;
    if(!outVirtualRoot.assign(virtualRootText)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Assets: asset virtual root '{}' exceeds CompactString capacity"),
            StringConvert(AString(virtualRootText))
        );
        return false;
    }

    return true;
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(const Path& assetRoot, const AStringView virtualRoot, const Path& sourceOrMetaPath, AString& outVirtualPath){
    return __hidden_asset_paths::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, outVirtualPath);
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& sourceOrMetaPath,
    Name& outVirtualPath
){
    outVirtualPath = NAME_NONE;

    AString virtualPathText;
    if(!__hidden_asset_paths::BuildDerivedAssetVirtualPathText(assetRoot, virtualRoot, sourceOrMetaPath, virtualPathText))
        return false;

    outVirtualPath = Name(AStringView(virtualPathText));
    if(!outVirtualPath){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Assets: failed to derive asset name from '{}'"),
            PathToString<tchar>(sourceOrMetaPath)
        );
        return false;
    }

    return true;
}

[[nodiscard]] inline bool BuildDerivedAssetVirtualPath(
    const Path& assetRoot,
    const CompactString& virtualRoot,
    const Path& sourceOrMetaPath,
    Name& outVirtualPath
){
    return BuildDerivedAssetVirtualPath(assetRoot, virtualRoot.view(), sourceOrMetaPath, outVirtualPath);
}

[[nodiscard]] inline bool HasReservedAssetVirtualRoot(const AStringView virtualPath){
    CompactString virtualRoot;
    if(!__hidden_asset_paths::ExtractAssetVirtualRoot(virtualPath, virtualRoot))
        return false;

    return virtualRoot.view() == s_EngineVirtualRoot
        || virtualRoot.view() == s_ProjectVirtualRoot;
}

[[nodiscard]] inline bool ResolveVirtualAssetPath(const Vector<Path>& assetRoots, const AStringView virtualPath, Path& outResolvedPath){
    outResolvedPath.clear();

    const Path virtualPathPath(virtualPath);
    auto componentIt = virtualPathPath.begin();
    if(componentIt == virtualPathPath.end())
        return false;

    CompactString requestedVirtualRoot;
    if(!__hidden_asset_paths::ExtractAssetVirtualRoot(virtualPath, requestedVirtualRoot))
        return false;

    for(const Path& assetRoot : assetRoots){
        CompactString assetVirtualRoot;
        if(!BuildAssetRootVirtualRoot(assetRoot, assetVirtualRoot))
            return false;
        if(assetVirtualRoot != requestedVirtualRoot)
            continue;

        outResolvedPath = assetRoot;
        ++componentIt;
        for(; componentIt != virtualPathPath.end(); ++componentIt){
            const AString componentText = CanonicalizeText(PathToString(*componentIt));
            if(componentText.empty() || componentText == "." || componentText == ".." || componentText.find('/') != AString::npos){
                NWB_LOGGER_ERROR(
                    NWB_TEXT("Assets: invalid virtual path '{}'; components must not be empty, '.', '..' or contain path separators"),
                    StringConvert(virtualPath)
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

[[nodiscard]] inline bool ResolvePairedSourcePathFromMetadata(const Path& nwbFilePath, AString& outSourcePath){
    outSourcePath.clear();

    const Path parentDirectory = nwbFilePath.parent_path();
    if(parentDirectory.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata directory is empty"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    const AString nwbStem = CanonicalizeText(PathToString(nwbFilePath.stem()));
    if(nwbStem.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': failed to resolve paired source because the metadata filename stem is empty"),
            PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    ErrorCode errorCode;
    Path matchedSourcePath;
    usize matchCount = 0;
    const auto logDirectoryScanError = [&](){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': failed to scan metadata directory '{}': {}"),
            PathToString<tchar>(nwbFilePath),
            PathToString<tchar>(parentDirectory),
            StringConvert(errorCode.message())
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
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': failed to inspect '{}' while resolving paired source: {}"),
                PathToString<tchar>(nwbFilePath),
                PathToString<tchar>(dirEntry.path()),
                StringConvert(errorCode.message())
            );
            return false;
        }
        if(!isRegularFile)
            continue;

        const Path& candidatePath = dirEntry.path();
        if(CanonicalizeText(PathToString(candidatePath.extension())) == s_NwbExtension)
            continue;
        if(CanonicalizeText(PathToString(candidatePath.stem())) != nwbStem)
            continue;

        matchedSourcePath = candidatePath.lexically_normal();
        ++matchCount;
        if(matchCount > 1){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Meta '{}': paired source is ambiguous; multiple source files share stem '{}'"),
                PathToString<tchar>(nwbFilePath),
                StringConvert(nwbStem)
            );
            return false;
        }
    }

    if(errorCode){
        logDirectoryScanError();
        return false;
    }

    if(matchCount == 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Meta '{}': failed to find a paired source file with stem '{}'"),
            PathToString<tchar>(nwbFilePath),
            StringConvert(nwbStem)
        );
        return false;
    }

    outSourcePath = PathToString(matchedSourcePath);
    return true;
}

template<typename MetadataValue>
[[nodiscard]] inline bool RejectVirtualPathOverrideField(const Path& nwbFilePath, const MetadataValue& asset, const AStringView assetLabel){
    if(!asset.findField("name"))
        return true;

    NWB_LOGGER_ERROR(
        NWB_TEXT("{} meta '{}': field 'name' is no longer supported; virtual paths are derived from the asset file hierarchy"),
        StringConvert(assetLabel),
        PathToString<tchar>(nwbFilePath)
    );
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
