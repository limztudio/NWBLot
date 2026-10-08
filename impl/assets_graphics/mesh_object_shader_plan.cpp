// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_object_shader_plan.h"

#include <impl/assets_material/shader_stage_names.h>

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <core/graphics/shader_stage_names.h>


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
    // The fixed decoder and this raster stage share an engine ABI, independent of authored material defines.
    static constexpr AStringView s_SharedMeshProgramName = "engine/graphics/mesh/shared_ms";
    static constexpr AStringView s_ObjectVertexSourceName = "object_vs.slang";
    static constexpr AStringView s_ImplPathToken = "impl";
    static constexpr AStringView s_AssetsPathToken = "assets";
    static constexpr AStringView s_GraphicsPathToken = "graphics";
    static constexpr AStringView s_MeshPathToken = "mesh";
    static constexpr AStringView s_SharedMeshSourceFile = "shared_ms.slang";
    if(mesh.name != s_SharedMeshProgramName && !TStringView(meshEntry.sourcePath.native()).ends_with(NWB_TEXT("shared_ms.slang")))
        return true;

    const Path expectedSource = resolvedPaths.repoRoot / s_ImplPathToken / s_AssetsPathToken / s_GraphicsPathToken / s_MeshPathToken / s_SharedMeshSourceFile;
    const bool hasFixedSource = meshEntry.sourcePath.lexicallyNormal() == expectedSource.lexicallyNormal();
    if(mesh.name != s_SharedMeshProgramName && !hasFixedSource)
        return true;
    if(
        mesh.name != s_SharedMeshProgramName
        || mesh.archiveStage.view() != Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage)
        || mesh.stage.view() != Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::MeshStage)
        || !hasFixedSource
        || !mesh.emitMeshComputeShadow
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetBuilder: object geometry stages require the fixed engine shared mesh program"));
        return false;
    }

    PreparedShaderEntry prepared(cookArena);
    prepared.entry.name = mesh.name;
    if(!prepared.entry.stage.assign(Core::ShaderStageNames::ArchiveStageTextFromShaderType(Core::ShaderType::VertexStage))
        || !prepared.entry.archiveStage.assign(MaterialShaderStageNames::s_MeshObjectVertexArchiveStageText))
        return false;
    prepared.entry.emitMeshComputeShadow = false;
    prepared.sourcePath = meshEntry.sourcePath.parentPath() / s_ObjectVertexSourceName;
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

