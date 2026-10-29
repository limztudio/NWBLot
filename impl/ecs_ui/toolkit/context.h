// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "input/router.h"
#include "state/store.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Builder containers and context scopes nest together, so one depth limit bounds both.
inline constexpr usize s_ContextMaxScopeDepth = 64u;

// One CPU owner coordinates declarations, input and the exact matching frozen frame's layout publication.
class Context final : NoCopy{
private:
    struct OwnedTarget{
        HitTarget target;
        WidgetRoot root;
    };

    struct OwnedPopup{
        PopupScope scope;
        WidgetRoot root;
    };

    struct StateClaim{
        u64 instanceGeneration = 0u;
        WidgetKind::Enum kind = WidgetKind::Panel;


    public:
        [[nodiscard]] b8 operator==(const StateClaim&)const noexcept = default;
    };

    struct StateClaimHash{
        [[nodiscard]] usize operator()(const StateClaim& claim)const noexcept;
    };


private:
    using StateClaims = HashSet<StateClaim, Core::Alloc::GlobalArena, StateClaimHash, EqualTo<StateClaim>>;


private:
    static constexpr usize s_SmallStateClaims = 32u;

    [[nodiscard]] static b8 ContainsRoot(const WidgetRoot* roots, usize count, const WidgetRoot& root)noexcept;


public:
    explicit Context(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] b8 beginFrame(u64 generation)noexcept;
    [[nodiscard]] b8 beginRoot(const WidgetRoot& root);
    [[nodiscard]] b8 endRoot()noexcept;
    [[nodiscard]] b8 pushScope(AStringView stableKey);
    [[nodiscard]] b8 popScope()noexcept;
    [[nodiscard]] Expected<NotNull<WidgetState*>> declare(AStringView stableKey, WidgetKind::Enum kind);
    // Compound controls declare internal scopes from their owning widget instead of borrowing the current layout scope.
    [[nodiscard]] Expected<NotNull<WidgetState*>> declarePart(const WidgetState& owner, AStringView stableKey, WidgetKind::Enum kind);
    // A state instance may own only one control of a given kind in a frame, even across balanced roots/scopes.
    [[nodiscard]] b8 claimState(const WidgetState& owner, u64 instanceGeneration);
    [[nodiscard]] b8 addTarget(const WidgetState& state, HitTarget target);
    [[nodiscard]] b8 addPartTarget(const WidgetState& owner, WidgetId part, HitTarget target);
    [[nodiscard]] b8 takeActivation(const WidgetState& state, b8 enabled);
    [[nodiscard]] Expected<PointerGesture> takePointerGesture(const WidgetState& state, b8 enabled);
    [[nodiscard]] Expected<ControlAction> takeControlAction(const WidgetState& state, b8 enabled, const ControlToken& token);
    [[nodiscard]] Expected<PointerGesture> takePartPointerGesture(const WidgetState& owner, WidgetId part, b8 enabled);
    // Registration reserves the declaration-order layer; activation may be repeated during deferred painting.
    [[nodiscard]] b8 registerPopupScope(const WidgetState& state, PopupScope scope);
    [[nodiscard]] b8 activatePopupScope(const PopupToken& token)noexcept;
    [[nodiscard]] b8 updatePopupScope(const PopupToken& token, PopupScope scope)noexcept;
    [[nodiscard]] b8 beginPopupScope(const WidgetState& state, PopupScope scope);
    [[nodiscard]] b8 endPopupScope(b8 visible)noexcept;
    // Discard an inactive scope and its descendants without disturbing the current parent activation.
    void discardPopupScope(const PopupToken& token)noexcept;
    [[nodiscard]] b8 hasPopupScope(const PopupToken& token)const noexcept;
    [[nodiscard]] WidgetId scopeId()const noexcept{ return m_scopes.empty() ? WidgetId{} : m_scopes.back(); }

    [[nodiscard]] PopupToken popupToken()const noexcept{ return m_currentPopup; }

    [[nodiscard]] u32 popupLayer()const noexcept{ return m_popupLayer; }
    [[nodiscard]] u32 popupLayer(const PopupToken& token)const noexcept;
    [[nodiscard]] PopupToken topPopupToken()const noexcept{ return m_popups.empty() ? PopupToken{} : m_popups.back().scope.token; }
    [[nodiscard]] b8 finishFrame();
    // Only the host's exact accepted and successfully presented generation may publish its prepared hit layout.
    [[nodiscard]] b8 commitFrame(u64 generation);
    void abandonFrame();
    void resetInput()noexcept;
    // Retire removed/hidden host roots even while a GPU frame is pending.
    void retainRoots(const WidgetRoot* roots, usize count);
    void fail()noexcept{ m_failed = true; }

    [[nodiscard]] b8 failed()const noexcept{ return m_failed; }

    [[nodiscard]] b8 ready()const noexcept{ return m_readyGeneration != 0u; }

    [[nodiscard]] u64 readyGeneration()const noexcept{ return m_readyGeneration; }

    [[nodiscard]] InputRouter& input()noexcept{ return m_input; }

    [[nodiscard]] const InputRouter& input()const noexcept{ return m_input; }

    [[nodiscard]] const WidgetStateStore& states()const noexcept{ return m_states; }


private:
    [[nodiscard]] Expected<NotNull<WidgetState*>> declareId(WidgetId id, WidgetKind::Enum kind);
    [[nodiscard]] b8 currentDeclaration(const WidgetState& state)const noexcept;
    void retireUnseenStates();


private:
    InputRouter m_input;
    WidgetStateStore m_states;
    PaintVector<WidgetId> m_scopes;
    PaintVector<OwnedTarget> m_targets;
    PaintVector<OwnedPopup> m_popups;
    Array<StateClaim, s_SmallStateClaims> m_smallStateClaims{};
    StateClaims m_stateClaims;
    InputVector<HitTarget> m_commitTargets;
    InputVector<PopupScope> m_commitPopups;
    PopupToken m_currentPopup;
    Array<PopupToken, s_InputMaxPopups> m_popupStack{};
    usize m_popupDepth = 0u;
    usize m_declarationCount = 0u;
    usize m_stateClaimCount = 0u;
    WidgetRoot m_root;
    u64 m_frameGeneration = 0u;
    u64 m_focusLossGeneration = 0u;
    u64 m_lastFrameGeneration = 0u;
    u64 m_readyGeneration = 0u;
    u32 m_popupLayer = 0u;
    b8 m_rootActive = false;
    b8 m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

