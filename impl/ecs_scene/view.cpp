// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "view.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_scene_view{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////







////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void BuildDefaultSceneViewBasisVectors(SIMDVectorSingle& outRight, SIMDVectorSingle& outUp, SIMDVectorSingle& outForward)noexcept{
    SIMDVectorSingle sinAngles;
    SIMDVectorSingle cosAngles;
    VectorSinCos(sinAngles, cosAngles, VectorSet(s_DefaultSceneViewYaw, s_DefaultSceneViewPitch, 0.0f, 0.0f));

    outRight = VectorPermute<4, 3, 0, 3>(sinAngles, cosAngles);
    const SIMDVectorSingle upBase = VectorMultiply(VectorPermute<0, 5, 4, 3>(sinAngles, cosAngles), VectorSet(1.0f, 1.0f, -1.0f, 0.0f));
    const SIMDVectorSingle upScale = VectorPermute<1, 4, 1, 3>(sinAngles, s_SIMDOne);
    const SIMDVectorSingle forwardBase = VectorMultiply(VectorPermute<0, 1, 4, 3>(sinAngles, cosAngles), VectorSet(-1.0f, 1.0f, 1.0f, 0.0f));
    const SIMDVectorSingle forwardScale = VectorPermute<5, 6, 5, 3>(sinAngles, cosAngles);
    outUp = VectorMultiply(upBase, upScale);
    outForward = VectorMultiply(forwardBase, forwardScale);
}

void BuildSceneViewBasisVectors(const SIMDVectorSingle rotation, SIMDVectorSingle& outRight, SIMDVectorSingle& outUp, SIMDVectorSingle& outForward)noexcept{
    outRight = Vector3Rotate(s_SIMDIdentityR0, rotation);
    outUp = Vector3Rotate(s_SIMDIdentityR1, rotation);
    outForward = Vector3Rotate(s_SIMDIdentityR2, rotation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDMatrix BuildDefaultSceneViewBasis()noexcept{
    SIMDVectorSingle right;
    SIMDVectorSingle up;
    SIMDVectorSingle forward;
    __hidden_scene_view::BuildDefaultSceneViewBasisVectors(right, up, forward);
    return SIMDMatrix{ .v = { right, up, forward, VectorSet(0.0f, 0.0f, 0.0f, s_DefaultSceneViewDepthOffset) } };
}

SIMDMatrix BuildSceneViewBasis(const SIMDVectorSingle position, const SIMDVectorSingle rotation)noexcept{
    SIMDVectorSingle right;
    SIMDVectorSingle up;
    SIMDVectorSingle forward;
    __hidden_scene_view::BuildSceneViewBasisVectors(rotation, right, up, forward);
    return SIMDMatrix{ .v = { right, up, forward, position } };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_SCENE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

