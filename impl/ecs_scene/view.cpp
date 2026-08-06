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

void BuildDefaultSceneViewBasisVectors(SIMDVector& outRight, SIMDVector& outUp, SIMDVector& outForward){
    SIMDVector sinAngles;
    SIMDVector cosAngles;
    VectorSinCos(
        &sinAngles,
        &cosAngles,
        VectorSet(s_DefaultSceneViewYaw, s_DefaultSceneViewPitch, 0.0f, 0.0f)
    );

    outRight = VectorPermute<4, 3, 0, 3>(sinAngles, cosAngles);
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
    outUp = VectorMultiply(upBase, upScale);
    outForward = VectorMultiply(forwardBase, forwardScale);
}

void BuildSceneViewBasisVectors(
    const SIMDVector rotation,
    SIMDVector& outRight,
    SIMDVector& outUp,
    SIMDVector& outForward
){
    outRight = Vector3Rotate(s_SIMDIdentityR0, rotation);
    outUp = Vector3Rotate(s_SIMDIdentityR1, rotation);
    outForward = Vector3Rotate(s_SIMDIdentityR2, rotation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneViewBasis BuildDefaultSceneViewBasis(){
    SceneViewBasis basis;
    __hidden_scene::BuildDefaultSceneViewBasisVectors(basis.right, basis.up, basis.forward);
    basis.positionDepthBias = VectorSet(0.0f, 0.0f, 0.0f, __hidden_scene::s_DefaultSceneViewDepthOffset);
    return basis;
}

SceneViewBasis BuildSceneViewBasis(const SIMDVector position, const SIMDVector rotation){
    SceneViewBasis basis;
    basis.positionDepthBias = position;
    __hidden_scene::BuildSceneViewBasisVectors(rotation, basis.right, basis.up, basis.forward);
    return basis;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

