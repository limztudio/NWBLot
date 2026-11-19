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


template<typename PayloadT>
struct MemberTask{
    using Payload = PayloadT;

    [[nodiscard]] static GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept(noexcept(payload.commandRequirements())) requires requires{ static_cast<GpuTaskCommandRequirements (Payload::*)()const>(&Payload::commandRequirements); }{
        return payload.commandRequirements();
    }
    [[nodiscard]] static b8 Record(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context)noexcept(noexcept(payload.record(commandList, context))) requires requires{ static_cast<b8 (Payload::*)(CommandList&, const GpuTaskRecordContext&)const>(&Payload::record); }{
        return payload.record(commandList, context);
    }
    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept(noexcept(payload.accepted(token))) requires (requires{ static_cast<void (Payload::*)(const QueueSubmissionToken&)>(&Payload::accepted); } || requires{ static_cast<void (Payload::*)(QueueSubmissionToken)>(&Payload::accepted); }){
        payload.accepted(token);
    }
    static void Discarded(Payload& payload)noexcept(noexcept(payload.discarded())) requires requires{ static_cast<void (Payload::*)()>(&Payload::discarded); }{
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

