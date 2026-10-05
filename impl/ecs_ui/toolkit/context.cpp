// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "context.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize Context::StateClaimHash::operator()(const StateClaim& claim)const{
    usize hash = Hasher<u64>{}(claim.instanceGeneration);
    HashCombine(hash, static_cast<u8>(claim.kind));
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Context::containsRoot(const WidgetRoot* roots, const usize count, const WidgetRoot& root){
    for(usize index = 0u; index < count; ++index){
        if(roots[index] == root)
            return true;
    }
    return false;
}

Context::Context(Core::Alloc::GlobalArena& arena)
    : m_input(arena)
    , m_states(arena)
    , m_scopes(arena)
    , m_targets(arena)
    , m_popups(arena)
    , m_stateClaims(arena)
    , m_commitTargets(arena)
    , m_commitPopups(arena)
{
    m_scopes.reserve(64u);
    m_targets.reserve(s_InputMaxTargets);
    m_commitTargets.reserve(s_InputMaxTargets);
    m_popups.reserve(s_InputMaxPopups);
    m_commitPopups.reserve(s_InputMaxPopups);
}

bool Context::beginFrame(const u64 generation){
    if(generation == 0u || generation <= m_lastFrameGeneration || m_frameGeneration != 0u || ready())
        return false;
    m_lastFrameGeneration = generation;
    m_frameGeneration = generation;
    m_focusLossGeneration = m_input.focusLossGeneration();
    m_failed = false;
    m_declarationCount = 0u;
    m_stateClaimCount = 0u;
    m_targets.clear();
    m_popups.clear();
    m_popupDepth = 0u;
    m_currentPopup = {};
    m_popupLayer = 0u;
    return true;
}

bool Context::beginRoot(const WidgetRoot& root){
    if(m_failed || m_frameGeneration == 0u || m_rootActive || root.generation == 0u){
        fail();
        return false;
    }
    m_root = root;
    m_rootActive = true;
    m_scopes.clear();
    m_scopes.push_back(MakeRootId(root));
    return true;
}

bool Context::endRoot(){
    const bool valid = m_rootActive && m_scopes.size() == 1u && m_popupDepth == 0u && !m_currentPopup.valid();
    m_rootActive = false;
    m_scopes.clear();
    if(!valid)
        fail();
    return valid && !m_failed;
}

bool Context::pushScope(const AStringView stableKey){
    if(m_failed || !m_rootActive || m_scopes.size() >= 64u){
        fail();
        return false;
    }
    const WidgetId id = MakeWidgetId(m_scopes.back(), stableKey);
    if(!id.valid()){
        fail();
        return false;
    }
    m_scopes.push_back(id);
    return true;
}

bool Context::popScope(){
    if(!m_rootActive || m_scopes.size() <= 1u){
        fail();
        return false;
    }
    m_scopes.pop_back();
    return !m_failed;
}

WidgetState* Context::declare(const AStringView stableKey, const WidgetKind::Enum kind){
    if(m_failed || !m_rootActive)
        return nullptr;
    const WidgetId id = MakeWidgetId(m_scopes.back(), stableKey);
    return declareId(id, kind);
}

WidgetState* Context::declarePart(const WidgetState& owner, const AStringView stableKey, const WidgetKind::Enum kind){
    if(m_failed || !currentDeclaration(owner)){
        fail();
        return nullptr;
    }
    return declareId(MakeWidgetId(owner.id, stableKey), kind);
}

bool Context::claimState(const WidgetState& owner, const u64 instanceGeneration){
    if(m_failed || !currentDeclaration(owner) || instanceGeneration == 0u || m_stateClaimCount == s_InputMaxTargets){
        fail();
        return false;
    }
    const StateClaim claim{ instanceGeneration, owner.kind };
    if(m_stateClaimCount < s_SmallStateClaims){
        for(usize index = 0u; index < m_stateClaimCount; ++index){
            if(m_smallStateClaims[index] == claim){
                fail();
                return false;
            }
        }
        m_smallStateClaims[m_stateClaimCount] = claim;
    }
    else{
        if(m_stateClaimCount == s_SmallStateClaims){
            // Small frames never clear or allocate the retained hash storage from an earlier large frame.
            m_stateClaims.clear();
            if(m_stateClaims.bucket_count() == 0u)
                m_stateClaims.reserve(s_SmallStateClaims * 2u);
            m_stateClaims.insert(m_smallStateClaims.begin(), m_smallStateClaims.end());
        }
        if(!m_stateClaims.emplace(claim).second){
            fail();
            return false;
        }
    }
    ++m_stateClaimCount;
    return true;
}

bool Context::addTarget(const WidgetState& state, HitTarget target){
    if(m_failed || !currentDeclaration(state) || m_targets.size() >= s_InputMaxTargets){
        fail();
        return false;
    }
    target.id = state.id;
    target.declarationGeneration = state.declarationGeneration;
    target.paintOrder = static_cast<u32>(m_targets.size());
    target.popup = m_currentPopup;
    target.layer = m_popupLayer;
    m_targets.push_back({ target, state.root });
    return true;
}

bool Context::takeActivation(const WidgetState& state, const bool enabled){
    if(m_failed || !currentDeclaration(state))
        return false;
    if(!enabled){
        m_input.invalidateTarget(state.id);
        return false;
    }
    const HitTarget* target = m_input.findTarget(state.id);
    if(target && target->popup != m_currentPopup)
        return false;
    return !m_failed && m_input.consumeActivation(state.id);
}

bool Context::takePointerGesture(const WidgetState& state, const bool enabled, PointerGesture& gesture){
    if(m_failed || !currentDeclaration(state))
        return false;
    if(!enabled){
        m_input.invalidateTarget(state.id);
        return false;
    }
    const HitTarget* target = m_input.findTarget(state.id);
    if(target && target->popup != m_currentPopup)
        return false;
    return m_input.consumePointerGesture(state.id, state.declarationGeneration, gesture);
}

bool Context::finishFrame(){
    if(m_failed || m_frameGeneration == 0u || m_rootActive || m_popupDepth != 0u || m_currentPopup.valid()){
        fail();
        return false;
    }
    retireUnseenStates();
    m_readyGeneration = m_frameGeneration;
    m_frameGeneration = 0u;
    return true;
}

bool Context::commitFrame(const u64 generation){
    if(generation == 0u || generation != m_readyGeneration)
        return false;
    m_commitTargets.clear();
    for(const auto& owned : m_targets)
        m_commitTargets.push_back(owned.target);
    m_commitPopups.clear();
    for(const auto& owned : m_popups)
        m_commitPopups.push_back(owned.scope);
    if(!m_input.commitTargets(
        m_commitTargets.data(), m_commitTargets.size(), generation, m_commitPopups.data(), m_commitPopups.size(), m_focusLossGeneration
    ))
        return false;
    m_targets.clear();
    m_popups.clear();
    m_readyGeneration = 0u;
    return true;
}

void Context::abandonFrame(){
    resetInput();
    retireUnseenStates();
    m_rootActive = false;
    m_scopes.clear();
    m_frameGeneration = 0u;
    m_failed = false;
}

void Context::resetInput(){
    m_input.reset();
    m_targets.clear();
    m_popups.clear();
    m_currentPopup = {};
    m_popupDepth = 0u;
    m_popupStack.fill({});
    m_popupLayer = 0u;
    m_readyGeneration = 0u;
}

void Context::retainRoots(const WidgetRoot* roots, const usize count){
    if(!roots && count != 0u)
        return;
    for(usize index = m_states.entries().size(); index > 0u; --index){
        const auto& state = m_states.entries()[index - 1u];
        if(!containsRoot(roots, count, state.root)){
            m_input.invalidateTarget(state.id);
            m_states.erase(index - 1u);
        }
    }
    for(usize index = m_targets.size(); index > 0u; --index){
        if(!containsRoot(roots, count, m_targets[index - 1u].root))
            m_targets.erase(m_targets.begin() + index - 1u);
    }
    for(usize index = m_popups.size(); index > 0u; --index){
        if(!containsRoot(roots, count, m_popups[index - 1u].root))
            m_popups.erase(m_popups.begin() + static_cast<isize>(index - 1u));
    }
}

WidgetState* Context::declareId(const WidgetId id, const WidgetKind::Enum kind){
    if(m_declarationCount >= s_WidgetMaxStates){
        fail();
        return nullptr;
    }
    const WidgetState* previous = m_states.find(id);
    if(previous && previous->kind != kind)
        m_input.invalidateTarget(id);
    WidgetState* state = m_states.touch(id, m_root, kind, m_frameGeneration);
    if(!state)
        fail();
    else
        ++m_declarationCount;
    return state;
}

bool Context::currentDeclaration(const WidgetState& state)const{
    if(!m_rootActive || !(state.root == m_root) || state.lastSeenFrame != m_frameGeneration)
        return false;
    const WidgetState* current = m_states.find(state.id);
    return
        current && current->root == state.root && current->declarationGeneration == state.declarationGeneration
        && current->lastSeenFrame == state.lastSeenFrame && current->kind == state.kind
    ;
}


void Context::retireUnseenStates(){
    if(m_frameGeneration == 0u)
        return;
    for(usize index = m_states.entries().size(); index > 0u; --index){
        const auto& state = m_states.entries()[index - 1u];
        if(state.lastSeenFrame != m_frameGeneration){
            m_input.invalidateTarget(state.id);
            m_states.erase(index - 1u);
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

