// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/arena_base.h>
#include <global/containers.h>
#include <global/sync.h>
#include <global/filesystem/path.h>
#include <global/filesystem/staged_directory_paths.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Key, typename Value, typename Arena>
using AdhdHashMap = HashMap<Key, Value, Arena,
    Conditional_T<IsSame_V<Key, AString<::ArenaBase>>, Hasher<AStringView>, Hasher<Key>>,
    Conditional_T<IsSame_V<Key, AString<::ArenaBase>>, EqualTo<>, EqualTo<Key>>>;

template<typename TargetT, typename SrcT>
[[nodiscard]] FOUNDATION_INLINE TargetT AdhdRebindConstruct(::ArenaBase& targetArena, SrcT&& src);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T, typename Map>
[[nodiscard]] inline T& AdhdGetOrCreateTextRecord(Map& map, const AStringView name){
    if(const auto found = map.find(name); found != map.end())
        return found.value();
    auto& arena = map.get_allocator().arena();
    typename Map::key_type key(name, arena);
    return map.try_emplace(Move(key), arena).first.value();
}

template<typename T, typename MapT, typename K>
[[nodiscard]] b8 TryEmplaceOwned(MapT& destination, const K& key, T& value){
    return destination.try_emplace(key, Move(value)).second;
}

template<typename T, typename ArenaT>
[[nodiscard]] b8 TryCopyVector(Vector<T, ArenaT>& destination, const Span<const T> source){
    if(source.size() > destination.max_size())
        return false;
    destination.assign(source.begin(), source.end());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AdhdArenaDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
struct IsFixedArray : FalseType{};

template<typename T, usize N>
struct IsFixedArray<Array<T, N>> : TrueType{
    using Element = T;
    static constexpr usize s_Count = N;
};

template<typename T>
struct IsBoundedFixedVector : FalseType{};

template<typename T, usize N>
struct IsBoundedFixedVector<FixedVector<T, N>> : TrueType{
    using Element = T;
    static constexpr usize s_Count = N;
};

template<typename T>
struct IsStdExpected : FalseType{};

template<typename V, typename E>
struct IsStdExpected<Expected<V, E>> : TrueType{
    using Value = V;
    using Error = E;
};

template<typename T>
struct IsStdUnexpected : FalseType{};

template<typename E>
struct IsStdUnexpected<Unexpected<E>> : TrueType{};

template<typename TargetT, typename SrcT, usize... Indices>
[[nodiscard]] FOUNDATION_INLINE TargetT RebindConstructFixedArrayImpl(::ArenaBase& targetArena, SrcT&& src, IndexSequence<Indices...>){
    using Elem = typename IsFixedArray<TargetT>::Element;
    if constexpr(IsLValueReference_V<SrcT>)
        return TargetT{ ::AdhdRebindConstruct<Elem>(targetArena, src[Indices])... };
    else
        return TargetT{ ::AdhdRebindConstruct<Elem>(targetArena, Move(src[Indices]))... };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename TargetT, typename SrcT>
[[nodiscard]] FOUNDATION_INLINE TargetT AdhdRebindConstruct(::ArenaBase& targetArena, SrcT&& src){
    if constexpr(::LeadingArenaConstructible<TargetT> && IsConstructible_V<TargetT, ::ArenaBase&, SrcT>){
        return TargetT(targetArena, Forward<SrcT>(src));
    }
    else if constexpr(requires{
        typename TargetT::allocator_type;
        requires IsConstructible_V<typename TargetT::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, SrcT, typename TargetT::allocator_type>;
    }){
        return TargetT(Forward<SrcT>(src), typename TargetT::allocator_type(targetArena));
    }
    else if constexpr(requires{
        typename TargetT::container_type::allocator_type;
        requires IsConstructible_V<typename TargetT::container_type::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, SrcT, typename TargetT::container_type::allocator_type>;
    }){
        return TargetT(Forward<SrcT>(src), typename TargetT::container_type::allocator_type(targetArena));
    }
    else if constexpr(requires{
        typename TargetT::traits_type;
        typename TargetT::allocator_type;
        src.data();
        src.size();
        requires IsConstructible_V<typename TargetT::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, const typename TargetT::value_type*, usize, typename TargetT::allocator_type>;
    }){
        const typename TargetT::allocator_type outAlloc(targetArena);
        return TargetT(src.data(), src.size(), outAlloc);
    }
    else if constexpr(requires{
        typename TargetT::allocator_type;
        src.begin();
        src.end();
        requires IsConstructible_V<typename TargetT::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, typename TargetT::allocator_type>;
    }){
        if constexpr(!IsLValueReference_V<SrcT> && IsSame_V<RemoveCVRef_T<SrcT>, TargetT>){
            if(src.get_allocator().arenaPtr() == &targetArena)
                return TargetT(Forward<SrcT>(src));
        }
        const typename TargetT::allocator_type outAlloc(targetArena);
        TargetT out(outAlloc);
        if constexpr(requires{ out.reserve(src.size()); })
            out.reserve(src.size());
        if constexpr(requires{ typename TargetT::mapped_type; }){
            using KeyT = RemoveConst_T<typename TargetT::key_type>;
            using ValT = typename TargetT::mapped_type;
            for(auto&& kv : src){
                if constexpr(IsLValueReference_V<SrcT> || IsConst_V<RemoveReference_T<decltype(kv.second)>>)
                    out.emplace(AdhdRebindConstruct<KeyT>(targetArena, kv.first), AdhdRebindConstruct<ValT>(targetArena, kv.second));
                else
                    out.emplace(AdhdRebindConstruct<KeyT>(targetArena, kv.first), AdhdRebindConstruct<ValT>(targetArena, Move(kv.second)));
            }
        }
        else if constexpr(requires{ out.push_back(DeclVal<typename TargetT::value_type>()); }){
            using ElemT = typename TargetT::value_type;
            if constexpr(IsTriviallyCopyable_V<ElemT> && !LeadingArenaConstructible<ElemT> && IsSame_V<RemoveCVRef_T<decltype(*src.begin())>, ElemT> && requires{ out.assign(src.begin(), src.end()); })
                out.assign(src.begin(), src.end());
            else{
                for(auto&& elem : src){
                    if constexpr(IsLValueReference_V<SrcT> || IsConst_V<RemoveReference_T<decltype(elem)>>)
                        out.push_back(AdhdRebindConstruct<ElemT>(targetArena, elem));
                    else
                        out.push_back(AdhdRebindConstruct<ElemT>(targetArena, Move(elem)));
                }
            }
        }
        else if constexpr(requires{ out.insert(DeclVal<typename TargetT::value_type>()); }){
            using ElemT = typename TargetT::value_type;
            for(auto&& elem : src){
                if constexpr(IsLValueReference_V<SrcT> || IsConst_V<RemoveReference_T<decltype(elem)>>)
                    out.insert(AdhdRebindConstruct<ElemT>(targetArena, elem));
                else
                    out.insert(AdhdRebindConstruct<ElemT>(targetArena, Move(elem)));
            }
        }
        else{
            out.assign(src.begin(), src.end());
        }
        return out;
    }
    else if constexpr(AdhdArenaDetail::IsStdExpected<TargetT>::value){
        using ValT = typename AdhdArenaDetail::IsStdExpected<TargetT>::Value;
        using ErrT = typename TargetT::error_type;
        using SrcRaw = RemoveCVRef_T<SrcT>;
        if constexpr(AdhdArenaDetail::IsStdUnexpected<SrcRaw>::value){
            return TargetT(s_Unexpect, AdhdRebindConstruct<ErrT>(targetArena, Forward<SrcT>(src).error()));
        }
        else if constexpr(AdhdArenaDetail::IsStdExpected<SrcRaw>::value){
            if(!src.has_value())
                return TargetT(s_Unexpect, AdhdRebindConstruct<ErrT>(targetArena, Forward<SrcT>(src).error()));
            if constexpr(!IsSame_V<ValT, void>){
                if constexpr(IsLValueReference_V<SrcT>)
                    return TargetT(s_InPlace, AdhdRebindConstruct<ValT>(targetArena, *src));
                else
                    return TargetT(s_InPlace, AdhdRebindConstruct<ValT>(targetArena, Move(*src)));
            }
            else{
                return TargetT();
            }
        }
        else if constexpr(!IsSame_V<ValT, void>){
            return TargetT(s_InPlace, AdhdRebindConstruct<ValT>(targetArena, Forward<SrcT>(src)));
        }
        else{
            return TargetT(Forward<SrcT>(src));
        }
    }
    else if constexpr(ContainerDetail::AdaptorDetail::IsStdOptional<TargetT>::value){
        using Inner = typename TargetT::value_type;
        if constexpr(IsSame_V<RemoveCVRef_T<SrcT>, NullOptional>){
            return TargetT();
        }
        else if constexpr(ContainerDetail::AdaptorDetail::IsStdOptional<RemoveCVRef_T<SrcT>>::value){
            if(!src.has_value())
                return TargetT();
            if constexpr(IsLValueReference_V<SrcT>)
                return TargetT(s_InPlace, AdhdRebindConstruct<Inner>(targetArena, *src));
            else
                return TargetT(s_InPlace, AdhdRebindConstruct<Inner>(targetArena, Move(*src)));
        }
        else{
            return TargetT(s_InPlace, AdhdRebindConstruct<Inner>(targetArena, Forward<SrcT>(src)));
        }
    }
    else if constexpr(AdhdArenaDetail::IsFixedArray<TargetT>::value && IsSame_V<RemoveCVRef_T<SrcT>, TargetT>){
        return AdhdArenaDetail::RebindConstructFixedArrayImpl<TargetT>(
            targetArena,
            Forward<SrcT>(src),
            MakeIndexSequence<AdhdArenaDetail::IsFixedArray<TargetT>::s_Count>{}
        );
    }
    else if constexpr(AdhdArenaDetail::IsBoundedFixedVector<TargetT>::value && IsSame_V<RemoveCVRef_T<SrcT>, TargetT>){
        using Elem = typename AdhdArenaDetail::IsBoundedFixedVector<TargetT>::Element;
        TargetT out;
        for(usize i = 0u; i < src.size(); ++i){
            if constexpr(IsLValueReference_V<SrcT>)
                out.push_back(AdhdRebindConstruct<Elem>(targetArena, src[i]));
            else
                out.push_back(AdhdRebindConstruct<Elem>(targetArena, Move(src[i])));
        }
        return out;
    }
    else{
        return TargetT(Forward<SrcT>(src));
    }
}

template<typename TargetT>
[[nodiscard]] FOUNDATION_INLINE TargetT AdhdDefaultConstructInArena(::ArenaBase& targetArena){
    if constexpr(::LeadingArenaConstructible<TargetT> && IsConstructible_V<TargetT, ::ArenaBase&>){
        return TargetT(targetArena);
    }
    else if constexpr(requires{
        typename TargetT::allocator_type;
        requires IsConstructible_V<typename TargetT::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, typename TargetT::allocator_type>;
    }){
        const typename TargetT::allocator_type alloc(targetArena);
        return TargetT(alloc);
    }
    else if constexpr(requires{
        typename TargetT::container_type::allocator_type;
        requires IsConstructible_V<typename TargetT::container_type::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<TargetT, typename TargetT::container_type::allocator_type>;
    }){
        const typename TargetT::container_type::allocator_type alloc(targetArena);
        return TargetT(alloc);
    }
    else if constexpr(AdhdArenaDetail::IsFixedArray<TargetT>::value){
        using ElemT = typename AdhdArenaDetail::IsFixedArray<TargetT>::Element;
        constexpr usize s_Count = AdhdArenaDetail::IsFixedArray<TargetT>::s_Count;
        return [&]<usize... Indices>(IndexSequence<Indices...>){
            return TargetT{ (static_cast<void>(Indices), AdhdDefaultConstructInArena<ElemT>(targetArena))... };
        }(MakeIndexSequence<s_Count>{});
    }
    else{
        return TargetT{};
    }
}

template<typename LhsT, typename RhsT>
FOUNDATION_INLINE LhsT& AdhdAssign(LhsT& lhs, RhsT&& rhs){
    using RhsRaw = RemoveCVRef_T<RhsT>;
    if constexpr(
        requires{ typename LhsT::traits_type; lhs.get_allocator().arenaPtr(); lhs.assign(rhs.data(), rhs.size()); }
    ){
        if constexpr(IsSame_V<LhsT, RhsRaw>){
            if(&lhs == &rhs)
                return lhs;
            if(lhs.get_allocator().arenaPtr() == rhs.get_allocator().arenaPtr()){
                lhs = Forward<RhsT>(rhs);
                return lhs;
            }
        }
        lhs.assign(rhs.data(), rhs.size());
        return lhs;
    }
    else if constexpr(
        requires{ lhs.get_allocator().arena(); lhs.clear(); rhs.begin(); rhs.end(); }
        && !requires{ typename LhsT::traits_type; }
    ){
        if constexpr(IsSame_V<LhsT, RhsRaw>){
            if(&lhs == &rhs)
                return lhs;
            if(lhs.get_allocator().arenaPtr() == rhs.get_allocator().arenaPtr()){
                lhs = Forward<RhsT>(rhs);
                return lhs;
            }
        }
        ::ArenaBase& targetArena = lhs.get_allocator().arena();
        lhs.clear();
        if constexpr(requires{ lhs.reserve(rhs.size()); })
            lhs.reserve(rhs.size());
        if constexpr(requires{ typename LhsT::mapped_type; }){
            using KeyT = RemoveConst_T<typename LhsT::key_type>;
            using ValT = typename LhsT::mapped_type;
            for(auto&& kv : rhs){
                if constexpr(IsLValueReference_V<RhsT> || IsConst_V<RemoveReference_T<decltype(kv.second)>>)
                    lhs.emplace(AdhdRebindConstruct<KeyT>(targetArena, kv.first), AdhdRebindConstruct<ValT>(targetArena, kv.second));
                else
                    lhs.emplace(AdhdRebindConstruct<KeyT>(targetArena, kv.first), AdhdRebindConstruct<ValT>(targetArena, Move(kv.second)));
            }
        }
        else if constexpr(requires{ lhs.push_back(DeclVal<typename LhsT::value_type>()); }){
            using ElemT = typename LhsT::value_type;
            if constexpr(IsTriviallyCopyable_V<ElemT> && !LeadingArenaConstructible<ElemT> && IsSame_V<RemoveCVRef_T<decltype(*rhs.begin())>, ElemT> && requires{ lhs.assign(rhs.begin(), rhs.end()); })
                lhs.assign(rhs.begin(), rhs.end());
            else{
                for(auto&& elem : rhs){
                    if constexpr(IsLValueReference_V<RhsT> || IsConst_V<RemoveReference_T<decltype(elem)>>)
                        lhs.push_back(AdhdRebindConstruct<ElemT>(targetArena, elem));
                    else
                        lhs.push_back(AdhdRebindConstruct<ElemT>(targetArena, Move(elem)));
                }
            }
        }
        else if constexpr(requires{ lhs.insert(DeclVal<typename LhsT::value_type>()); }){
            using ElemT = typename LhsT::value_type;
            for(auto&& elem : rhs){
                if constexpr(IsLValueReference_V<RhsT> || IsConst_V<RemoveReference_T<decltype(elem)>>)
                    lhs.insert(AdhdRebindConstruct<ElemT>(targetArena, elem));
                else
                    lhs.insert(AdhdRebindConstruct<ElemT>(targetArena, Move(elem)));
            }
        }
        else{
            lhs.assign(rhs.begin(), rhs.end());
        }
        return lhs;
    }
    else if constexpr(AdhdArenaDetail::IsFixedArray<LhsT>::value && IsSame_V<LhsT, RhsRaw>){
        if(&lhs == &rhs)
            return lhs;
        for(usize i = 0u; i < AdhdArenaDetail::IsFixedArray<LhsT>::s_Count; ++i){
            if constexpr(IsLValueReference_V<RhsT>)
                AdhdAssign(lhs[i], rhs[i]);
            else
                AdhdAssign(lhs[i], Move(rhs[i]));
        }
        return lhs;
    }
    else{
        lhs = Forward<RhsT>(rhs);
        return lhs;
    }
}

template<typename T, typename Sequence>
    requires(IsSame_V<T, typename Sequence::value_type>)
inline void AdhdAppendOwned(Sequence& values, T& source){
    values.push_back(AdhdRebindConstruct<T>(values.get_allocator().arena(), Move(source)));
}

template<typename T, typename Sequence, typename Error>
    requires(IsSame_V<T, typename Sequence::value_type>)
[[nodiscard]] inline b8 AdhdAppendParsedValue(Sequence& values, Expected<T, Error> parsed){
    if(!parsed)
        return false;
    values.push_back(AdhdRebindConstruct<T>(values.get_allocator().arena(), Move(*parsed)));
    return true;
}

template<typename T, typename DestinationArena, typename SourceArena>
inline void AdhdTransferVector(Vector<T, DestinationArena>& destination, Vector<T, SourceArena>& source){
    if constexpr(IsSame_V<DestinationArena, SourceArena>){
        if(&destination == &source)
            return;
    }
    AdhdAssign(destination, Move(source));
    source.clear();
}

template<typename T>
    requires (!IsCopyConstructible_V<T> && IsAssignable_V<T&, T&&>)
FOUNDATION_INLINE void AdhdMoveInto(T& destination, T& source){
    if(&destination != &source)
        AdhdAssign(destination, Move(source));
}

template<typename LhsT, typename RhsT>
FOUNDATION_INLINE LhsT& AdhdAssignLocked(::Futex& mutex, LhsT& lhs, RhsT&& rhs){
    const ::NothrowScopedLock lock(mutex);
    return AdhdAssign(lhs, Forward<RhsT>(rhs));
}

template<typename InnerT, typename RhsT>
FOUNDATION_INLINE Optional<InnerT>& AdhdAssignWrapped(::ArenaBase& targetArena, Optional<InnerT>& lhs, RhsT&& rhs){
    using RhsRaw = RemoveCVRef_T<RhsT>;
    if constexpr(IsSame_V<RhsRaw, NullOptional>){
        lhs.reset();
        return lhs;
    }
    else if constexpr(ContainerDetail::AdaptorDetail::IsStdOptional<RhsRaw>::value){
        if constexpr(IsSame_V<Optional<InnerT>, RhsRaw>){
            if(&lhs == &rhs)
                return lhs;
        }
        if(!rhs.has_value()){
            lhs.reset();
            return lhs;
        }
        if(lhs.has_value()){
            if constexpr(IsLValueReference_V<RhsT>)
                AdhdAssign(*lhs, *rhs);
            else
                AdhdAssign(*lhs, Move(*rhs));
        }
        else{
            if constexpr(IsLValueReference_V<RhsT>)
                lhs.emplace(AdhdRebindConstruct<InnerT>(targetArena, *rhs));
            else
                lhs.emplace(AdhdRebindConstruct<InnerT>(targetArena, Move(*rhs)));
        }
        return lhs;
    }
    else{
        if(lhs.has_value())
            AdhdAssign(*lhs, Forward<RhsT>(rhs));
        else
            lhs.emplace(AdhdRebindConstruct<InnerT>(targetArena, Forward<RhsT>(rhs)));
        return lhs;
    }
}

template<typename ValueT, typename ErrorT, typename RhsT>
FOUNDATION_INLINE Expected<ValueT, ErrorT>& AdhdAssignWrapped(::ArenaBase& targetArena, Expected<ValueT, ErrorT>& lhs, RhsT&& rhs){
    using TargetT = Expected<ValueT, ErrorT>;
    if constexpr(IsSame_V<TargetT, RemoveCVRef_T<RhsT>>){
        if(&lhs == &rhs)
            return lhs;
    }
    lhs = AdhdRebindConstruct<TargetT>(targetArena, Forward<RhsT>(rhs));
    return lhs;
}

template<typename LhsT, typename RhsT>
FOUNDATION_INLINE LhsT& AdhdAssignWrappedLocked(::Futex& mutex, ::ArenaBase& targetArena, LhsT& lhs, RhsT&& rhs){
    const ::NothrowScopedLock lock(mutex);
    return AdhdAssignWrapped(targetArena, lhs, Forward<RhsT>(rhs));
}

template<typename MapT, typename KeyT>
[[nodiscard]] FOUNDATION_INLINE typename MapT::mapped_type& AdhdMapIndex(MapT& map, KeyT&& key){
    using MappedT = typename MapT::mapped_type;
    ::ArenaBase& mapArena = map.get_allocator().arena();
    if constexpr(::LeadingArenaConstructible<MappedT> && IsConstructible_V<MappedT, ::ArenaBase&>){
        const auto it = map.try_emplace(Forward<KeyT>(key), mapArena).first;
        if constexpr(requires{ it.value(); })
            return it.value();
        else
            return it->second;
    }
    else if constexpr(requires{
        typename MappedT::allocator_type;
        requires IsConstructible_V<typename MappedT::allocator_type, ::ArenaBase&>;
        requires IsConstructible_V<MappedT, typename MappedT::allocator_type>;
    }){
        const auto it = map.try_emplace(Forward<KeyT>(key), typename MappedT::allocator_type(mapArena)).first;
        if constexpr(requires{ it.value(); })
            return it.value();
        else
            return it->second;
    }
    else{
        return map[Forward<KeyT>(key)];
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

