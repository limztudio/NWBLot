// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "input/router.h"
#include "state/store.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// One CPU owner coordinates declarations, input and the exact matching frozen frame's layout publication.
class Context final : NoCopy{
private:
    struct OwnedTarget{
        HitTarget target;
        WidgetRoot root;
    };

    [[nodiscard]] static bool ContainsRoot(const WidgetRoot* roots, usize count, const WidgetRoot& root);


public:
    explicit Context(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] bool beginFrame(u64 generation);
    [[nodiscard]] bool beginRoot(const WidgetRoot& root);
    [[nodiscard]] bool endRoot();
    [[nodiscard]] bool pushScope(AStringView stableKey);
    [[nodiscard]] bool popScope();
    [[nodiscard]] WidgetState* declare(AStringView stableKey, WidgetKind::Enum kind);
    [[nodiscard]] bool addTarget(const WidgetState& state, HitTarget target);
    [[nodiscard]] bool takeActivation(const WidgetState& state, bool enabled);
    [[nodiscard]] bool takePointerGesture(const WidgetState& state, bool enabled, PointerGesture& gesture);
    [[nodiscard]] bool finishFrame();
    // Only the host's exact accepted and successfully presented generation may publish its prepared hit layout.
    [[nodiscard]] bool commitFrame(u64 generation);
    void abandonFrame();
    void resetInput();
    // Retire removed/hidden host roots even while a GPU frame is pending.
    void retainRoots(const WidgetRoot* roots, usize count);
    void fail(){ m_failed = true; }
    [[nodiscard]] bool failed()const{ return m_failed; }
    [[nodiscard]] bool ready()const{ return m_readyGeneration != 0u; }
    [[nodiscard]] u64 readyGeneration()const{ return m_readyGeneration; }
    [[nodiscard]] InputRouter& input(){ return m_input; }
    [[nodiscard]] const InputRouter& input()const{ return m_input; }
    [[nodiscard]] const WidgetStateStore& states()const{ return m_states; }


private:
    [[nodiscard]] bool currentDeclaration(const WidgetState& state)const;


private:
    InputRouter m_input;
    WidgetStateStore m_states;
    PaintVector<WidgetId> m_scopes;
    PaintVector<OwnedTarget> m_targets;
    InputVector<HitTarget> m_commitTargets;
    WidgetRoot m_root;
    u64 m_frameGeneration = 0u;
    u64 m_lastFrameGeneration = 0u;
    u64 m_readyGeneration = 0u;
    bool m_rootActive = false;
    bool m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

