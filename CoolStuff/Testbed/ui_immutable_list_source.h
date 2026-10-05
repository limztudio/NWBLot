// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/search_combo.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Immutable demo rows share key navigation without adding per-source row or label storage.
template<typename Source, usize LabelCount, u64 DisabledKey, typename Interface = NWB::Impl::Ui::IListDataSource>
class TestbedUiImmutableListSource : public Interface, NoCopy{
public:
    virtual u64 instanceGeneration()const override final{ return 1u; }
    virtual u64 revision()const override final{ return 1u; }
    virtual u64 rowCount()const override final{ return LabelCount; }
    virtual u64 key(const u64 index)const override final{ return index < LabelCount ? index + 1u : 0u; }

    virtual bool indexOf(const u64 keyValue, u64& index)const override final{
        if(keyValue == 0u || keyValue > LabelCount)
            return false;
        index = keyValue - 1u;
        return true;
    }

    virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override final{
        if(start >= LabelCount)
            return false;
        if(enabled(start)){
            index = start;
            return true;
        }
        if(reverse){
            if(start == 0u)
                return false;
            index = start - 1u;
        }
        else{
            if(start + 1u >= LabelCount)
                return false;
            index = start + 1u;
        }
        return enabled(index);
    }

    virtual StringView text(const u64 index)const override{ return index < LabelCount ? Source::s_Labels[index] : StringView{}; }
    virtual bool enabled(const u64 index)const override final{ return index < LabelCount && index + 1u != DisabledKey; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source, usize LabelCount, u64 DisabledKey>
class TestbedUiFilteredListSource : public TestbedUiImmutableListSource<Source, LabelCount, DisabledKey, NWB::Impl::Ui::ISearchableListDataSource>{
private:
    class View final : public NWB::Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const TestbedUiFilteredListSource& source) : m_source(source){}


    public:
        virtual u64 instanceGeneration()const override{ return 2u; }
        virtual u64 revision()const override{ return m_source.m_viewRevision; }
        virtual u64 rowCount()const override{ return m_source.m_count; }
        virtual u64 key(const u64 index)const override{ return index < rowCount() ? m_source.key(m_source.m_indices[index]) : 0u; }

        virtual bool indexOf(const u64 keyValue, u64& index)const override{
            for(u64 candidate = 0u; candidate < rowCount(); ++candidate){
                if(key(candidate) == keyValue){
                    index = candidate;
                    return true;
                }
            }
            return false;
        }

        virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
            if(start >= rowCount())
                return false;
            for(u64 candidate = start; candidate < rowCount(); reverse ? --candidate : ++candidate){
                if(enabled(candidate)){
                    index = candidate;
                    return true;
                }
                if(reverse && candidate == 0u)
                    break;
            }
            return false;
        }

        virtual StringView text(const u64 index)const override{
            return index < rowCount() ? static_cast<const Source&>(m_source).text(m_source.m_indices[index]) : StringView{};
        }

        virtual bool enabled(const u64 index)const override{ return index < rowCount() && m_source.enabled(m_source.m_indices[index]); }


    private:
        const TestbedUiFilteredListSource& m_source;
    };


private:
    [[nodiscard]] static char Lower(const char value){
        return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
    }

    [[nodiscard]] static bool Matches(const StringView text, const AStringView query){
        if(query.size() > text.size())
            return false;
        for(usize start = 0u; start <= text.size() - query.size(); ++start){
            bool matched = true;
            for(usize offset = 0u; offset < query.size(); ++offset){
                if(Lower(text[start + offset]) != Lower(query[offset])){
                    matched = false;
                    break;
                }
            }
            if(matched)
                return true;
        }
        return false;
    }


public:
    explicit TestbedUiFilteredListSource(NWB::Core::Alloc::GlobalArena& arena)
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
            if(!Matches(Source::s_Labels[index], query))
                continue;
            m_indices[m_count] = index;
            ++m_count;
        }
        ++m_viewRevision;
        return true;
    }

    [[nodiscard]] virtual const NWB::Impl::Ui::IListDataSource& filtered()const override final{ return m_view; }


private:
    AString<NWB::Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<u64, LabelCount> m_indices{};
    u64 m_count = 0u;
    u64 m_viewRevision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

