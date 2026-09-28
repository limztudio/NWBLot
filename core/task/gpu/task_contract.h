// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"

#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuGraphTaskContract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TaskT>
concept RecordApi = requires{
    static_cast<bool (*)(const typename TaskT::Payload&, CommandList&, const GpuTaskRecordContext&)>(&TaskT::record);
};

template<typename TaskT>
concept AcceptedApi = requires{
    static_cast<void (*)(typename TaskT::Payload&, const QueueSubmissionToken&)>(&TaskT::accepted);
};

template<typename TaskT>
concept DiscardedApi = requires{
    static_cast<void (*)(typename TaskT::Payload&)>(&TaskT::discarded);
};

// Payload-dependent contracts are evaluated once at declaration, before immutable graph compilation.
// They describe commands and external timeline requirements, independently of physical placement.
template<typename TaskT>
[[nodiscard]] GpuTaskCommandRequirements CommandRequirements(const typename TaskT::Payload& payload){
    if constexpr(requires{ static_cast<GpuTaskCommandRequirements (*)(const typename TaskT::Payload&)>(&TaskT::commandRequirements); })
        return TaskT::commandRequirements(payload);
    else if constexpr(requires{ GpuTaskCommandRequirements{ TaskT::s_CommandRequirements }; })
        return TaskT::s_CommandRequirements;
    else{
        static_assert(!RecordApi<TaskT>, "A GPU recording task must declare its command requirements in the task implementation");
        return {};
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

