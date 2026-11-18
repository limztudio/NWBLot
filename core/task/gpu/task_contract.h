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


// Member tasks use the same graph-owned payload and optional lifecycle thunks as static task contracts.
template<typename PayloadT>
struct MemberTask{
    using Payload = PayloadT;

    [[nodiscard]] static GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept(noexcept(payload.commandRequirements())) requires requires{ payload.commandRequirements(); }{
        return payload.commandRequirements();
    }
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept(noexcept(payload.record(commandList, context))) requires requires{ payload.record(commandList, context); }{
        return payload.record(commandList, context);
    }
    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept(noexcept(payload.accepted(token))) requires requires{ payload.accepted(token); }{
        payload.accepted(token);
    }
    static void Discarded(Payload& payload)noexcept(noexcept(payload.discarded())) requires requires{ payload.discarded(); }{
        payload.discarded();
    }
};


template<typename TaskT>
concept RecordApi = requires{
    static_cast<b8 (*)(const typename TaskT::Payload&, CommandList&, const GpuTaskRecordContext&)>(&TaskT::Record);
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

