// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "component.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessMode{
    enum Enum : u8{
        Read,
        Write
    };
};


using SystemTypeId = usize;


template<typename T>
inline SystemTypeId SystemType(){
    return __hidden_ecs::TypeIdGenerator::id<Decay_T<T>>();
}


struct ComponentAccess{
    ComponentTypeId typeId;
    AccessMode::Enum mode;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ISystem{
    friend class SystemScheduler;


public:
    virtual ~ISystem() = default;


public:
    virtual void update(World& world, f32 delta) = 0;


public:
protected:
    template<typename T>
    inline void readAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Read);
    }
    template<typename T>
    inline void writeAccess(){
        registerAccess(ComponentType<T>(), AccessMode::Write);
    }


private:
    void registerAccess(ComponentTypeId typeId, AccessMode::Enum mode);

    FixedVector<ComponentAccess, 16> m_access;
    bool m_enabled = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SystemScheduler{
private:
    using SystemAllocator = Alloc::CustomAllocator<ISystem*>;
    using Stage = Vector<ISystem*, SystemAllocator>;
    using StageAllocator = Alloc::CustomAllocator<Stage>;


public:
    explicit SystemScheduler(Alloc::CustomArena& arena);
    ~SystemScheduler();


public:
    void addSystem(ISystem& system);
    void removeSystem(ISystem& system);
    void rebuild();
    void execute(World& world, f32 delta);


private:
    Alloc::CustomArena& m_arena;

    // Each stage is a group of systems that can safely run in parallel
    Vector<Stage, StageAllocator> m_stages;
    Stage m_allSystems;
    bool m_dirty;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

