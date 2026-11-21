// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"
#include "type_id.h"

#include <core/task/cpu/scheduler.h>

#include <global/name.h>
#include <global/span.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DEFINE_SYSTEM_TYPE(systemTypeLiteral) \
    static constexpr ::AStringView s_SystemTypeText = systemTypeLiteral; \
    inline static constexpr ::Name s_SystemTypeName = ::Name(systemTypeLiteral); \
    [[nodiscard]] static constexpr const ::Name& SystemTypeName()noexcept{ \
        return s_SystemTypeName; \
    }


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessMode{
    enum Enum : u8{
        Read,
        Write
    };
};


struct ComponentAccess{
    ComponentTypeId typeId;
    AccessMode::Enum mode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ISystem : public NoCopy{
    friend class SystemScheduler;


public:
    explicit ISystem(::ArenaBase& arena)noexcept
        : m_access(arena)
    {}
    virtual ~ISystem()noexcept = default;


public:
    [[nodiscard]] virtual const Name& systemType()const noexcept{ return s_NameNone; }

    virtual void prepare([[maybe_unused]] World& world){}

    [[nodiscard]] virtual Expected<void, CpuTaskAdmissionError::Enum> update(World& world, f32 delta) = 0;

    [[nodiscard]] virtual CpuTaskOptions taskOptions()const noexcept{
        return { .cost = CpuTaskCost::Heavy };
    }

    [[nodiscard]] Span<const ComponentAccess> accesses()const noexcept{
        return Span<const ComponentAccess>(m_access.data(), m_access.size());
    }


protected:
    template<typename T>
    inline void readAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Read);
    }

    template<typename T>
    inline void writeAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Write);
    }

    void registerAccess(ComponentTypeId typeId, AccessMode::Enum mode);


private:
    Vector<ComponentAccess, ::ArenaBase> m_access;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SystemScheduler{
private:
    using SystemList = Vector<NotNull<ISystem*>, Alloc::GlobalArena>;
    using DependencyOffsetList = Vector<usize, Alloc::GlobalArena>;
    using DependencyIndexList = Vector<usize, Alloc::GlobalArena>;


public:
    explicit SystemScheduler(Alloc::GlobalArena& arena)noexcept
        : m_dependencyOffsets(arena)
        , m_dependencyIndices(arena)
        , m_allSystems(arena)
        , m_executionHandles(arena)
        , m_executionDependencies(arena)
        , m_executionResults(arena)
        , m_dirty(false)
    {}
    ~SystemScheduler()noexcept = default;


public:
    void addSystem(ISystem& system);
    void removeSystem(ISystem& system)noexcept;
    void clear()noexcept;
    void rebuild();
    [[nodiscard]] Expected<void, CpuTaskAdmissionError::Enum> execute(World& world, f32 delta);


private:
    DependencyOffsetList m_dependencyOffsets;
    DependencyIndexList m_dependencyIndices;
    SystemList m_allSystems;
    Vector<CpuTaskScheduler::TaskHandle, Alloc::GlobalArena> m_executionHandles;
    Vector<CpuTaskScheduler::TaskHandle, Alloc::GlobalArena> m_executionDependencies;
    Vector<Expected<void, CpuTaskAdmissionError::Enum>, Alloc::GlobalArena> m_executionResults;
    b8 m_dirty;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

