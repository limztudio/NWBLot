// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


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

    const SIMDVector right = VectorPermute<4, 3, 0, 3>(sinAngles, cosAngles);
    const SIMDVector upBase = VectorMultiply(
        VectorPermute<0, 5, 4, 3>(sinAngles, cosAngles),
        VectorSet(1.0f, 1.0f, -1.0f, 0.0f)
    );
    const SIMDVector upScale = VectorPermute<1, 4, 1, 3>(sinAngles, s_SIMDOne);
    const SIMDVector forwardBase = VectorMultiply(
        VectorPermute<0, 1, 4, 3>(sinAngles, cosAngles),
        VectorSet(-1.0f, 1.0f, 1.0f, 0.0f)
    );
    const SIMDVector forwardScale = VectorPermute<5, 6, 5, 3>(sinAngles, cosAngles);

    SceneViewBasis basis;
    StoreFloat(right, &basis.right);
    StoreFloat(VectorMultiply(upBase, upScale), &basis.up);
    StoreFloat(VectorMultiply(forwardBase, forwardScale), &basis.forward);
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


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

