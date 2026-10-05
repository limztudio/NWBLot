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

    struct OwnedPopup{
        PopupScope scope;
        WidgetRoot root;
    };

    struct StateClaim{
        u64 instanceGeneration = 0u;
        WidgetKind::Enum kind = WidgetKind::Panel;

        [[nodiscard]] bool operator==(const StateClaim&)const = default;
    };

    struct StateClaimHash{
        [[nodiscard]] usize operator()(const StateClaim& claim)const;
    };

    using StateClaims = HashSet<StateClaim, Core::Alloc::GlobalArena, StateClaimHash, EqualTo<StateClaim>>;


private:
    static constexpr usize s_SmallStateClaims = 32u;

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
    // Compound controls declare internal scopes from their owning widget instead of borrowing the current layout scope.
    [[nodiscard]] WidgetState* declarePart(const WidgetState& owner, AStringView stableKey, WidgetKind::Enum kind);
    // A state instance may own only one control of a given kind in a frame, even across balanced roots/scopes.
    [[nodiscard]] bool claimState(const WidgetState& owner, u64 instanceGeneration);
    [[nodiscard]] bool addTarget(const WidgetState& state, HitTarget target);
    [[nodiscard]] bool addPartTarget(const WidgetState& owner, WidgetId part, HitTarget target);
    [[nodiscard]] bool takeActivation(const WidgetState& state, bool enabled);
    [[nodiscard]] bool takePointerGesture(const WidgetState& state, bool enabled, PointerGesture& gesture);
    [[nodiscard]] bool takeControlAction(const WidgetState& state, bool enabled, const ControlToken& token, ControlAction& action);
    [[nodiscard]] bool takePartPointerGesture(const WidgetState& owner, WidgetId part, bool enabled, PointerGesture& gesture);
    // Registration reserves the declaration-order layer; activation may be repeated during deferred painting.
    [[nodiscard]] bool registerPopupScope(const WidgetState& state, PopupScope scope);
    [[nodiscard]] bool activatePopupScope(const PopupToken& token);
    [[nodiscard]] bool updatePopupScope(const PopupToken& token, PopupScope scope);
    [[nodiscard]] bool beginPopupScope(const WidgetState& state, PopupScope scope);
    [[nodiscard]] bool endPopupScope(bool visible);
    // Discard an inactive scope and its descendants without disturbing the current parent activation.
    void discardPopupScope(const PopupToken& token);
    [[nodiscard]] bool hasPopupScope(const PopupToken& token)const;
    [[nodiscard]] WidgetId scopeId()const{ return m_scopes.empty() ? WidgetId{} : m_scopes.back(); }
    [[nodiscard]] PopupToken popupToken()const{ return m_currentPopup; }
    [[nodiscard]] u32 popupLayer()const{ return m_popupLayer; }
    [[nodiscard]] u32 popupLayer(const PopupToken& token)const;
    [[nodiscard]] PopupToken topPopupToken()const{ return m_popups.empty() ? PopupToken{} : m_popups.back().scope.token; }
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
    [[nodiscard]] WidgetState* declareId(WidgetId id, WidgetKind::Enum kind);
    [[nodiscard]] bool currentDeclaration(const WidgetState& state)const;
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
    u32 m_popupLayer = 0u;
    WidgetRoot m_root;
    u64 m_frameGeneration = 0u;
    u64 m_focusLossGeneration = 0u;
    u64 m_lastFrameGeneration = 0u;
    u64 m_readyGeneration = 0u;
    bool m_rootActive = false;
    bool m_failed = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

