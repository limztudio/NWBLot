// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_system.h"

#include <impl/assets_graphics/deferred/subsystem.h>
#include <impl/assets_shader/loader.h>

#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererShaderSystem::loadShaderForStage(Core::ShaderHandle& outShader, const Name& shaderAssetName, const AStringView variantName, const Core::ShaderType::Mask shaderType, const Name& debugName, const Name* archiveStageName){
    if(!shaderAssetName)
        return false;
    return ShaderAssetLoader::LoadForStage(outShader, shaderAssetName, variantName, shaderType, debugName, m_graphics, m_assetManager, m_shaderPathResolver, s_RendererAssetOwnerName, archiveStageName);
}


bool RendererShaderSystem::hasShaderArchiveStage(const Name& shaderAssetName, const AStringView variantName, const Name& stageName)const{
    return m_shaderPathResolver(shaderAssetName, variantName, stageName).has_value();
}


bool RendererShaderSystem::loadDeferredCompositeVertexShader(){
    return loadShader<VertexShader>(m_deferredCompositeVertexShader, &DeferredSubsystem::compositeVs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredCompositeVS");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

