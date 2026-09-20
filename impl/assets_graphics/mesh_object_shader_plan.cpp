// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_object_shader_plan.h"

#include <impl/assets_material/shader_stage_names.h>

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsGraphicsCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AppendMeshObjectShaderEntries(
    ShaderCook::CookArena& cookArena,
    ShaderCook& shaderCook,
    const ResolvedCookPaths& resolvedPaths,
    const PreparedShaderEntry& meshEntry,
    PreparedShaderPlan& plan,
    ScratchArena& scratchArena
){
    const ShaderCook::ShaderEntry& mesh = meshEntry.entry;
    if(mesh.meshObjectVertexSource.empty())
        return true;

    // The fixed decoder and this raster stage share an engine ABI, independent of authored material defines.
    const Path expectedSource = resolvedPaths.repoRoot / "impl" / "assets" / "graphics" / "mesh" / "shared_ms.slang";
    if(
        mesh.name != "engine/graphics/mesh/shared_ms"
        || mesh.archiveStage.view() != "mesh"
        || mesh.stage.view() != "mesh"
        || meshEntry.sourcePath.lexically_normal() != expectedSource.lexically_normal()
        || mesh.meshObjectVertexSource != "object_vs.slang"
        || !mesh.emitMeshComputeShadow
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: object geometry stages require the fixed engine shared mesh program"));
        return false;
    }

    PreparedShaderEntry prepared(cookArena);
    prepared.entry.name = mesh.name;
    if(!prepared.entry.stage.assign("vs")
        || !prepared.entry.archiveStage.assign(MaterialShaderStageNames::MeshObjectVertexArchiveStageText())
        || !prepared.entry.targetProfile.assign("spirv_1_5"))
        return false;
    prepared.entry.emitMeshComputeShadow = false;
    prepared.sourcePath = meshEntry.sourcePath.parent_path() / "object_vs.slang";
    prepared.entry.source = PathToString(cookArena, prepared.sourcePath);
    prepared.includeDirectories = meshEntry.includeDirectories;
    prepared.variantCount = 1u;

    ErrorCode errorCode;
    if(!IsRegularFile(prepared.sourcePath, errorCode) || errorCode){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: fixed object geometry shader is missing: '{}'"), PathToString<tchar>(prepared.sourcePath));
        return false;
    }
    if(!shaderCook.gatherShaderDependencies(prepared.sourcePath, prepared.includeDirectories, prepared.dependencies, scratchArena))
        return false;
    if(!shaderCook.computeDependencyChecksum(
        prepared.dependencies,
        { { resolvedPaths.repoRoot, "repo" }, { resolvedPaths.cacheDirectory, "generated_cache" } },
        prepared.dependencyChecksum,
        scratchArena
    ))
        return false;
    if(!Core::Assets::AddPlannedFileCount(prepared.variantCount, plan.plannedFileCount))
        return false;
    plan.preparedEntries.push_back(Move(prepared));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

