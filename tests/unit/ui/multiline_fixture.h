// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/edit/model.h>
#include <impl/ui/edit/grapheme.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiMultilineTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

class MultilineSnapshot final{
public:
    MultilineSnapshot(Core::Alloc::GlobalArena& arena, const EditModel& model)
        : m_text(arena)
        , m_preedit(arena)
        , m_boundaries(arena)
    {
        if(!model.text().empty())
            m_text.assign(model.text().data(), model.text().size());
        m_boundaries.assign(model.graphemeBoundaries().begin(), model.graphemeBoundaries().end());
        m_generation = model.instanceGeneration();
        m_revision = model.revision();
        m_external = model.externalRevision();
        m_compositionGeneration = model.compositionGeneration();
        m_selectionGeneration = model.selectionGeneration();
        m_anchor = model.anchor();
        m_caret = model.caret();
        m_undo = model.canUndo();
        m_redo = model.canRedo();
        m_mode = model.textMode();
        m_composition = model.composition();
        if(!m_composition.text.empty())
            m_preedit.assign(m_composition.text.data(), m_composition.text.size());
        m_composition.text = {};
    }


public:
    void expectUnchanged(const EditModel& model)const{
        EXPECT_EQ(model.text(), AStringView(m_text.data(), m_text.size()));
        EXPECT_EQ(model.graphemeBoundaries(), m_boundaries);
        EXPECT_EQ(model.instanceGeneration(), m_generation);
        EXPECT_EQ(model.revision(), m_revision);
        EXPECT_EQ(model.externalRevision(), m_external);
        EXPECT_EQ(model.compositionGeneration(), m_compositionGeneration);
        EXPECT_EQ(model.selectionGeneration(), m_selectionGeneration);
        EXPECT_EQ(model.anchor(), m_anchor);
        EXPECT_EQ(model.caret(), m_caret);
        EXPECT_EQ(model.canUndo(), m_undo);
        EXPECT_EQ(model.canRedo(), m_redo);
        EXPECT_EQ(model.textMode(), m_mode);
        const EditCompositionView composition = model.composition();
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
    EditBoundaryVector m_boundaries;
    EditCompositionView m_composition;
    u64 m_generation = 0u;
    u64 m_revision = 0u;
    u64 m_external = 0u;
    u64 m_compositionGeneration = 0u;
    u64 m_selectionGeneration = 0u;
    usize m_anchor = 0u;
    usize m_caret = 0u;
    EditTextMode::Enum m_mode = EditTextMode::SingleLine;
    bool m_undo = false;
    bool m_redo = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MultilineFixture : public testing::Test{
public:
    MultilineFixture()
        : m_arena(Name("tests/ui/multiline"))
        , m_model(m_arena, {}, EditTextMode::Multiline)
    {}


protected:
    Core::Alloc::GlobalArena m_arena;
    EditModel m_model;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

