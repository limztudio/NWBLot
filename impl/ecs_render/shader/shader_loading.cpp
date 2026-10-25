// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shader_system.h"

#include <impl/assets_graphics/deferred/names.h>
#include <impl/assets_shader/loader.h>

#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererShaderSystem::loadShaderForStage(Core::ShaderHandle& outShader, const Name& shaderName, const AStringView variantName, const Core::ShaderType::Mask shaderType, const Name& debugName, const Name* archiveStageName){
    const Name resolvedShaderName = resolveShaderName(shaderName);
    if(!resolvedShaderName)
        return false;
    return ShaderAssetLoader::LoadForStage(outShader, resolvedShaderName, variantName, shaderType, debugName, m_graphics, m_assetManager, m_shaderPathResolver, s_RendererAssetOwnerName, archiveStageName);
}


bool RendererShaderSystem::hasShaderArchiveStage(const Name& shaderAssetName, const AStringView variantName, const Name& stageName)const{
    return m_shaderPathResolver(shaderAssetName, variantName, stageName).has_value();
}


bool RendererShaderSystem::loadDeferredCompositeVertexShader(){
    return loadShader<VertexShader>(m_deferredCompositeVertexShader, AssetsGraphicsDeferred::s_CompositeVertexShaderName, Core::ShaderArchive::s_DefaultVariant, "ECSRender_DeferredCompositeVS");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

