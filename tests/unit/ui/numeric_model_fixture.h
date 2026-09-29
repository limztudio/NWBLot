// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/edit/float_model.h>
#include <impl/ui/edit/integer_model.h>

#include <global/bit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiNumericTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

class DraftSnapshot final{
public:
    DraftSnapshot(Core::Alloc::GlobalArena& arena, const EditModel& draft)
        : m_text(arena)
        , m_preedit(arena)
    {
        if(!draft.text().empty())
            m_text.assign(draft.text().data(), draft.text().size());
        m_generation = draft.instanceGeneration();
        m_revision = draft.revision();
        m_external = draft.externalRevision();
        m_compositionGeneration = draft.compositionGeneration();
        m_anchor = draft.anchor();
        m_caret = draft.caret();
        m_undo = draft.canUndo();
        m_redo = draft.canRedo();
        m_composition = draft.composition();
        if(!m_composition.text.empty())
            m_preedit.assign(m_composition.text.data(), m_composition.text.size());
        m_composition.text = {};
    }


public:
    void expectUnchanged(const EditModel& draft)const{
        EXPECT_EQ(draft.text(), AStringView(m_text.data(), m_text.size()));
        EXPECT_EQ(draft.instanceGeneration(), m_generation);
        EXPECT_EQ(draft.revision(), m_revision);
        EXPECT_EQ(draft.externalRevision(), m_external);
        EXPECT_EQ(draft.compositionGeneration(), m_compositionGeneration);
        EXPECT_EQ(draft.anchor(), m_anchor);
        EXPECT_EQ(draft.caret(), m_caret);
        EXPECT_EQ(draft.canUndo(), m_undo);
        EXPECT_EQ(draft.canRedo(), m_redo);
        const auto composition = draft.composition();
        EXPECT_EQ(composition.text, AStringView(m_preedit.data(), m_preedit.size()));
        EXPECT_EQ(composition.anchor, m_composition.anchor);
        EXPECT_EQ(composition.caret, m_composition.caret);
        EXPECT_EQ(composition.replacementStart, m_composition.replacementStart);
        EXPECT_EQ(composition.replacementEnd, m_composition.replacementEnd);
        EXPECT_EQ(composition.active, m_composition.active);
    }


private:
    AString<Core::Alloc::GlobalArena> m_text;
    AString<Core::Alloc::GlobalArena> m_preedit;
    EditCompositionView m_composition;
    u64 m_generation = 0u;
    u64 m_revision = 0u;
    u64 m_external = 0u;
    u64 m_compositionGeneration = 0u;
    usize m_anchor = 0u;
    usize m_caret = 0u;
    bool m_undo = false;
    bool m_redo = false;
};

template<typename Model>
class NumericModelFixture : public testing::Test{
public:
    NumericModelFixture()
        : m_arena(Name("tests/ui/numeric_models"))
        , m_model(m_arena)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    Model m_model;
};

inline void ExpectRejected(const NumericEditResult& result, const bool restored = false){
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.rejected);
    EXPECT_FALSE(result.committed);
    EXPECT_FALSE(result.valueChanged);
    EXPECT_FALSE(result.cancelled);
    EXPECT_FALSE(result.clamped);
    EXPECT_EQ(result.restored, restored);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

