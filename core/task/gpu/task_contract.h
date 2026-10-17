// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_desc.h"

#include <global/type.h>
#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuGraphTaskContract{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TaskT>
concept RecordApi = requires{
    static_cast<bool (*)(const typename TaskT::Payload&, CommandList&, const GpuTaskRecordContext&)>(&TaskT::Record);
};

template<typename TaskT>
concept AcceptedApi = requires{
    static_cast<void (*)(typename TaskT::Payload&, const QueueSubmissionToken&)>(&TaskT::Accepted);
};

template<typename TaskT>
concept DiscardedApi = requires{
    static_cast<void (*)(typename TaskT::Payload&)>(&TaskT::Discarded);
};

// Payload-dependent contracts are evaluated once at declaration, before immutable graph compilation.
// They describe commands and external timeline requirements, independently of physical placement.
template<typename TaskT>
[[nodiscard]] GpuTaskCommandRequirements CommandRequirements(const typename TaskT::Payload& payload)noexcept([]()noexcept{
    if constexpr(requires{ static_cast<GpuTaskCommandRequirements (*)(const typename TaskT::Payload&)>(&TaskT::CommandRequirements); })
        return noexcept(TaskT::CommandRequirements(DeclVal<const typename TaskT::Payload&>()));
    else if constexpr(requires{ GpuTaskCommandRequirements{ TaskT::s_CommandRequirements }; })
        return noexcept([](GpuTaskCommandRequirements)noexcept{}(TaskT::s_CommandRequirements));
    else
        return true;
}()){
    if constexpr(requires{ static_cast<GpuTaskCommandRequirements (*)(const typename TaskT::Payload&)>(&TaskT::CommandRequirements); })
        return TaskT::CommandRequirements(payload);
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

