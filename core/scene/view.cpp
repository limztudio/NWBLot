// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_DefaultSceneViewYaw = 0.82f;
static constexpr f32 s_DefaultSceneViewPitch = 0.94f;
static constexpr f32 s_DefaultSceneViewDepthOffset = 2.2f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneViewBasis BuildDefaultSceneViewBasis(){
    SIMDVector sinAngles;
    SIMDVector cosAngles;
    VectorSinCos(
        &sinAngles,
        &cosAngles,
        VectorSet(__hidden_scene::s_DefaultSceneViewYaw, __hidden_scene::s_DefaultSceneViewPitch, 0.0f, 0.0f)
    );
    const f32 sinYaw = VectorGetX(sinAngles);
    const f32 cosYaw = VectorGetX(cosAngles);
    const f32 sinPitch = VectorGetY(sinAngles);
    const f32 cosPitch = VectorGetY(cosAngles);

    SceneViewBasis basis;
    StoreFloat(VectorSet(cosYaw, 0.0f, sinYaw, 0.0f), &basis.right);
    StoreFloat(VectorSet(sinYaw * sinPitch, cosPitch, -cosYaw * sinPitch, 0.0f), &basis.up);
    StoreFloat(VectorSet(-sinYaw * cosPitch, sinPitch, cosYaw * cosPitch, 0.0f), &basis.forward);
    basis.positionDepthBias.w = __hidden_scene::s_DefaultSceneViewDepthOffset;
    return basis;
}

SceneViewBasis BuildSceneViewBasis(const TransformComponent& transform){
    SceneViewBasis basis;
    basis.positionDepthBias = transform.position;
    const SIMDVector rotation = LoadFloat(transform.rotation);
    StoreFloat(Vector3Rotate(s_SIMDIdentityR0, rotation), &basis.right);
    StoreFloat(Vector3Rotate(s_SIMDIdentityR1, rotation), &basis.up);
    StoreFloat(Vector3Rotate(s_SIMDIdentityR2, rotation), &basis.forward);
    return basis;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

