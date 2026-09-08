// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "arena_names.h"
#include "world.h"

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename AccessContainer>
[[nodiscard]] bool systemsConflict(const AccessContainer& predecessorAccesses, const AccessContainer& systemAccesses){
    for(const ComponentAccess& predecessor : predecessorAccesses){
        for(const ComponentAccess& access : systemAccesses){
            if(predecessor.typeId == access.typeId && (predecessor.mode == AccessMode::Write || access.mode == AccessMode::Write))
                return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


ISystem::ISystem(Alloc::GlobalArena& arena)
    : m_access(arena)
{}


void ISystem::registerAccess(ComponentTypeId typeId, AccessMode::Enum mode){
    for(auto& access : m_access){
        if(access.typeId != typeId)
            continue;

        // Write is stronger than read; keep the strongest access mode.
        if(mode == AccessMode::Write)
            access.mode = AccessMode::Write;
        return;
    }

    m_access.push_back(ComponentAccess{ typeId, mode });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SystemScheduler::SystemScheduler(Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_dependencies(arena)
    , m_allSystems(arena)
    , m_dirty(false)
{}


void SystemScheduler::addSystem(ISystem& system){
    if(FindIf(m_allSystems.begin(), m_allSystems.end(), [&system](ISystem* iterSystem){ return iterSystem == &system; }) != m_allSystems.end())
        return;

    m_allSystems.push_back(&system);
    m_dirty = true;
}


void SystemScheduler::removeSystem(ISystem& system){
    auto itr = FindIf(
        m_allSystems.begin(),
        m_allSystems.end(),
        [&system](ISystem* iterSystem){ return iterSystem == &system; }
    );
    if(itr != m_allSystems.end()){
        m_allSystems.erase(itr);
        m_dirty = true;
    }
}


void SystemScheduler::clear(){
    m_allSystems.clear();
    m_dependencies.clear();
    m_dirty = false;
}


void SystemScheduler::rebuild(){
    m_dependencies.clear();
    const usize systemCount = m_allSystems.size();
    m_dependencies.reserve(systemCount);

    for(usize systemIndex = 0u; systemIndex < systemCount; ++systemIndex){
        DependencyList predecessors(m_arena);
        for(usize predecessorIndex = 0u; predecessorIndex < systemIndex; ++predecessorIndex){
            if(__hidden_system::systemsConflict(m_allSystems[predecessorIndex]->m_access, m_allSystems[systemIndex]->m_access))
                predecessors.push_back(predecessorIndex);
        }
        m_dependencies.push_back(Move(predecessors));
    }

    m_dirty = false;
}


void SystemScheduler::execute(World& world, f32 delta){
    if(m_dirty)
        rebuild();

    // Preparation can change entity/component storage, so it stays on the caller before any update starts.
    for(ISystem* system : m_allSystems)
        system->prepare(world);

    if(m_allSystems.empty())
        return;

    using TaskHandle = Alloc::CpuTaskScheduler::TaskHandle;
    Alloc::ScratchArena scratchArena(EcsArenaScope::s_SchedulerExecutionScratch);
    Vector<TaskHandle, Alloc::ScratchArena> handles(m_allSystems.size(), TaskHandle{}, scratchArena);
    Vector<TaskHandle, Alloc::ScratchArena> dependencies(scratchArena);
    dependencies.reserve(m_allSystems.size());
    Alloc::CpuTaskScope& tasks = world.taskScope();

    // Keep world/system data alive if publishing a later node throws after earlier work has started.
    ScopeExit drainSubmitted([&]()noexcept{ tasks.drain(); });
    for(usize systemIndex = 0u; systemIndex < m_allSystems.size(); ++systemIndex){
        dependencies.clear();
        for(const usize predecessor : m_dependencies[systemIndex])
            dependencies.push_back(handles[predecessor]);

        ISystem* const system = m_allSystems[systemIndex];
        handles[systemIndex] = tasks.submit([system, &world, delta](){
            system->update(world, delta);
        }, system->taskOptions(), dependencies.data(), dependencies.size());
        if(!handles[systemIndex].valid())
            throw RuntimeException("CPU task scheduler rejected an ECS system update");
    }
    tasks.wait();
    drainSubmitted.release();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

