// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_scene/components.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_model/components.h>
#include <impl/ecs_render/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ModelComponentAsset final : public TypedComponent<ModelComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("model_component")


public:
    ModelComponent component;


public:
    using TypedComponent<ModelComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};

class MeshComponentAsset final : public TypedComponent<MeshComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("mesh_component")


public:
    MeshComponent component;


public:
    using TypedComponent<MeshComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};

class RendererComponentAsset final : public TypedComponent<RendererComponentAsset>{
public:
    NWB_DEFINE_ASSET_TYPE("renderer_component")


public:
    RendererComponent component;


public:
    using TypedComponent<RendererComponentAsset>::TypedComponent;


public:
    [[nodiscard]] virtual bool loadComponentPayload(Span<const u8> payload)override;
    [[nodiscard]] virtual bool validatePayload()const override;
    virtual void applyToEntity(Core::ECS::Entity& entity)const override;
#if defined(NWB_COOK)
    [[nodiscard]] virtual bool serializeComponentPayload(Core::Assets::AssetBytes& outPayload)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
