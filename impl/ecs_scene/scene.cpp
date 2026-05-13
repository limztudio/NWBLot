// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scene.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_DefaultSceneViewYaw = 0.82f;
static constexpr f32 s_DefaultSceneViewPitch = 0.94f;
static constexpr f32 s_DefaultSceneViewDepthOffset = 2.2f;

void StoreRotatedSceneViewBasisVector(Float4& outVector, const Float4& localVector, const SIMDVector rotation){
    StoreFloat(Vector3Rotate(LoadFloat(localVector), rotation), &outVector);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneViewBasis BuildDefaultSceneViewBasis(){
    SIMDVector sinAngles;
    SIMDVector cosAngles;
    VectorSinCos(
        &sinAngles,
        &cosAngles,
        VectorSet(__hidden_ecs_scene::s_DefaultSceneViewYaw, __hidden_ecs_scene::s_DefaultSceneViewPitch, 0.0f, 0.0f)
    );
    const f32 sinYaw = VectorGetX(sinAngles);
    const f32 cosYaw = VectorGetX(cosAngles);
    const f32 sinPitch = VectorGetY(sinAngles);
    const f32 cosPitch = VectorGetY(cosAngles);

    SceneViewBasis basis;
    basis.right = Float4(cosYaw, 0.0f, sinYaw, 0.0f);
    basis.up = Float4(sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch, 0.0f);
    basis.forward = Float4(-sinYaw * cosPitch, sinPitch, cosYaw * cosPitch, 0.0f);
    basis.positionDepthBias.w = __hidden_ecs_scene::s_DefaultSceneViewDepthOffset;
    return basis;
}

SceneViewBasis BuildSceneViewBasis(const TransformComponent& transform){
    SceneViewBasis basis;
    basis.positionDepthBias = transform.position;
    const SIMDVector rotation = LoadFloat(transform.rotation);
    __hidden_ecs_scene::StoreRotatedSceneViewBasisVector(basis.right, Float4(1.0f, 0.0f, 0.0f), rotation);
    __hidden_ecs_scene::StoreRotatedSceneViewBasisVector(basis.up, Float4(0.0f, 1.0f, 0.0f), rotation);
    __hidden_ecs_scene::StoreRotatedSceneViewBasisVector(basis.forward, Float4(0.0f, 0.0f, 1.0f), rotation);
    return basis;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

