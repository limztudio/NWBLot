// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "shader_path_resolver.h"

#include <impl/assets_scene/asset.h>
#include <impl/assets_shader/loader.h>

#include <core/assets/global.h>
#include <core/graphics/rhi/shader.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_RendererAssetOwnerName = NWB_TEXT("RendererSystem");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererShaderSystem final : NoCopy{
public:
    RendererShaderSystem(Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager, RendererShaderPathResolveCallback& shaderPathResolver)noexcept;


public:
    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{ m_activeSceneSubsystems = subsystems; }
    [[nodiscard]] Name resolveShaderName(const Name& shaderName)const noexcept{
        if(m_activeSceneSubsystems){
            const auto resolved = m_activeSceneSubsystems->findAsset(shaderName);
            if(resolved.valid())
                return resolved.name();
        }
        return shaderName;
    }

    void invalidateResources()noexcept;
    template<typename TShader>
        requires(IsBaseOf_V<IShader, TShader> && requires{ TShader::s_Stage; })
    [[nodiscard]] bool loadShader(Core::ShaderHandle& outShader, const Name& shaderName, const AStringView variantName, const Name& debugName, const Name* archiveStageName = nullptr){
        static_assert(Core::ShaderType::IsValid(TShader::s_Stage));
        const Name resolvedShaderName = resolveShaderName(shaderName);
        return ShaderAssetLoader::Load<TShader>(outShader, resolvedShaderName, variantName, debugName, m_graphics, m_assetManager, m_shaderPathResolver, s_RendererAssetOwnerName, archiveStageName);
    }

    [[nodiscard]] bool loadShaderForStage(Core::ShaderHandle& outShader, const Name& shaderName, AStringView variantName, Core::ShaderType::Mask shaderType, const Name& debugName, const Name* archiveStageName = nullptr);
    [[nodiscard]] bool hasShaderArchiveStage(const Name& shaderName, AStringView variantName, const Name& stageName)const;
    [[nodiscard]] bool loadDeferredCompositeVertexShader();
    [[nodiscard]] const Core::ShaderHandle& deferredCompositeVertexShader()const noexcept{
        return m_deferredCompositeVertexShader;
    }


private:
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    RendererShaderPathResolveCallback& m_shaderPathResolver;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;
    Core::ShaderHandle m_deferredCompositeVertexShader;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

