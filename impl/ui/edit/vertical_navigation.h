// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "commands.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace EditNavigationDirection{
    enum Enum : u8{ Up, Down, PageUp, PageDown };
};

struct EditNavigationSnapshot{
    u64 instanceGeneration = 0u;
    u64 generation = 0u;
    f32 preferredX = 0.0f;
    bool valid = false;
};

struct EditNavigationResult{
    usize committedByte = 0u;
    f32 preferredX = 0.0f;
    bool resolved = false;
};

// Caller-owned preferred-column intent is lent synchronously; identical accepted intentions still retire old snapshots.
class EditNavigationState final : NoCopy{
public:
    EditNavigationState();
    EditNavigationState(EditNavigationState&&) = delete;
    EditNavigationState& operator=(EditNavigationState&&) = delete;


public:
    [[nodiscard]] u64 instanceGeneration()const{ return m_instanceGeneration; }
    [[nodiscard]] u64 generation()const{ return m_generation; }
    [[nodiscard]] f32 preferredX()const{ return m_preferredX; }
    [[nodiscard]] bool hasPreferredX()const{ return m_valid; }
    [[nodiscard]] EditNavigationSnapshot snapshot()const;
    [[nodiscard]] bool matches(const EditNavigationSnapshot& snapshot)const;
    [[nodiscard]] bool setPreferredX(f32 preferredX);
    void reset();


private:
    void advanceGeneration();


private:
    const u64 m_instanceGeneration;
    u64 m_generation = 1u;
    f32 m_preferredX = 0.0f;
    bool m_valid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Resolve current committed text at the key's ordered event position. Return values; never retain the model or snapshot.
interface IEditNavigationResolver : private NoCopy{
public:
    virtual ~IEditNavigationResolver() = default;


public:
    [[nodiscard]] virtual EditNavigationResult resolve(const EditModel& model, EditNavigationDirection::Enum direction,
        const EditNavigationSnapshot& preferred, f32 viewportHeight) = 0;
};

[[nodiscard]] bool TranslateEditNavigation(const EditKeyStroke& stroke, EditNavigationDirection::Enum& output);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

