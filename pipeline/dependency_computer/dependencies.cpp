// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "dependencies.h"

#include "provider_catalog.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_font/asset.h>
#include <impl/assets_font_atlas/asset.h>
#include <impl/assets_graphics/mesh/names.h>
#include <impl/assets_material/cook.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_mesh/skin_asset.h>
#include <impl/assets_model/cook.h>
#include <impl/assets_scene/cook.h>
#include <impl/assets_skeleton/asset.h>
#include <impl/assets_subsystem/cook.h>
#include <impl/assets_texture/asset.h>
#include <impl/assets_ui_skin/cook.h>

#include <core/assets/bunch/cook.h>
#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_dependency_computer_dependencies{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
namespace Assets = Core::Assets;

inline constexpr Name s_AssetBunchTypeName("asset_bunch");
inline constexpr AStringView s_FontAtlasDiagnosticPrefix = "Font atlas meta";
inline constexpr AStringView s_FontField = "font";
inline constexpr AStringView s_SkinDiagnosticPrefix = "Skin meta";
inline constexpr AStringView s_MeshField = "mesh";
inline constexpr AStringView s_SkeletonField = "skeleton";

using KnownVirtualPathTextMap = HashMap<Name, Assets::AssetString, Assets::ScratchArena, Hasher<Name>, EqualTo<Name>>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Expected<bool, NameTextError::Enum> HasAssetBunchDeclaration(const Core::Metascript::Document& doc){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const AStringView typeName(declaration.type.data(), declaration.type.size());
        const auto parsedName = ToName(typeName);
        if(!parsedName)
            return MakeUnexpected(parsedName.error());
        if(*parsedName && **parsedName == s_AssetBunchTypeName)
            return true;
    }
    return false;
}

static void RecordVirtualPathText(const AStringView rawText, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    const AStringView trimmed = TrimView(rawText);
    if(trimmed.empty() || trimmed.find('/') == AStringView::npos)
        return;

    const auto name = Name::FromText(trimmed);
    if(!name || !*name)
        return;

    if(outMap.find(*name) == outMap.end())
        outMap.emplace(*name, Assets::AssetString(trimmed, arena));
}

static void CollectValueVirtualPathTexts(const Core::Metascript::Value& value, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    if(value.isString()){
        const Core::Metascript::MStringView str = value.asString();
        RecordVirtualPathText(AStringView(str.data(), str.size()), arena, outMap);
        return;
    }
    if(value.isList()){
        for(const Core::Metascript::Value& item : value.asList())
            CollectValueVirtualPathTexts(item, arena, outMap);
        return;
    }
    if(value.isMap()){
        for(const auto& [key, fieldValue] : value.asMap()){
            RecordVirtualPathText(AStringView(key.data(), key.size()), arena, outMap);
            CollectValueVirtualPathTexts(fieldValue, arena, outMap);
        }
    }
}

static void CollectDocumentVirtualPathTexts(const Core::Metascript::Document& doc, Assets::AssetArena& arena, KnownVirtualPathTextMap& outMap){
    for(const Core::Metascript::Document::Declaration& declaration : doc.declarations()){
        const Core::Metascript::MStringView varView(declaration.variable.data(), declaration.variable.size());
        if(const Core::Metascript::Value* value = doc.findVariable(varView))
            CollectValueVirtualPathTexts(*value, arena, outMap);
    }
}

[[nodiscard]] static AStringView LookupVirtualPathTextHint(const Name& virtualPath, const KnownVirtualPathTextMap& knownTexts){
    const auto found = knownTexts.find(virtualPath);
    if(found != knownTexts.end())
        return AStringView(found.value());
    return virtualPath.resolvedText();
}

[[nodiscard]] static bool IsMaterialResourceParameterValue(const AStringView valueText, Assets::ScratchArena& scratchArena){
    const AStringView trimmed = TrimView(valueText);
    if(trimmed.empty() || trimmed.find('(') != AStringView::npos || trimmed.find(')') != AStringView::npos)
        return false;
    return Assets::HasReservedAssetVirtualRoot(trimmed, scratchArena);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NWB::Core::Assets::AssetVector<NWB::Core::Assets::AssetString>> ComputeAssetDependencies(const PipelineOptions& options, NWB::Core::Assets::AssetArena& arena, NWB::Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_dependency_computer_dependencies;
    using namespace NWB;
    namespace Assets = Core::Assets;

    DependencyProviderCatalog catalog(arena);
    if(!catalog.discover(options, scratchArena))
        return MakeUnexpected(Failure{});
    auto selection = catalog.selectInputs(options, scratchArena);
    if(!selection)
        return MakeUnexpected(Failure{});

    const auto& files = catalog.m_files;
    Assets::AssetVector<Assets::AssetString> candidate(arena);
    if(!options.inputs.empty()){
        candidate.reserve(options.inputs.size());
        for(const auto& input : options.inputs)
            candidate.emplace_back(input, arena);
    }
    else{
        candidate.reserve(selection->order.size());
        for(const usize index : selection->order)
            candidate.emplace_back(PathToString(arena, files[index].filePath.lexicallyNormal()));
    }

    const Span<u8> included = selection->selected;
    Vector<u8, Assets::ScratchArena> processed(files.size(), u8(0), scratchArena);
    Vector<usize, Assets::ScratchArena> fileQueue(selection->order, scratchArena);
    HashSet<Name, Assets::ScratchArena, Hasher<Name>, EqualTo<Name>> activeFeatures(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    Assets::AssetVector<Impl::SubsystemCookEntry> activeSubsystems(arena);
    KnownVirtualPathTextMap knownVirtualPathTexts(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);

    RecordVirtualPathText(Impl::AssetsGraphicsMesh::s_SharedMeshShaderText, arena, knownVirtualPathTexts);
    RecordVirtualPathText("engine/graphics/mesh/object_decode_cs", arena, knownVirtualPathTexts);
    RecordVirtualPathText("engine/graphics/mesh/emulation_vs", arena, knownVirtualPathTexts);

    const auto enqueueDependency = [&](const Name& depVirtualPath, const Name& expectedAssetType) -> bool{
        if(!depVirtualPath)
            return false;
        const AStringView hint = LookupVirtualPathTextHint(depVirtualPath, knownVirtualPathTexts);
        const auto provider = catalog.resolve(depVirtualPath, expectedAssetType, hint, scratchArena);
        if(!provider)
            return false;
        if(!included[*provider]){
            candidate.emplace_back(PathToString(arena, files[*provider].filePath.lexicallyNormal()));
            included[*provider] = 1u;
        }
        if(!processed[*provider])
            fileQueue.push_back(*provider);
        return true;
    };

    const auto activateFeature = [&](const Name& featureName) -> bool{
        if(!featureName)
            return false;
        if(!activeFeatures.insert(featureName).second)
            return true;

        for(const Impl::SubsystemCookEntry& subsystem : activeSubsystems){
            for(const Impl::SubsystemFeature& feature : subsystem.features){
                if(feature.name != featureName)
                    continue;
                for(const Assets::AssetRef<Assets::IAsset>& assetRef : feature.assets){
                    if(!enqueueDependency(assetRef.name(), s_NameNone))
                        return false;
                }
            }
        }
        return true;
    };

    const auto processAssetItem = [&](
        const usize fileIndex,
        const Name& assetType,
        const Name& virtualPath,
        const Core::Metascript::Value& assetValue,
        const Core::Metascript::Document* documentOrNull
    ) -> bool{
        const auto& file = files[fileIndex];

        if(assetType == Impl::ProjectAsset::s_AssetTypeName){
            if(!documentOrNull)
                return false;
            const auto projectEntry = Impl::ParseProjectCookMetadata(file.assetRoot, file.virtualRoot.view(), file.filePath, *documentOrNull, arena, scratchArena);
            if(!projectEntry)
                return false;
            return enqueueDependency(projectEntry->startScene.name(), Impl::SceneAsset::s_AssetTypeName);
        }

        if(assetType == Impl::SceneAsset::s_AssetTypeName){
            if(!documentOrNull)
                return false;
            const auto sceneEntry = Impl::ParseSceneCookMetadata(file.assetRoot, file.virtualRoot.view(), file.filePath, *documentOrNull, arena, scratchArena);
            if(!sceneEntry)
                return false;
            for(const Assets::AssetRef<Impl::Subsystem>& subsystemRef : sceneEntry->subsystems){
                if(!enqueueDependency(subsystemRef.name(), Impl::Subsystem::s_AssetTypeName))
                    return false;
            }
            for(const Name& featureName : sceneEntry->enabledFeatures){
                if(!activateFeature(featureName))
                    return false;
            }
            for(const Assets::AssetRef<Assets::IAsset>& assetRef : sceneEntry->assets){
                if(!enqueueDependency(assetRef.name(), s_NameNone))
                    return false;
            }
            return true;
        }

        if(assetType == Impl::Subsystem::s_AssetTypeName){
            if(!documentOrNull)
                return false;
            auto subsystemEntry = Impl::ParseSubsystemCookMetadata(file.assetRoot, file.virtualRoot.view(), file.filePath, *documentOrNull, arena, scratchArena);
            if(!subsystemEntry)
                return false;
            for(const Assets::AssetRef<Impl::Subsystem>& childSubsystem : subsystemEntry->subsystems){
                if(!enqueueDependency(childSubsystem.name(), Impl::Subsystem::s_AssetTypeName))
                    return false;
            }
            for(const Assets::AssetRef<Assets::IAsset>& assetRef : subsystemEntry->assets){
                if(!enqueueDependency(assetRef.name(), s_NameNone))
                    return false;
            }
            for(const Impl::SubsystemFeature& feature : subsystemEntry->features){
                if(!activeFeatures.contains(feature.name))
                    continue;
                for(const Assets::AssetRef<Assets::IAsset>& assetRef : feature.assets){
                    if(!enqueueDependency(assetRef.name(), s_NameNone))
                        return false;
                }
            }
            activeSubsystems.push_back(Move(*subsystemEntry));
            return true;
        }

        if(assetType == Impl::UiSkin::s_AssetTypeName){
            if(!documentOrNull)
                return false;
            const auto skinEntry = Impl::ParseUiSkinCookMetadata(file.assetRoot, file.virtualRoot.view(), file.filePath, *documentOrNull, arena, scratchArena);
            if(!skinEntry)
                return false;
            return enqueueDependency(skinEntry->texture.name(), Impl::Texture::s_AssetTypeName);
        }

        if(assetType == Impl::FontAtlas::s_AssetTypeName){
            const auto fontRef = Assets::ReadMetadataAssetRefField<Impl::Font>(file.filePath, assetValue, s_FontAtlasDiagnosticPrefix, s_FontField, true);
            if(!fontRef)
                return false;
            return enqueueDependency(fontRef->name(), Impl::Font::s_AssetTypeName);
        }

        if(assetType == Impl::Model::s_AssetTypeName){
            const auto modelEntry = Impl::ParseModelCookMetadata(virtualPath, file.filePath, assetValue, arena, scratchArena);
            if(!modelEntry)
                return false;

            for(const Impl::ModelSkeletonObject& skeletonObject : modelEntry->skeletonObjects){
                if(!enqueueDependency(skeletonObject.skeleton.name(), Impl::Skeleton::s_AssetTypeName))
                    return false;
            }
            for(const Impl::ModelStaticMeshObject& staticMeshObject : modelEntry->staticMeshObjects){
                if(!enqueueDependency(staticMeshObject.mesh.name(), Impl::Mesh::s_AssetTypeName))
                    return false;
                if(staticMeshObject.material.valid() && !enqueueDependency(staticMeshObject.material.name(), Impl::Material::s_AssetTypeName))
                    return false;
            }
            for(const Impl::ModelSkinnedMeshObject& skinnedMeshObject : modelEntry->skinnedMeshObjects){
                if(!enqueueDependency(skinnedMeshObject.skin.name(), Impl::Skin::s_AssetTypeName))
                    return false;
                if(skinnedMeshObject.material.valid() && !enqueueDependency(skinnedMeshObject.material.name(), Impl::Material::s_AssetTypeName))
                    return false;
            }
            if(!modelEntry->skinnedMeshObjects.empty() && !activateFeature(Impl::BuiltInSubsystemFeatures::s_Skinning))
                return false;
            return true;
        }

        if(assetType == Impl::Skin::s_AssetTypeName){
            const auto meshRef = Assets::ReadMetadataAssetRefField<Impl::Mesh>(file.filePath, assetValue, s_SkinDiagnosticPrefix, s_MeshField, true);
            if(!meshRef)
                return false;
            const auto skeletonRef = Assets::ReadMetadataAssetRefField<Impl::Skeleton>(file.filePath, assetValue, s_SkinDiagnosticPrefix, s_SkeletonField, true);
            if(!skeletonRef)
                return false;
            if(!enqueueDependency(meshRef->name(), Impl::Mesh::s_AssetTypeName))
                return false;
            if(!enqueueDependency(skeletonRef->name(), Impl::Skeleton::s_AssetTypeName))
                return false;
            return activateFeature(Impl::BuiltInSubsystemFeatures::s_Skinning);
        }

        if(assetType == Impl::Material::s_AssetTypeName){
            if(!documentOrNull)
                return false;
            const auto materialEntry = Impl::ParseMaterialCookMetadata(file.assetRoot, file.virtualRoot.view(), file.filePath, *documentOrNull, arena, scratchArena);
            if(!materialEntry)
                return false;

            if(materialEntry->transparent && !activateFeature(Impl::BuiltInSubsystemFeatures::s_Transparent))
                return false;
            if(materialEntry->refractive && !activateFeature(Impl::BuiltInSubsystemFeatures::s_Refractive))
                return false;
            if(!materialEntry->surfaceSource.empty()){
                if(!enqueueDependency(Impl::AssetsGraphicsMesh::s_SharedMeshShaderName, s_NameNone))
                    return false;
            }

            Vector<Name, Assets::ScratchArena> stageShaderNames(scratchArena);
            stageShaderNames.reserve(materialEntry->stageShaders.size());
            for(const auto& [shaderType, shaderRef] : materialEntry->stageShaders){
                static_cast<void>(shaderType);
                stageShaderNames.push_back(shaderRef.name());
            }
            Sort(stageShaderNames.begin(), stageShaderNames.end());
            for(const Name& shaderName : stageShaderNames){
                if(!enqueueDependency(shaderName, s_NameNone))
                    return false;
            }

            Vector<AStringView, Assets::ScratchArena> resourceParameterValues(scratchArena);
            resourceParameterValues.reserve(materialEntry->parameters.size());
            for(const auto& [paramKey, paramValue] : materialEntry->parameters){
                static_cast<void>(paramKey);
                const AStringView valueView = TrimView(paramValue.view());
                if(IsMaterialResourceParameterValue(valueView, scratchArena))
                    resourceParameterValues.push_back(valueView);
            }
            Sort(resourceParameterValues.begin(), resourceParameterValues.end());
            for(const AStringView resourceValue : resourceParameterValues){
                const auto resourceName = Name::FromText(resourceValue);
                if(!resourceName || !*resourceName)
                    return false;
                if(!enqueueDependency(*resourceName, s_NameNone))
                    return false;
            }
            return true;
        }

        if(assetType == Impl::AssetsCsgCook::s_CsgShapeAssetTypeName){
            if(!documentOrNull)
                return false;
            const auto csgEntry = Impl::AssetsCsgCook::ParseCsgShapeCookMetadata(arena, file.filePath, *documentOrNull, scratchArena);
            if(!csgEntry)
                return false;
            return activateFeature(Impl::BuiltInSubsystemFeatures::s_Csg);
        }

        return true;
    };

    for(usize queueCursor = 0u; queueCursor < fileQueue.size(); ++queueCursor){
        const usize fileIndex = fileQueue[queueCursor];
        if(processed[fileIndex])
            continue;
        processed[fileIndex] = 1u;

        const auto document = catalog.read(fileIndex, scratchArena);
        if(!document)
            return MakeUnexpected(Failure{});
        CollectDocumentVirtualPathTexts(*document, arena, knownVirtualPathTexts);

        const auto hasBunch = HasAssetBunchDeclaration(*document);
        if(!hasBunch)
            return MakeUnexpected(Failure{});
        if(*hasBunch){
            const auto& file = files[fileIndex];
            const auto expandedAssets = Assets::AssetsBunchCook::ExpandAssetBunch(file.assetRoot, file.virtualRoot.view(), file.filePath, *document, scratchArena);
            if(!expandedAssets)
                return MakeUnexpected(Failure{});
            for(const Assets::ExpandedAssetMetadata& expanded : *expandedAssets)
                CollectValueVirtualPathTexts(expanded.value, arena, knownVirtualPathTexts);
            for(const Assets::ExpandedAssetMetadata& expanded : *expandedAssets){
                if(!processAssetItem(fileIndex, expanded.assetType, expanded.virtualPath, expanded.value, nullptr))
                    return MakeUnexpected(Failure{});
            }
            continue;
        }

        if(document->declarations().size() != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: provider '{}' declares multiple assets without an asset_bunch"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto* assetDeclaration = Assets::FindMetadataAssetDeclaration(*document);
        if(!assetDeclaration){
            NWB_LOGGER_ERROR(NWB_TEXT("DependencyComputer: meta '{}' is missing asset declaration 'asset'"), PathToString<tchar>(files[fileIndex].filePath));
            return MakeUnexpected(Failure{});
        }
        const auto documentAssetType = Name::FromText(AStringView(assetDeclaration->type.data(), assetDeclaration->type.size()));
        if(!documentAssetType)
            return MakeUnexpected(Failure{});
        const auto assetValue = Assets::FindMetadataAssetValue<Core::Metascript::Document, Core::Metascript::Value>(files[fileIndex].filePath, *document, "DependencyComputer");
        if(!assetValue)
            return MakeUnexpected(Failure{});
        if(!processAssetItem(fileIndex, *documentAssetType, catalog.m_virtualPaths[fileIndex], **assetValue, &*document))
            return MakeUnexpected(Failure{});
    }

    return candidate;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

