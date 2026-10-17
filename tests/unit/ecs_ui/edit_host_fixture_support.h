// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/edit_box_host.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditHostFixtureSupport{


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;


class FixtureShaper final : public Ui::ITextShaper{
public:
    explicit FixtureShaper(Alloc::GlobalArena& arena)noexcept
        : m_arena(arena)
    {}
    virtual ~FixtureShaper()noexcept override = default;


public:
    [[nodiscard]] virtual Expected<Ui::ShapedRun, Ui::TextLayoutStatus::Enum> shape(const Ui::ShapeRequest& request)override{
        Ui::ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        usize begin = 0u;
        while(begin < request.text.size()){
            usize end = begin + 1u;
            while(end < request.text.size() && IsUtf8Continuation(static_cast<u8>(request.text[end])))
                ++end;
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { 10.0f, 0.0f }, {} });
            begin = end;
        }
        return run;
    }


private:
    Alloc::GlobalArena& m_arena;
};


[[nodiscard]] inline bool PrepareEditView(Ui::EditBoxView& view, const Ui::EditModel& model, Ui::TextLayoutBuilder& layoutBuilder){
    if(!view.snapshot(model))
        return false;
    auto layout = layoutBuilder.layout({ view.displayText() });
    if(!layout)
        return false;
    return view.adoptLayout(Move(*layout));
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

