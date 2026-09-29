// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "list_source.h"

#include <impl/ui/widgets/search_combo.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A numeric prefix is a maximum of six disjoint decimal intervals, independent of the 100000-row count.
class UiSearchComboSmokeSource final : public Impl::Ui::ISearchableListDataSource, NoCopy{
private:
    struct Range{
        u64 first = 0u;
        u64 past = 0u;
    };

    class View final : public Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const UiSearchComboSmokeSource& source) : m_source(source){}


    public:
        virtual u64 instanceGeneration()const override{ return 2u; }
        virtual u64 revision()const override{ return m_source.m_viewRevision; }
        virtual u64 rowCount()const override{ return m_source.m_filteredCount; }
        virtual u64 key(u64 index)const override;
        virtual bool indexOf(u64 key, u64& index)const override;
        virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
        virtual StringView text(u64 index)const override;
        virtual bool enabled(u64 index)const override;


    private:
        const UiSearchComboSmokeSource& m_source;
    };


public:
    explicit UiSearchComboSmokeSource(Core::Alloc::GlobalArena& arena);


public:
    virtual u64 instanceGeneration()const override{ return m_rows.instanceGeneration(); }
    virtual u64 revision()const override{ return m_rows.revision(); }
    virtual u64 rowCount()const override{ return m_rows.rowCount(); }
    virtual u64 key(u64 index)const override{ return m_rows.key(index); }
    virtual bool indexOf(u64 key, u64& index)const override{ return m_rows.indexOf(key, index); }
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    virtual StringView text(u64 index)const override{ return m_rows.text(index); }
    virtual bool enabled(u64 index)const override{ return m_rows.enabled(index); }
    [[nodiscard]] virtual bool filter(AStringView query)override;
    [[nodiscard]] virtual const Impl::Ui::IListDataSource& filtered()const override{ return m_view; }


public:
    void reverse(){ m_rows.reverse(); }
    void remove(u64 key){ m_rows.remove(key); }
    void beginFrame(){ m_rows.beginFrame(); }
    [[nodiscard]] bool reversed()const{ return m_rows.reversed(); }
    [[nodiscard]] u64 removedKey()const{ return m_rows.removedKey(); }
    [[nodiscard]] u32 labelReads()const{ return m_rows.labelReads(); }


private:
    void addRange(u64 first, u64 past);


private:
    UiListSmokeSource m_rows;
    AString<Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<Range, 6u> m_ranges{};
    usize m_rangeCount = 0u;
    u64 m_filteredCount = 0u;
    u64 m_viewRevision = 0u;
    u64 m_cachedSourceRevision = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

