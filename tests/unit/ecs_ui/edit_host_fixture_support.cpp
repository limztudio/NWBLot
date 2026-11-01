// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_host_fixture_support.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditHostFixtureSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Ui::ShapedRun, Ui::TextLayoutStatus::Enum> FixtureShaper::shape(const Ui::ShapeRequest& request){
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

b8 PrepareEditView(const Ui::EditModel& model, Ui::TextLayoutBuilder& layoutBuilder, Ui::EditBoxView& view){
    if(!view.snapshot(model))
        return false;
    auto layout = layoutBuilder.layout({ view.displayText() });
    if(!layout)
        return false;
    return view.adoptLayout(Move(*layout));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

