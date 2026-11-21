// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "world.h"

#include <core/alloc/scratch.h>

#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] b8 SystemsConflict(const Span<const ComponentAccess> predecessorAccesses, const Span<const ComponentAccess> systemAccesses)noexcept{
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


void ISystem::registerAccess(ComponentTypeId typeId, AccessMode::Enum mode){
    for(auto& access : m_access){
        if(access.typeId != typeId)
            continue;

        if(mode == AccessMode::Write)
            access.mode = AccessMode::Write;
        return;
    }

    m_access.push_back(ComponentAccess{ typeId, mode });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void SystemScheduler::addSystem(ISystem& system){
    if(FindIf(m_allSystems.begin(), m_allSystems.end(), [&system](NotNull<ISystem*> iterSystem)noexcept{ return iterSystem.get() == &system; }) != m_allSystems.end())
        return;

    m_allSystems.push_back(MakeNotNull(&system));
    m_dirty = true;
}


void SystemScheduler::removeSystem(ISystem& system)noexcept{
    auto itr = FindIf(m_allSystems.begin(), m_allSystems.end(), [&system](NotNull<ISystem*> iterSystem)noexcept{ return iterSystem.get() == &system; });
    if(itr != m_allSystems.end()){
        m_allSystems.erase(itr);
        m_dirty = true;
    }
}


void SystemScheduler::clear()noexcept{
    m_allSystems.clear();
    m_dependencyOffsets.clear();
    m_dependencyIndices.clear();
    m_executionHandles.clear();
    m_executionDependencies.clear();
    m_executionResults.clear();
    m_dirty = false;
}


void SystemScheduler::rebuild(){
    m_dependencyOffsets.clear();
    m_dependencyIndices.clear();
    const usize systemCount = m_allSystems.size();
    if(systemCount == 0u){
        m_dirty = false;
        return;
    }

    m_dependencyOffsets.reserve(systemCount + 1u);
    m_dependencyOffsets.push_back(0u);

    Alloc::ScratchArena scratchArena;
    constexpr usize s_BitsPerWord = 64u;
    const usize wordsPerSystem = (systemCount + s_BitsPerWord - 1u) / s_BitsPerWord;
    Vector<u64, Alloc::ScratchArena> reachableAncestors(systemCount * wordsPerSystem, 0u, scratchArena);
    for(usize systemIndex = 0u; systemIndex < systemCount; ++systemIndex){
        const usize rangeBegin = m_dependencyIndices.size();
        u64* const systemAncestors = reachableAncestors.data() + systemIndex * wordsPerSystem;
        for(usize predecessorIndex = systemIndex; predecessorIndex-- > 0u;){
            const usize wordIndex = predecessorIndex / s_BitsPerWord;
            const u64 bitMask = static_cast<u64>(1u) << (predecessorIndex % s_BitsPerWord);
            if((systemAncestors[wordIndex] & bitMask) != 0u)
                continue;
            if(!__hidden_ecs_system::SystemsConflict(m_allSystems[predecessorIndex]->m_access, m_allSystems[systemIndex]->m_access))
                continue;

            m_dependencyIndices.push_back(predecessorIndex);
            systemAncestors[wordIndex] |= bitMask;
            const u64* const predAncestors = reachableAncestors.data() + predecessorIndex * wordsPerSystem;
            for(usize word = 0u; word <= wordIndex; ++word)
                systemAncestors[word] |= predAncestors[word];
        }
        const usize rangeEnd = m_dependencyIndices.size();
        for(usize lo = rangeBegin, hi = rangeEnd; lo + 1u < hi; ++lo, --hi)
            Swap(m_dependencyIndices[lo], m_dependencyIndices[hi - 1u]);
        m_dependencyOffsets.push_back(rangeEnd);
    }

    m_dirty = false;
}


Expected<void, CpuTaskAdmissionError::Enum> SystemScheduler::execute(World& world, f32 delta){
    if(m_dirty)
        rebuild();

    for(const auto system : m_allSystems)
        system->prepare(world);

    if(m_allSystems.empty())
        return {};

    using TaskHandle = CpuTaskScheduler::TaskHandle;
    m_executionHandles.clear();
    m_executionHandles.resize(m_allSystems.size(), TaskHandle{});
    m_executionDependencies.clear();
    m_executionDependencies.reserve(m_allSystems.size());
    m_executionResults.clear();
    m_executionResults.resize(m_allSystems.size());
    Vector<TaskHandle, Alloc::GlobalArena>& handles = m_executionHandles;
    Vector<TaskHandle, Alloc::GlobalArena>& dependencies = m_executionDependencies;
    CpuTaskScope& tasks = world.taskScope();

    ScopeExit drainSubmitted([&]()noexcept{ tasks.drain(); });
    const usize systemCount = m_allSystems.size();
    for(usize systemIndex = 0u; systemIndex < systemCount; ++systemIndex){
        const usize depBegin = m_dependencyOffsets[systemIndex];
        const usize depEnd = m_dependencyOffsets[systemIndex + 1u];
        dependencies.clear();
        for(usize depIndex = depBegin; depIndex < depEnd; ++depIndex)
            dependencies.push_back(handles[m_dependencyIndices[depIndex]]);

        const auto system = m_allSystems[systemIndex];
        const auto admitted = tasks.submit([this, system, systemIndex, depBegin, depEnd, &world, delta](){
            for(usize depIndex = depBegin; depIndex < depEnd; ++depIndex){
                const usize predecessor = m_dependencyIndices[depIndex];
                if(!m_executionResults[predecessor]){
                    m_executionResults[systemIndex] = MakeUnexpected(m_executionResults[predecessor].error());
                    return;
                }
            }
            m_executionResults[systemIndex] = system->update(world, delta);
        }, system->taskOptions(), dependencies.data(), dependencies.size()
        );
        if(!admitted){
            tasks.wait();
            drainSubmitted.release();
            return MakeUnexpected(admitted.error());
        }
        handles[systemIndex] = *admitted;
    }
    tasks.waitCooperative();
    drainSubmitted.release();
    for(const auto& completed : m_executionResults){
        if(!completed)
            return MakeUnexpected(completed.error());
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

