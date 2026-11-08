// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "csg_shadow_mode.h"
#include "smoke_scene_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{

inline constexpr AStringView s_CsgShadowMaterialInterface = "project/shaders/csg_shadow";


class CsgShadowAtlas final{
public:
    CsgShadowAtlas(Core::ECS::World& world, const CsgShadowMode& mode, const SmokeMaterialRef& opaqueMaterial)noexcept
        : m_world(world)
        , m_mode(mode)
        , m_opaqueMaterial(opaqueMaterial)
    {}


public:
    [[nodiscard]] b8 create();
    void moveCutters();

private:
    using Box = SmokeBoxBounds;
    using EntityID = Core::ECS::EntityID;
    EntityID createBox(const Box& box, b8 transparent, const SmokeMeshRef& mesh = SmokeMeshRef{"project/meshes/cube_hard_edges"});
    void createReferencePieces(const Box& box, const Box& cutter, b8 transparent);
    void addCaster(const Box& box, const Box& cutterBox, u32 groupIndex, b8 transparent, b8 moving);
    void createAtlas();
    template<typename ParameterT>
    void addAnalyticCutter(u32 groupIndex, Name shapeType, const ParameterT& parameters, const SIMDMatrix& shapeToWorld);
    void createAnalyticAtlas();
    [[nodiscard]] b8 createOpenAtlas();

private:
    Core::ECS::World& m_world;
    const CsgShadowMode& m_mode;
    SmokeMaterialRef m_opaqueMaterial;
    Array<Float4, 2u> m_finalCutterCenters{};
    Array<EntityID, 2u> m_movingCutters{};
    u32 m_movingCount = 0u;
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

