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
    if(mesh.meshObjectCullSource.empty() && mesh.meshObjectVertexSource.empty())
        return true;

    // Cache decoding and these two raster stages share a fixed engine ABI, independent of authored material defines.
    const Path expectedSource = resolvedPaths.repoRoot / "impl" / "assets" / "graphics" / "mesh" / "shared_ms.slang";
    if(
        mesh.name != "engine/graphics/mesh/shared_ms"
        || mesh.archiveStage.view() != "mesh"
        || mesh.stage.view() != "mesh"
        || meshEntry.sourcePath.lexically_normal() != expectedSource.lexically_normal()
        || mesh.meshObjectCullSource != "object_cull_cs.slang"
        || mesh.meshObjectVertexSource != "object_vs.slang"
        || !mesh.emitMeshComputeShadow
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: object geometry stages require the fixed engine shared mesh program"));
        return false;
    }

    const AStringView archiveStages[] = {
        MaterialShaderStageNames::MeshObjectCullArchiveStageText(),
        MaterialShaderStageNames::MeshObjectVertexArchiveStageText(),
    };
    constexpr AStringView stages[] = { "cs", "vs" };
    constexpr AStringView filenames[] = { "object_cull_cs.slang", "object_vs.slang" };
    for(u32 stageIndex = 0u; stageIndex < LengthOf(stages); ++stageIndex){
        PreparedShaderEntry prepared(cookArena);
        prepared.entry.name = mesh.name;
        if(!prepared.entry.stage.assign(stages[stageIndex])
            || !prepared.entry.archiveStage.assign(archiveStages[stageIndex])
            || !prepared.entry.targetProfile.assign("spirv_1_5"))
            return false;
        prepared.entry.emitMeshComputeShadow = false;
        prepared.sourcePath = meshEntry.sourcePath.parent_path() / filenames[stageIndex];
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
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

