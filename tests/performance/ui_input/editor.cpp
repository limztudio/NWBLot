// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "benchmark.h"

#include <impl/ecs_ui/edit_box_host.h>

#include <core/os/clipboard_service.h>
#include <core/os/text_input_service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_BEGIN
namespace __hidden_ui_editor_benchmark{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

class BenchmarkShaper final : public ITextShaper{
public:
    explicit BenchmarkShaper(Alloc::GlobalArena& arena)noexcept : m_arena(arena){}


public:
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override{
        ShapedRun run(m_arena);
        run.metrics = { 8.0f, 2.0f, 2.0f };
        run.glyphs.reserve(request.text.size());
        for(usize byte = 0u; byte < request.text.size(); ++byte)
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(byte), static_cast<u32>(byte + 1u), {}, { 10.0f, 0.0f }, {} });
        return run;
    }


private:
    Alloc::GlobalArena& m_arena;
};

class BenchmarkTextInput final : public QueuedTextInputService{
public:
    explicit BenchmarkTextInput(Alloc::GlobalArena& arena) : QueuedTextInputService(arena){}


public:
    [[nodiscard]] virtual TextInputCapabilities capabilities()const noexcept override{ return { true, true, true, true }; }
};

class BenchmarkClipboard final : public QueuedClipboardService{
public:
    explicit BenchmarkClipboard(Alloc::GlobalArena& arena) : QueuedClipboardService(arena){}


public:
    [[nodiscard]] virtual ClipboardCapabilities capabilities(ClipboardChannel::Enum)const noexcept override{ return {}; }


protected:
    virtual void startNativeRequest(const ClipboardRequestToken token, ClipboardOperation::Enum, ClipboardChannel::Enum, AStringView)noexcept override{
        const bool completed = completeNativeRequest(token, ClipboardStatus::Unsupported);
        NWB_FATAL_ASSERT(completed);
    }
};

class PublicationCase final{
public:
    explicit PublicationCase(const usize stops)
        : m_arena(Name("tests/performance/ui_input/publication"))
        , m_context(m_arena)
        , m_textInput(m_arena)
        , m_clipboard(m_arena)
        , m_host(m_arena, m_context, m_textInput, m_clipboard)
        , m_model(m_arena, { stops, 0u, 0u })
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
        , m_view(m_arena)
        , m_stops(stops)
    {}


public:
    [[nodiscard]] bool setup(){
        AString<Alloc::GlobalArena> text(m_arena);
        text.assign(m_stops - 1u, 'a');
        if(!m_model.setText(text) || !m_model.setSelection(0u, 0u) || !m_view.snapshot(m_model))
            return false;
        auto layout = m_layoutBuilder.layout({ m_view.displayText() });
        if(!layout || !m_view.adoptLayout(Move(*layout)))
            return false;
        const auto placement = m_view.arrange({ 10.0f, 20.0f, 320.0f, 30.0f }, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, {}, 1.0f, false);
        if(!placement || m_view.caretStops().size() != m_stops)
            return false;
        m_placement = *placement;
        return true;
    }

    [[nodiscard]] bool operation(){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        const auto widget = m_context.declare("edit", WidgetKind::EditBox);
        if(!widget)
            return false;
        m_widget = **widget;
        EditBoxOptions options;
        options.readOnly = true;
        if(!m_host.edit(m_widget, m_model, options).valid || !m_host.publish(m_widget, m_view, m_placement, options))
            return false;
        HitTarget target;
        target.rectangle = m_placement.bounds;
        target.clip = m_placement.frameClip;
        target.focusable = true;
        target.textEditable = true;
        if(!m_context.addTarget(m_widget, target) || !m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        if(!m_context.commitFrame(m_generation))
            return false;
        m_host.commitFrame(m_generation);
        return true;
    }

    [[nodiscard]] Expected<u64> checksum()const noexcept{
        const HitTarget* target = m_context.input().findTarget(m_widget.id, m_widget.declarationGeneration);
        if(m_context.input().layoutGeneration() != m_generation || !target || target->rectangle.width != 320.0f || m_host.hasTextFocus() || !m_view.ready())
            return MakeUnexpected(Failure{});
        u64 hash = MixChecksum(1469598103934665603u, m_view.committedBytes());
        for(const EditBoxCaretStop& stop : m_view.caretStops()){
            hash = MixChecksum(hash, stop.committedByte);
            hash = MixChecksum(hash, static_cast<u64>(stop.x));
        }
        return hash;
    }


private:
    Alloc::GlobalArena m_arena;
    Context m_context;
    BenchmarkTextInput m_textInput;
    BenchmarkClipboard m_clipboard;
    UiEditBoxHost m_host;
    EditModel m_model;
    BenchmarkShaper m_shaper;
    TextLayoutBuilder m_layoutBuilder;
    EditBoxView m_view;
    EditBoxPlacement m_placement;
    WidgetState m_widget;
    const usize m_stops;
    u64 m_generation = 0u;
};

class EmptySelectionCase final{
public:
    explicit EmptySelectionCase(const usize lines)
        : m_arena(Name("tests/performance/ui_input/empty_selection"))
        , m_skin(m_arena, Name("tests/performance/ui_input/skin"))
        , m_model(m_arena, { lines, 0u, 0u }, EditTextMode::Multiline)
        , m_shaper(m_arena)
        , m_layoutBuilder(m_arena, m_shaper)
        , m_view(m_arena)
        , m_text(m_arena)
        , m_paint(m_arena)
        , m_snapshot(m_arena)
        , m_lines(lines)
    {}


public:
    [[nodiscard]] bool setup(){
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Assets::AssetRef<Texture>("tests/performance/ui_input/texture"), 8u, 8u, 1.0f, Move(regions));
        AString<Alloc::GlobalArena> text(m_arena);
        text.assign(m_lines - 1u, '\n');
        if(!m_model.setText(text) || !m_model.setSelection(0u, 0u) || !m_view.snapshot(m_model))
            return false;
        auto layout = m_layoutBuilder.layout({ m_view.displayText() });
        if(!layout || !m_view.adoptLayout(Move(*layout)))
            return false;
        const auto placement = m_view.arrange({ 10.0f, 10.0f, 300.0f, 100.0f }, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, {}, 1.0f, false);
        if(!placement || m_view.layout().lines().size() != m_lines || !m_view.layout().glyphs().empty())
            return false;
        m_placement = *placement;
        return true;
    }

    [[nodiscard]] bool operation(){
        m_paint.begin({ 500.0f, 200.0f }, 1u, 1u, Assets::AssetRef<UiSkin>("tests/performance/ui_input/skin"), m_skin);
        if(!m_view.paint(m_text, m_paint, m_skin, m_placement))
            return false;
        m_snapshot = m_paint.freeze();
        return m_snapshot.commands().size() == 1u && m_snapshot.indices().size() == 6u;
    }

    [[nodiscard]] Expected<u64> checksum()const noexcept{
        if(m_view.selectionRange().begin != m_view.selectionRange().end || m_snapshot.vertices().size() != 4u || m_snapshot.commands().size() != 1u)
            return MakeUnexpected(Failure{});
        u64 hash = MixChecksum(1469598103934665603u, m_lines);
        for(const Vertex& vertex : m_snapshot.vertices()){
            hash = MixChecksum(hash, static_cast<u64>(vertex.position.x));
            hash = MixChecksum(hash, static_cast<u64>(vertex.position.y));
        }
        return hash;
    }


private:
    Alloc::GlobalArena m_arena;
    UiSkin m_skin;
    EditModel m_model;
    BenchmarkShaper m_shaper;
    TextLayoutBuilder m_layoutBuilder;
    EditBoxView m_view;
    EditBoxPlacement m_placement;
    TextService m_text;
    PaintBuilder m_paint;
    DrawSnapshot m_snapshot;
    const usize m_lines;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RunEditBenchmarks(){
    constexpr usize s_StopCounts[]{ 256u, 4096u, 16384u };
    constexpr AStringView s_PublicationNames[]{ "editor.publish.256", "editor.publish.4096", "editor.publish.16384" };
    constexpr usize s_LineCounts[]{ 100u, 1000u, 10000u };
    constexpr AStringView s_SelectionNames[]{ "editor.empty_selection.100", "editor.empty_selection.1000", "editor.empty_selection.10000" };
    for(usize index = 0u; index < 3u; ++index){
        __hidden_ui_editor_benchmark::PublicationCase publication(s_StopCounts[index]);
        if(!publication.setup() || !publication.operation() || !publication.checksum())
            return false;
        if(!Measure(s_PublicationNames[index], s_StopCounts[index], [&publication](){ return publication.operation(); }, [&publication]()noexcept{ return publication.checksum(); }))
            return false;
        __hidden_ui_editor_benchmark::EmptySelectionCase selection(s_LineCounts[index]);
        if(!selection.setup() || !selection.operation() || !selection.checksum())
            return false;
        if(!Measure(s_SelectionNames[index], s_LineCounts[index], [&selection](){ return selection.operation(); }, [&selection]()noexcept{ return selection.checksum(); }))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UI_INPUT_BENCHMARK_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

