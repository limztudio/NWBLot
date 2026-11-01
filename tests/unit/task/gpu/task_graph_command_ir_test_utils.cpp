// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_command_ir_test_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TaskGraphTestUtils{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 CaptureAllBuiltinCommandIrRecords(Core::GpuCommandIrCapture& capture){
    Core::TextureSlice sourceSlice;
    sourceSlice.setOrigin(1u, 2u, 3u).setSize(4u, 5u, 6u).setMipLevel(7u).setArraySlice(8u);
    Core::TextureSlice destinationSlice;
    destinationSlice.setOrigin(9u, 10u, 11u).setSize(12u, 13u, 14u).setMipLevel(15u).setArraySlice(16u);
    Core::GpuClearTextureTaskDesc clearTexture;
    clearTexture.destination = s_CommandIrDestination;
    clearTexture.subresources = Core::TextureSubresourceSet(2u, 3u, 4u, 5u);
    clearTexture.valueType = Core::GpuClearTextureTaskValueType::DepthStencil;
    clearTexture.floatValue = Core::Color(0.25f, 0.5f, 0.75f, 1.f);
    clearTexture.uintValue = Core::UIntColor(2u, 3u, 5u, 7u);
    clearTexture.intValue = Core::IntColor(-2, -3, -5, -7);
    clearTexture.depthValue = 0.125f;
    clearTexture.stencilValue = 19u;
    clearTexture.clearDepth = true;
    clearTexture.clearStencil = true;

    return
        capture.captureCopyBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrSource, 16u, s_CommandIrDestination, 32u, 64u)
        && capture.captureCopyTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrSource, sourceSlice, s_CommandIrDestination, destinationSlice)
        && capture.captureClearBuffer(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, 0xdecafbadU)
        && capture.captureClearTexture(s_CommandIrTask, s_CommandIrPacket, s_CommandIrQueue, s_CommandIrDestination, clearTexture)
    ;
}

void CopyCommandIrBytes(const BinaryByteView source, Core::GraphicsBytes& outBytes){
    outBytes.resize(source.size());
    if(!source.empty())
        NWB_MEMCPY(outBytes.data(), outBytes.size(), source.data(), source.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

