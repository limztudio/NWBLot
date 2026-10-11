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

void BuildDefaultSceneViewBasisVectors(SIMDVectorSingle& outRight, SIMDVectorSingle& outUp, SIMDVectorSingle& outForward)noexcept{
    SIMDVectorSingle sinAngles;
    SIMDVectorSingle cosAngles;
    VectorSinCos(
        sinAngles,
        cosAngles,
        VectorSet(s_DefaultSceneViewYaw, s_DefaultSceneViewPitch, 0.0f, 0.0f)
    );

    outRight = VectorPermute<4, 3, 0, 3>(sinAngles, cosAngles);
    const SIMDVectorSingle upBase = VectorMultiply(
        VectorPermute<0, 5, 4, 3>(sinAngles, cosAngles),
        VectorSet(1.0f, 1.0f, -1.0f, 0.0f)
    );
    const SIMDVectorSingle upScale = VectorPermute<1, 4, 1, 3>(sinAngles, s_SIMDOne);
    const SIMDVectorSingle forwardBase = VectorMultiply(
        VectorPermute<0, 1, 4, 3>(sinAngles, cosAngles),
        VectorSet(-1.0f, 1.0f, 1.0f, 0.0f)
    );
    const SIMDVectorSingle forwardScale = VectorPermute<5, 6, 5, 3>(sinAngles, cosAngles);
    outUp = VectorMultiply(upBase, upScale);
    outForward = VectorMultiply(forwardBase, forwardScale);
}

void BuildSceneViewBasisVectors(
    const SIMDVectorSingle rotation,
    SIMDVectorSingle& outRight,
    SIMDVectorSingle& outUp,
    SIMDVectorSingle& outForward
)noexcept{
    outRight = Vector3Rotate(s_SIMDIdentityR0, rotation);
    outUp = Vector3Rotate(s_SIMDIdentityR1, rotation);
    outForward = Vector3Rotate(s_SIMDIdentityR2, rotation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneViewBasis BuildDefaultSceneViewBasis()noexcept{
    SceneViewBasis basis;
    SIMDVectorSingle right;
    SIMDVectorSingle up;
    SIMDVectorSingle forward;
    __hidden_scene::BuildDefaultSceneViewBasisVectors(right, up, forward);
    StoreFloat(right, basis.right);
    StoreFloat(up, basis.up);
    StoreFloat(forward, basis.forward);
    StoreFloat(VectorSet(0.0f, 0.0f, 0.0f, __hidden_scene::s_DefaultSceneViewDepthOffset), basis.positionDepthBias);
    return basis;
}

SceneViewBasis BuildSceneViewBasis(const SIMDVectorSingle position, const SIMDVectorSingle rotation)noexcept{
    SceneViewBasis basis;
    SIMDVectorSingle right;
    SIMDVectorSingle up;
    SIMDVectorSingle forward;
    __hidden_scene::BuildSceneViewBasisVectors(rotation, right, up, forward);
    StoreFloat(right, basis.right);
    StoreFloat(up, basis.up);
    StoreFloat(forward, basis.forward);
    StoreFloat(position, basis.positionDepthBias);
    return basis;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

