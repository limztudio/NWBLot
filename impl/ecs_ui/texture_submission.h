// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextureUploadBatch final{
private:
    struct Request{
        ImTextureData* textureData = nullptr;
        ImTextureStatus expectedStatus = ImTextureStatus_Destroyed;
        bool* initialUploadAccepted = nullptr;
    };
    using RequestVector = Vector<Request, Core::Alloc::GlobalArena>;


public:
    explicit UiTextureUploadBatch(Core::Alloc::GlobalArena& arena)
        : m_requests(arena)
    {}


public:
    void reset(){ m_requests.clear(); }
    [[nodiscard]] bool empty()const{ return m_requests.empty(); }

    void add(ImTextureData& textureData, bool* const initialUploadAccepted = nullptr){
        Request request;
        request.textureData = &textureData;
        request.expectedStatus = textureData.Status;
        request.initialUploadAccepted = initialUploadAccepted;
        m_requests.push_back(request);
    }

    void complete(const bool submitted){
        if(submitted){
            for(const Request& request : m_requests){
                if(!request.textureData || request.textureData->Status != request.expectedStatus)
                    continue;

                switch(request.expectedStatus){
                case ImTextureStatus_WantCreate:
                    if(request.initialUploadAccepted)
                        *request.initialUploadAccepted = true;
                    request.textureData->SetStatus(ImTextureStatus_OK);
                    break;
                case ImTextureStatus_WantUpdates:
                    request.textureData->SetStatus(ImTextureStatus_OK);
                    break;
                case ImTextureStatus_OK:
                case ImTextureStatus_WantDestroy:
                case ImTextureStatus_Destroyed:
                default:
                    break;
                }
            }
        }

        reset();
    }


private:
    RequestVector m_requests;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

