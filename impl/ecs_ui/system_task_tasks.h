// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UiSystem::TaskGraphRenderTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::AcquiredPresentationFrame frame;
        Core::GpuGraphResourceId backbuffer;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        return payload.ui && payload.ui->recordTaskGraphPresentation(
            commandList,
            payload.frame,
            payload.backbuffer,
            context
        );
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->confirmTaskGraphPresentationSubmission();
    }
};


struct UiSystem::TaskGraphUploadCompletionTask{
    struct Payload{
        UiSystem* ui = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.ui && payload.ui->recordTaskGraphUploadCompletion();
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->confirmTaskGraphPresentationSubmission();
    }
};


struct UiSystem::StandaloneTextureUploadCompletionTask{
    struct Payload{
        UiSystem* ui = nullptr;
        bool uploadsPrepared = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(commandList);
        static_cast<void>(context);
        return payload.ui && payload.uploadsPrepared;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->m_textureUploadBatch.complete(true);
    }

    static void discarded(Payload& payload){
        if(payload.ui)
            payload.ui->m_textureUploadBatch.complete(false);
    }
};


// Opaque callbacks cannot enter the immutable packet; record them synchronously.
struct UiSystem::StandaloneLegacyPresentationTask{
    struct Payload{
        UiSystem* ui = nullptr;
        Core::AcquiredPresentationFrame frame;
        Core::GpuGraphResourceId backbuffer;
        ImDrawData* drawData = nullptr;
        u64 frameGeneration = 0u;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        return payload.ui && payload.ui->recordStandaloneLegacyTaskGraphPresentation(
            commandList,
            payload.frame,
            payload.backbuffer,
            payload.drawData,
            payload.frameGeneration,
            context
        );
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.ui && token.valid())
            payload.ui->confirmTaskGraphPresentationSubmission();
    }

    static void discarded(Payload& payload){
        if(payload.ui)
            payload.ui->discardStandaloneLegacyTaskGraphPresentation();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
