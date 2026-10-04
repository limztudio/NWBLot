// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_scene.h"

#include "../smoke_environment.h"

#include <core/common/log.h>

#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiTextAreaSmokeScene::UiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard)
    : m_clipboard(clipboard)
    , m_longText(arena)
    , m_model(arena, {}, Impl::Ui::EditTextMode::Multiline)
    , m_observed(arena)
    , m_completion(arena)
{
    m_longText.reserve(1044u);
    for(u32 line = 0u; line < 16u; ++line)
        m_longText.append("abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijkl\n");
    m_longText.append("tail");
    resetModel();
}

UiTextAreaSmokeScene::~UiTextAreaSmokeScene(){
    if(m_clipboardToken.valid() && !m_clipboard.cancel(m_clipboardToken))
        TerminateInvariant();
}

bool UiTextAreaSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    if(!drainClipboard())
        return false;
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("text_area_fixture", { 24.0f, 24.0f, 480.0f, 380.0f }))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions controller{ { LayoutSizePolicy::Fixed, 112.0f }, { LayoutSizePolicy::Fixed, 30.0f } };
    if(!ui.label("title", "Multiline plain text", title) || !ui.beginRow("document_actions"))
        return false;
    if(ui.button("reset", "Reset", controller))
        resetModel();
    if(ui.button("long", "Long document", controller))
        longDocument();
    if(ui.button("readonly", m_readOnly ? "Editable" : "Read only", controller))
        m_readOnly = !m_readOnly;
    if(!ui.endContainer() || !ui.beginRow("viewport_actions"))
        return false;
    if(ui.button("enabled", m_enabled ? "Disable" : "Enable", controller))
        m_enabled = !m_enabled;
    if(ui.button("viewport", m_compact ? "Large viewport" : "Small viewport", controller))
        m_compact = !m_compact;
    if(ui.button("clipboard", "Seed CRLF", controller) && !seedClipboard())
        return false;
    if(!ui.endContainer())
        return false;
    TextAreaOptions options;
    options.width = { LayoutSizePolicy::Fixed, m_compact ? 120.0f : 380.0f };
    options.height = { LayoutSizePolicy::Fixed, m_compact ? 72.0f : 160.0f };
    options.enabled = m_enabled;
    options.readOnly = m_readOnly;
    const EditBoxResult result = ui.textArea("document", m_model, m_state, options);
    if(!result.valid)
        return false;
    m_submits += result.submitted ? 1u : 0u;
    m_cancels += result.cancelled ? 1u : 0u;
    m_blurs += result.blurred ? 1u : 0u;
    m_abandons += result.abandoned ? 1u : 0u;
    const WidgetOptions outside{ { LayoutSizePolicy::Fixed, 160.0f }, { LayoutSizePolicy::Fixed, 30.0f } };
    if(ui.button("outside", "Outside focus", outside))
        ++m_outside;
    if(!ui.label("hint", "Enter: LF / Ctrl+Enter: submit") || !ui.endPanel())
        return false;
    if(!observeState(context))
        return false;
    paintMarkers(context);
    return true;
}

void UiTextAreaSmokeScene::resetModel(){
    const bool initialized = m_model.setText("abcdef\nx\nabcdef\n한국어") && m_model.setSelection(5u, 5u);
    GLB_FATAL_ASSERT(initialized);
    m_state.reset();
    m_enabled = true;
    m_readOnly = false;
    m_compact = false;
    m_longDocument = false;
}

void UiTextAreaSmokeScene::longDocument(){
    const bool replaced = m_model.setText(m_longText) && m_model.setSelection(1039u, 1039u);
    GLB_FATAL_ASSERT(replaced);
    m_longDocument = true;
}

bool UiTextAreaSmokeScene::seedClipboard(){
    if(m_clipboardToken.valid() && !m_clipboard.cancel(m_clipboardToken))
        return false;
    m_clipboardToken = {};
    const auto request = m_clipboard.requestWriteText(Core::ClipboardChannel::Clipboard, "ab\r\ncdef\r\nxy");
    if(request.admission != Core::ClipboardAdmission::Accepted || !request.token.valid())
        return false;
    m_clipboardToken = request.token;
    return true;
}

bool UiTextAreaSmokeScene::drainClipboard(){
    if(!m_clipboardToken.valid())
        return true;
    const auto status = m_clipboard.poll(m_clipboardToken, m_completion);
    if(status == Core::ClipboardPollResult::Pending)
        return true;
    if(
        status != Core::ClipboardPollResult::Completed || m_completion.token != m_clipboardToken
        || m_completion.operation != Core::ClipboardOperation::WriteText
        || m_completion.channel != Core::ClipboardChannel::Clipboard || m_completion.status != Core::ClipboardStatus::Success
    )
        return false;
    m_clipboardToken = {};
    ++m_clipboardSeeds;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerTextAreaSmokeEnabled(){ return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXT_AREA"); }

bool IsUiLayerTextAreaSkinSmokeEnabled(){ return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_TEXT_AREA_SKIN"); }

SharedUiTextAreaSmokeScene CreateUiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard){
    return SharedUiTextAreaSmokeScene(
        NewArenaObject<RefCounter<UiTextAreaSmokeScene>>(arena, arena, clipboard),
        ArenaRefDeleter<RefCounter<UiTextAreaSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

