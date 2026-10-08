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


static bool TryResolveMaterialBindDependencyInterface(
    const Path& normalizedMaterialBindIncludeRoot,
    const Path& dependency,
    CookString& outInterfacePath,
    ScratchArena& scratchArena
){
    outInterfacePath.clear();
    if(normalizedMaterialBindIncludeRoot.empty())
        return true;

    auto normalizedDependencyResult = AbsolutePath(dependency);
    if(!normalizedDependencyResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize shader dependency '{}': {}")
            , PathToString<tchar>(dependency)
            , StringConvert(normalizedDependencyResult.error().message())
        );
        return false;
    }
    Path normalizedDependency = normalizedDependencyResult->lexicallyNormal();

    if(!PathHasDirectoryAncestor(normalizedDependency, normalizedMaterialBindIncludeRoot))
        return true;

    ScratchString extension = PathToString(scratchArena, normalizedDependency.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != MaterialBindNames::SourceExtensionText())
        return true;

    Path relativePath = normalizedDependency.lexicallyRelative(normalizedMaterialBindIncludeRoot);
    relativePath.replaceExtension();
    if(!Core::Assets::AssetPathsDetail::BuildRelativeAssetPathText(relativePath, outInterfacePath)){
        NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to derive interface from generated include '{}'")
            , PathToString<tchar>(normalizedDependency)
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<MaterialBindDependency> ResolveMaterialBindDependencyInterface(
    CookArena& arena,
    const AStringView shaderName,
    const Path& materialBindIncludeRoot,
    const CookVector<Path>& dependencies,
    ScratchArena& scratchArena
){
    MaterialBindDependency result(arena);

    Path normalizedMaterialBindIncludeRoot(materialBindIncludeRoot.arena());
    if(!materialBindIncludeRoot.empty()){
        const auto normalizedRoot = AbsolutePath(materialBindIncludeRoot);
        if(!normalizedRoot){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: failed to normalize generated include root '{}': {}")
                , PathToString<tchar>(materialBindIncludeRoot)
                , StringConvert(normalizedRoot.error().message())
            );
            return MakeUnexpected(Failure{});
        }
        normalizedMaterialBindIncludeRoot = normalizedRoot->lexicallyNormal();
    }

    CookString dependencyInterfacePath{arena};
    bool dependsOnMultipleInterfaces = false;
    for(const Path& dependency : dependencies){
        if(!TryResolveMaterialBindDependencyInterface(
            normalizedMaterialBindIncludeRoot,
            dependency,
            dependencyInterfacePath,
            scratchArena
        ))
            return MakeUnexpected(Failure{});
        if(dependencyInterfacePath.empty())
            continue;

        const Name dependencyInterfaceName{ AStringView(dependencyInterfacePath) };
        if(!dependencyInterfaceName){
            NWB_LOGGER_ERROR(NWB_TEXT("Material bind dependency: shader '{}' includes invalid generated "
                "material bind interface '{}'")
                , StringConvert(shaderName)
                , StringConvert(dependencyInterfacePath)
            );
            return MakeUnexpected(Failure{});
        }

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

