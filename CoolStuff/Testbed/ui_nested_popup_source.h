// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/search_combo.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Five immutable keys; filtering owns a bounded cached view without changing full-source identity.
class TestbedUiNestedPopupSource final : public NWB::Impl::Ui::ISearchableListDataSource, NoCopy{
private:
    class View final : public NWB::Impl::Ui::IListDataSource, NoCopy{
    public:
        explicit View(const TestbedUiNestedPopupSource& source) : m_source(source){}


    public:
        virtual u64 instanceGeneration()const override{ return 2u; }
        virtual u64 revision()const override{ return m_source.m_viewRevision; }
        virtual u64 rowCount()const override{ return m_source.m_count; }
        virtual u64 key(u64 index)const override;
        virtual bool indexOf(u64 key, u64& index)const override;
        virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
        virtual StringView text(u64 index)const override;
        virtual bool enabled(u64 index)const override;


    private:
        const TestbedUiNestedPopupSource& m_source;
    };


public:
    explicit TestbedUiNestedPopupSource(NWB::Core::Alloc::GlobalArena& arena);


public:
    virtual u64 instanceGeneration()const override{ return 1u; }
    virtual u64 revision()const override{ return 1u; }
    virtual u64 rowCount()const override{ return 5u; }
    virtual u64 key(u64 index)const override{ return index < rowCount() ? index + 1u : 0u; }
    virtual bool indexOf(u64 key, u64& index)const override;
    virtual bool findEnabled(u64 start, bool reverse, u64& index)const override;
    virtual StringView text(u64 index)const override;
    virtual bool enabled(u64 index)const override{ return index < rowCount() && key(index) != 3u; }
    [[nodiscard]] virtual bool filter(AStringView query)override;
    [[nodiscard]] virtual const NWB::Impl::Ui::IListDataSource& filtered()const override{ return m_view; }
    void beginFrame(){ m_labelReads = 0u; }
    [[nodiscard]] u32 labelReads()const{ return m_labelReads; }


private:
    AString<NWB::Core::Alloc::GlobalArena> m_query;
    View m_view;
    Array<u64, 5u> m_indices{};
    u64 m_count = 0u;
    u64 m_viewRevision = 0u;
    mutable u32 m_labelReads = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

