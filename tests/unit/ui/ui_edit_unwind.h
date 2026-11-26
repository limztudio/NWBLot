// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/edit/integer_model.adhd.h>
#include <impl/ecs_ui/toolkit/edit/model.adhd.h>
#include <tests/common/gtest_sdk.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::UiEditUnwind{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CapturedEditModelState{
    explicit CapturedEditModelState(::ArenaBase& arena)
        : text(arena)
        , boundaries(arena)
        , preedit(arena)
    {}

    AString<::ArenaBase> text;
    Vector<usize, ::ArenaBase> boundaries;
    AString<::ArenaBase> preedit;
    Impl::Ui::TextAreaModelStamp stamp{};
    usize anchor = 0u;
    usize caret = 0u;
    u64 revision = 0u;
    u64 externalRevision = 0u;
    u64 selectionGeneration = 0u;
    u64 compositionGeneration = 0u;
    usize compAnchor = 0u;
    usize compCaret = 0u;
    usize replacementStart = 0u;
    usize replacementEnd = 0u;
    Impl::Ui::EditTextMode::Enum textMode = Impl::Ui::EditTextMode::SingleLine;
    b8 canUndo = false;
    b8 canRedo = false;
    b8 hasSelection = false;
    b8 compositionActive = false;
};

[[nodiscard]] inline CapturedEditModelState CaptureState(::ArenaBase& arena, const Impl::Ui::EditModel& model){
    CapturedEditModelState out(arena);
    const auto text = model.text();
    out.text.assign(text.data(), text.size());
    const auto bounds = model.graphemeBoundaries();
    out.boundaries.assign(bounds.begin(), bounds.end());
    const auto comp = model.composition();
    out.preedit.assign(comp.text.data(), comp.text.size());
    out.stamp = model.stamp();
    out.anchor = model.anchor();
    out.caret = model.caret();
    out.revision = model.revision();
    out.externalRevision = model.externalRevision();
    out.selectionGeneration = model.selectionGeneration();
    out.compositionGeneration = model.compositionGeneration();
    out.compAnchor = comp.anchor;
    out.compCaret = comp.caret;
    out.replacementStart = comp.replacementStart;
    out.replacementEnd = comp.replacementEnd;
    out.textMode = model.textMode();
    out.canUndo = model.canUndo();
    out.canRedo = model.canRedo();
    out.hasSelection = model.hasSelection();
    out.compositionActive = comp.active;
    return out;
}

[[nodiscard]] inline b8 MatchesState(const Impl::Ui::EditModel& model, const CapturedEditModelState& expected){
    const auto bounds = model.graphemeBoundaries();
    if(bounds.size() != expected.boundaries.size())
        return false;
    for(usize i = 0u; i < bounds.size(); ++i){
        if(bounds[i] != expected.boundaries[i])
            return false;
    }
    const auto comp = model.composition();
    return model.text() == AStringView(expected.text)
        && model.stamp() == expected.stamp
        && model.anchor() == expected.anchor
        && model.caret() == expected.caret
        && model.revision() == expected.revision
        && model.externalRevision() == expected.externalRevision
        && model.selectionGeneration() == expected.selectionGeneration
        && model.compositionGeneration() == expected.compositionGeneration
        && model.textMode() == expected.textMode
        && model.canUndo() == expected.canUndo
        && model.canRedo() == expected.canRedo
        && model.hasSelection() == expected.hasSelection
        && comp.active == expected.compositionActive
        && comp.text == AStringView(expected.preedit)
        && comp.anchor == expected.compAnchor
        && comp.caret == expected.compCaret
        && comp.replacementStart == expected.replacementStart
        && comp.replacementEnd == expected.replacementEnd;
}

template<typename SetupFn, typename MutateFn, typename RecoverFn>
[[nodiscard]] inline b8 SweepEditModelOom(const usize minHits, SetupFn&& setup, MutateFn&& mutate, RecoverFn&& recover){
    HeapStorage support;
    usize hits = 0u;
    for(usize failAt = 1u; failAt <= 64u; ++failAt){
        Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
        Impl::Ui::EditLimits limits;
        limits.maxBytes = 4096u;
        limits.maxHistoryRecords = 3u;
        limits.maxHistoryBytes = 700u;
        Impl::Ui::EditModel model(arena, limits, Impl::Ui::EditTextMode::SingleLine);
        if(!setup(arena, model, support.arena))
            return false;

        const CapturedEditModelState before = CaptureState(support.arena, model);
        arena.arm(failAt);
        b8 succeeded = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            succeeded = mutate(model, support.arena);
        }));
        const b8 hit = arena.hit();
        arena.disarm();

        if(!threw){
            EXPECT_FALSE(hit);
            EXPECT_TRUE(succeeded);
            EXPECT_GE(hits, minHits);
            if(hit || !succeeded)
                return false;
            return hits >= minHits;
        }
        if(!hit){
            ADD_FAILURE() << "SweepEditModelOom: threw AllocationException without arena hit at failAt=" << failAt;
            return false;
        }
        ++hits;
        if(!MatchesState(model, before)){
            ADD_FAILURE() << "SweepEditModelOom: model state corrupted after OOM at failAt=" << failAt;
            return false;
        }
        if(!recover(model, support.arena)){
            ADD_FAILURE() << "SweepEditModelOom: recovery failed at failAt=" << failAt;
            return false;
        }
    }
    ADD_FAILURE() << "SweepEditModelOom: exceeded max failAt iterations; hits=" << hits;
    return false;
}

[[nodiscard]] inline b8 EditModelConstructorUnwindCase(){
    Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
    Impl::Ui::EditLimits limits;
    usize hits = 0u;
    for(usize failAt = 1u; failAt <= 16u; ++failAt){
        arena.arm(failAt);
        b8 constructed = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            Impl::Ui::EditModel model(arena, limits, Impl::Ui::EditTextMode::Multiline);
            constructed = model.text().empty() && model.graphemeBoundaries().size() == 1u;
        }));
        const b8 hit = arena.hit();
        arena.disarm();
        if(!threw)
            return !hit && constructed && hits > 0u;
        if(!hit)
            return false;
        ++hits;
    }
    return false;
}

[[nodiscard]] inline b8 ReplaceSelectionRedoAndEvictUnwindCase(){
    return SweepEditModelOom(
        1u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> a(100u, 'a', support);
            AString<::ArenaBase> b(100u, 'b', support);
            AString<::ArenaBase> c(100u, 'c', support);
            if(!model.setText(AStringView(a)))
                return false;
            if(!model.selectAll() || !model.replaceSelection(AStringView(b)))
                return false;
            if(!model.selectAll() || !model.replaceSelection(AStringView(c)))
                return false;
            if(!model.undo() || !model.canUndo() || !model.canRedo())
                return false;
            if(!model.setSelection(4u, 20u))
                return false;
            return model.beginComposition() && model.updateComposition("comp", 1u, 3u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> replacement(260u, 'z', support);
            return model.replaceSelection(AStringView(replacement));
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            const auto expectedBefore = AString<::ArenaBase>(100u, 'b', support);
            const auto expectedUndo = AString<::ArenaBase>(100u, 'a', support);
            AString<::ArenaBase> replacement(260u, 'z', support);
            if(model.text() != AStringView(expectedBefore) || !model.canRedo())
                return false;
            if(!model.replaceSelection(AStringView(replacement)))
                return false;
            if(model.canRedo() || !model.undo() || model.text() != AStringView(expectedBefore))
                return false;
            if(!model.undo() || model.text() != AStringView(expectedUndo) || model.canUndo())
                return false;
            return model.redo() && model.redo() && model.text().size() == (100u - 16u + 260u);
        }
    );
}

[[nodiscard]] inline b8 ReplaceSelectionMaxRecordsEvictUnwindCase(){
    HeapStorage support;
    usize hits = 0u;
    for(usize failAt = 1u; failAt <= 64u; ++failAt){
        Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
        Impl::Ui::EditLimits limits;
        limits.maxBytes = 4096u;
        limits.maxHistoryRecords = 2u;
        limits.maxHistoryBytes = 4096u;
        Impl::Ui::EditModel model(arena, limits, Impl::Ui::EditTextMode::SingleLine);
        if(!model.setText("first") || !model.selectAll() || !model.replaceSelection("second") || !model.setSelection(1u, 4u))
            return false;
        if(!model.beginComposition() || !model.updateComposition("pre", 0u, 2u, Span<const Impl::Ui::EditPreeditClause>{}))
            return false;

        const CapturedEditModelState before = CaptureState(support.arena, model);
        AString<::ArenaBase> big(320u, 'm', support.arena);
        arena.arm(failAt);
        b8 ok = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            ok = model.replaceSelection(AStringView(big));
        }));
        const b8 hit = arena.hit();
        arena.disarm();

        if(!threw)
            return !hit && ok && hits > 0u;
        if(!hit || !MatchesState(model, before))
            return false;
        ++hits;
        if(!model.replaceSelection(AStringView(big)) || !model.undo() || model.text() != "second")
            return false;
    }
    return false;
}

[[nodiscard]] inline b8 EraseAroundSelectionUnwindCase(){
    return SweepEditModelOom(
        1u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> seed(64u, 'k', support);
            return model.setText(AStringView(seed)) && model.setSelection(12u, 20u);
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.eraseAroundSelection(4u, 4u);
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.eraseAroundSelection(4u, 4u) && model.text().size() == 56u && model.undo() && model.text().size() == 64u;
        }
    );
}

[[nodiscard]] inline b8 SetTextExtendedAndSelfAliasedUnwindCases(){
    const b8 extendedOk = SweepEditModelOom(
        1u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.setText("seed") && model.setSelection(1u, 3u) && model.beginComposition() && model.updateComposition("p", 0u, 1u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> longText(512u, 's', support);
            return model.setText(AStringView(longText));
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> longText(512u, 's', support);
            return model.setText(AStringView(longText)) && !model.composition().active;
        }
    );
    const b8 selfAliasedReplaceOk = SweepEditModelOom(
        1u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> seed(128u, 'q', support);
            return model.setText(AStringView(seed)) && model.beginComposition() && model.updateComposition("comp", 1u, 2u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.replaceSelection(model.text());
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.replaceSelection(model.text()) && model.text().size() == 256u && !model.composition().active;
        }
    );
    const b8 selfAliasedSetTextOk = SweepEditModelOom(
        0u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> seed(128u, 'q', support);
            return model.setText(AStringView(seed)) && model.beginComposition() && model.updateComposition("comp", 1u, 2u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.setText(model.text());
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.setText(model.text()) && model.text().size() == 128u && !model.composition().active;
        }
    );
    return extendedOk && selfAliasedReplaceOk && selfAliasedSetTextOk;
}

[[nodiscard]] inline b8 UndoRedoAndCompositionUnwindCases(){
    const b8 compOk = SweepEditModelOom(
        1u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.setText("base") && model.setSelection(1u, 3u) && model.beginComposition() && model.updateComposition("pre", 1u, 2u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> longComp(512u, 'u', support);
            return model.updateComposition(AStringView(longComp), 10u, 20u, Span<const Impl::Ui::EditPreeditClause>{});
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> longComp(512u, 'u', support);
            return model.updateComposition(AStringView(longComp), 10u, 20u, Span<const Impl::Ui::EditPreeditClause>{}) && model.composition().text == AStringView(longComp);
        }
    );

    const b8 undoRedoOk = SweepEditModelOom(
        0u,
        [](Tests::NthFailArena&, Impl::Ui::EditModel& model, ::ArenaBase& support){
            AString<::ArenaBase> first(128u, 'a', support);
            AString<::ArenaBase> second(96u, 'b', support);
            return model.setText(AStringView(first)) && model.selectAll() && model.replaceSelection(AStringView(second));
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return model.undo() && model.redo();
        },
        [](Impl::Ui::EditModel& model, ::ArenaBase&){
            return !model.canRedo() && model.undo() && model.redo() && model.text().size() == 96u;
        }
    );

    return compOk && undoRedoOk;
}

[[nodiscard]] inline b8 IntegerEditModelUnwindCases(){
    HeapStorage support;
    usize draftHits = 0u;
    for(usize failAt = 1u; failAt <= 64u; ++failAt){
        Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
        Impl::Ui::IntegerEditModel model(arena);
        if(!model.setValue(42))
            return false;

        const i64 expectedValue = model.value();
        const b8 expectedDirty = model.dirty();
        const u64 expectedRevision = model.revision();
        const CapturedEditModelState beforeDraft = CaptureState(support.arena, model.draft());

        AString<::ArenaBase> badDraft(120u, 'w', support.arena);
        arena.arm(failAt);
        b8 ok = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            ok = model.setDraft(AStringView(badDraft));
        }));
        const b8 hit = arena.hit();
        arena.disarm();

        if(!threw){
            EXPECT_FALSE(hit);
            EXPECT_TRUE(ok);
            EXPECT_GT(draftHits, 0u);
            if(hit || !ok || draftHits == 0u)
                return false;
            break;
        }
        if(!hit){
            ADD_FAILURE() << "IntegerEditModel setDraft threw without hit at failAt=" << failAt;
            return false;
        }
        ++draftHits;
        if(model.value() != expectedValue || model.dirty() != expectedDirty || model.revision() != expectedRevision){
            ADD_FAILURE() << "IntegerEditModel setDraft scalar mismatch at failAt=" << failAt;
            return false;
        }
        if(!MatchesState(model.draft(), beforeDraft)){
            ADD_FAILURE() << "IntegerEditModel setDraft draft state mismatch at failAt=" << failAt;
            return false;
        }
        if(!model.setDraft(AStringView(badDraft)) || !model.dirty()){
            ADD_FAILURE() << "IntegerEditModel setDraft recover failed at failAt=" << failAt;
            return false;
        }
    }

    usize ctorHits = 0u;
    for(usize failAt = 1u; failAt <= 32u; ++failAt){
        Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
        arena.arm(failAt);
        b8 constructed = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            Impl::Ui::IntegerEditModel model(arena);
            constructed = model.value() == 0 && !model.dirty() && model.draft().text() == "0";
        }));
        const b8 hit = arena.hit();
        arena.disarm();
        if(!threw){
            if(hit || !constructed || ctorHits == 0u)
                return false;
            break;
        }
        if(!hit)
            return false;
        ++ctorHits;
    }

    usize valueCleanRuns = 0u;
    for(usize failAt = 1u; failAt <= 16u; ++failAt){
        Tests::NthFailArena arena(Name("tests/ui/nth_fail"), Name("tests/ui/nth_fail_backing"));
        Impl::Ui::IntegerEditModel model(arena);
        AString<::ArenaBase> dirtyLong(120u, 'x', support.arena);
        if(!model.setDraft(AStringView(dirtyLong)))
            return false;

        const i64 expectedValue = model.value();
        const b8 expectedDirty = model.dirty();
        const u64 expectedRevision = model.revision();
        const CapturedEditModelState beforeDraft = CaptureState(support.arena, model.draft());

        arena.arm(failAt);
        b8 ok = false;
        const b8 threw = static_cast<b8>(ThrowsException<AllocationException>([&](){
            ok = model.setValue(99);
        }));
        const b8 hit = arena.hit();
        arena.disarm();

        if(!threw){
            EXPECT_FALSE(hit);
            EXPECT_TRUE(ok);
            if(hit || !ok)
                return false;
            ++valueCleanRuns;
            break;
        }
        if(!hit)
            return false;
        if(model.value() != expectedValue || model.dirty() != expectedDirty || model.revision() != expectedRevision)
            return false;
        if(!MatchesState(model.draft(), beforeDraft))
            return false;
        if(!model.setValue(99) || model.value() != 99 || model.dirty() || model.draft().text() != "99")
            return false;
    }
    return draftHits > 0u && ctorHits > 0u && valueCleanRuns > 0u;
}

[[nodiscard]] inline b8 EditModelUnwindCases(){
    b8 ctorOk = false;
    {
        ScopedTestTrace trace("ctor");
        ctorOk = EditModelConstructorUnwindCase();
        EXPECT_TRUE(ctorOk);
    }
    b8 redoEvictOk = false;
    {
        ScopedTestTrace trace("redoEvict");
        redoEvictOk = ReplaceSelectionRedoAndEvictUnwindCase();
        EXPECT_TRUE(redoEvictOk);
    }
    b8 recordsEvictOk = false;
    {
        ScopedTestTrace trace("recordsEvict");
        recordsEvictOk = ReplaceSelectionMaxRecordsEvictUnwindCase();
        EXPECT_TRUE(recordsEvictOk);
    }
    b8 eraseAroundOk = false;
    {
        ScopedTestTrace trace("eraseAround");
        eraseAroundOk = EraseAroundSelectionUnwindCase();
        EXPECT_TRUE(eraseAroundOk);
    }
    b8 setTextOk = false;
    {
        ScopedTestTrace trace("setText");
        setTextOk = SetTextExtendedAndSelfAliasedUnwindCases();
        EXPECT_TRUE(setTextOk);
    }
    b8 undoRedoCompOk = false;
    {
        ScopedTestTrace trace("undoRedoComp");
        undoRedoCompOk = UndoRedoAndCompositionUnwindCases();
        EXPECT_TRUE(undoRedoCompOk);
    }
    b8 intModelOk = false;
    {
        ScopedTestTrace trace("intModel");
        intModelOk = IntegerEditModelUnwindCases();
        EXPECT_TRUE(intModelOk);
    }
    return ctorOk && redoEvictOk && recordsEvictOk && eraseAroundOk && setTextOk && undoRedoCompOk && intModelOk;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

