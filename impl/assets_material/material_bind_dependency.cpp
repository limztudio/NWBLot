// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace MaterialCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool TryResolveMaterialBindDependencyInterface(const ScratchPath& normalizedMaterialBindIncludeRoot, const Path& dependency, ScratchArena& scratchArena, ScratchString& outInterfacePath){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    auto normalizedDependencyResult = AbsolutePath(ScratchPath(scratchArena, dependency));
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}"), PathToString<tchar>(dependency), StringConvert(normalizedDependencyResult.error().message()));
        return false;
    }
    ScratchPath normalizedDependency = normalizedDependencyResult->lexicallyNormal();

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchString extension = PathToString(scratchArena, normalizedDependency.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != MaterialBindNames::SourceExtensionText())
        return true;

    ScratchPath relativePath = normalizedDependency.lexicallyRelative(normalizedMaterialBindIncludeRoot);
    relativePath.replaceExtension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'"), PathToString<tchar>(normalizedDependency));
        return false;
    }

    return true;
}

static const MaterialBindEntry* FindMaterialBindEntry(const CookVector<MaterialBindEntry>& materialBindEntries, const Name& interfaceName){
    for(const MaterialBindEntry& bindEntry : materialBindEntries){
        const auto bindInterfaceName = Name::FromText(AStringView(bindEntry.virtualPath));
        if(bindInterfaceName && *bindInterfaceName == interfaceName)
            return &bindEntry;
    }
    return nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    CookArena& arena,
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<MaterialBindEntry>& materialBindEntries,
    const CookVector<Path>& dependencies,
    ScratchArena& scratchArena
){
    MaterialBindDependency result(arena);

    ScratchPath normalizedMaterialBindIncludeRoot(scratchArena);
    if(!materialBindIncludeRoot.empty()){
        const auto normalizedRoot = AbsolutePath(ScratchPath(scratchArena, materialBindIncludeRoot));
        if(!normalizedRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}"), PathToString<tchar>(materialBindIncludeRoot), StringConvert(normalizedRoot.error().message()));
            return MakeUnexpected(Failure{});
        }
        normalizedMaterialBindIncludeRoot = normalizedRoot->lexicallyNormal();
    }

    ScratchString dependencyInterfacePath{scratchArena};
    bool dependsOnMultipleInterfaces = false;
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(normalizedMaterialBindIncludeRoot, dependency, scratchArena, dependencyInterfacePath))
            return MakeUnexpected(Failure{});
        if(dependencyInterfacePath.empty())
            continue;

        const auto dependencyInterfaceNameResult = Name::FromText(AStringView(dependencyInterfacePath));
        if(!dependencyInterfaceNameResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated " "material bind interface '{}'"), StringConvert(shaderName), StringConvert(dependencyInterfacePath));
            return MakeUnexpected(Failure{});
        }
        const Name dependencyInterfaceName = *dependencyInterfaceNameResult;

        // Each generated include comes from one authored file, and Gather already drops repeated paths, so this list has no repeats.
        const MaterialBindEntry* bindEntry = FindMaterialBindEntry(materialBindEntries, dependencyInterfaceName);
        if(!bindEntry){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes generated material bind interface '{}' with no authored source"), StringConvert(shaderName), StringConvert(dependencyInterfacePath));
            return MakeUnexpected(Failure{});
        }
        result.interfaceSources.emplace_back(arena, AStringView(bindEntry->source));

        // Reads typed material constants, so it needs the typed binding.
        result.dependsOnMaterialBind = true;
        if(dependsOnMultipleInterfaces)
            continue;

        if(!result.interfaceName){
            result.interfacePath = dependencyInterfacePath;
            result.interfaceName = dependencyInterfaceName;
            continue;
        }

        if(result.interfaceName != dependencyInterfaceName){
            // Generic dispatch consumer: no single owning interface.
            result.interfacePath.clear();
            result.interfaceName = s_NameNone;
            dependsOnMultipleInterfaces = true;
        }
    }

    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

