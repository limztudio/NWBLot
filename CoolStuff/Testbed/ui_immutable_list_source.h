// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"

#include <impl/ecs_ui/toolkit/widgets/search_combo.h>

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Immutable demo rows share key navigation without adding per-source row or label storage.
// TextNoThrow is checked when the label reads are instantiated, after the source type is complete.
template<typename Source, usize LabelCount, u64 DisabledKey, typename Interface = NWB::Impl::Ui::IListDataSource, bool TextNoThrow = true>
class UiImmutableListSource : public Interface, NoCopy{
public:
    virtual u64 instanceGeneration()const noexcept override final{ return 1u; }

    virtual u64 revision()const noexcept override final{ return 1u; }

    virtual u64 rowCount()const noexcept override final{ return LabelCount; }

    virtual u64 key(const u64 index)const noexcept override final{ return index < LabelCount ? index + 1u : 0u; }

    virtual Expected<u64> indexOf(const u64 keyValue)const noexcept override final{
        if(keyValue == 0u || keyValue > LabelCount)
            return MakeUnexpected(Failure{});
        return keyValue - 1u;
    }

    virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const noexcept override final{
        if(start >= LabelCount)
            return MakeUnexpected(Failure{});
        if(enabled(start))
            return start;
        u64 index = 0u;
        if(reverse){
            if(start == 0u)
                return MakeUnexpected(Failure{});
            index = start - 1u;
        }
        else{
            if(start + 1u >= LabelCount)
                return MakeUnexpected(Failure{});
            index = start + 1u;
        }
        if(!enabled(index))
            return MakeUnexpected(Failure{});
        return index;
    }

    virtual StringView text(const u64 index)const noexcept(TextNoThrow)override{
        static_assert(!TextNoThrow || noexcept(StringView(index < LabelCount ? Source::s_Labels[index] : StringView{})), "throwing list labels require TextNoThrow = false");
        return index < LabelCount ? Source::s_Labels[index] : StringView{};
    }

    virtual bool enabled(const u64 index)const noexcept override final{ return index < LabelCount && index + 1u != DisabledKey; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source, usize LabelCount, u64 DisabledKey, bool TextNoThrow = true>
class UiFilteredListSource : public UiImmutableListSource<Source, LabelCount, DisabledKey, NWB::Impl::Ui::ISearchableListDataSource, TextNoThrow>{
private:
    class View final : public NWB::Impl::Ui::IListDataSource, NoCopy{
    private:
        static constexpr u64 s_FilteredInstanceGeneration = 2u;


    public:
        explicit View(const UiFilteredListSource& source)noexcept
            : m_source(source)
        {}


    public:
        virtual u64 instanceGeneration()const noexcept override{ return s_FilteredInstanceGeneration; }

        virtual u64 revision()const noexcept override{ return m_source.m_viewRevision; }

        virtual u64 rowCount()const noexcept override{ return m_source.m_count; }

        virtual u64 key(const u64 index)const noexcept override{ return index < rowCount() ? m_source.key(m_source.m_indices[index]) : 0u; }

        virtual Expected<u64> indexOf(const u64 keyValue)const noexcept override{
            for(u64 candidate = 0u; candidate < rowCount(); ++candidate){
                if(key(candidate) == keyValue){
                    return candidate;
                }
            }
            return MakeUnexpected(Failure{});
        }

        virtual Expected<u64> findEnabled(const u64 start, const bool reverse)const noexcept override{
            if(start >= rowCount())
                return MakeUnexpected(Failure{});
            for(u64 candidate = start; candidate < rowCount(); reverse ? --candidate : ++candidate){
                if(enabled(candidate)){
                    return candidate;
                }
                if(reverse && candidate == 0u)
                    break;
            }
            return MakeUnexpected(Failure{});
        }

        virtual StringView text(const u64 index)const noexcept(TextNoThrow)override{
            static_assert(!TextNoThrow || noexcept(StringView(index < rowCount() ? static_cast<const Source&>(m_source).text(m_source.m_indices[index]) : StringView{})), "throwing list labels require TextNoThrow = false");
            return index < rowCount() ? static_cast<const Source&>(m_source).text(m_source.m_indices[index]) : StringView{};
        }

        virtual bool enabled(const u64 index)const noexcept override{ return index < rowCount() && m_source.enabled(m_source.m_indices[index]); }


    private:
        const UiFilteredListSource& m_source;
    };


public:
    explicit UiFilteredListSource(NWB::Core::Alloc::GlobalArena& arena)noexcept
        : m_query(arena)
        , m_view(*this)
        , m_count(LabelCount)
        , m_viewRevision(1u)
    {
        for(u64 index = 0u; index < LabelCount; ++index)
            m_indices[index] = index;
    }


public:
    [[nodiscard]] virtual bool filter(const AStringView query)override final{
        if(m_viewRevision != 0u && query == AStringView(m_query.data(), m_query.size()))
            return true;
        if(query.empty())
            m_query.clear();
        else
            m_query.assign(query.data(), query.size());
        m_count = 0u;
        for(u64 index = 0u; index < LabelCount; ++index){
            if(!ContainsAsciiIgnoreCase<char>(Source::s_Labels[index], query))
                continue;
            m_indices[m_count] = index;
            ++m_count;
        }
        ++m_viewRevision;
        return true;
    }

    [[nodiscard]] virtual const NWB::Impl::Ui::IListDataSource& filtered()const noexcept override final{ return m_view; }


private:
    AString<NWB::Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<u64, LabelCount> m_indices{};
    u64 m_count = 0u;
    u64 m_viewRevision = 0u;
};


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

