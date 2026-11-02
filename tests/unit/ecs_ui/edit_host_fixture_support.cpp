// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_host_fixture_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditHostFixtureSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextInputAdmission::Enum FakeTextInput::startNativeSession(const TextInputSessionToken, const TextInputSessionDesc&)noexcept{
    ++starts;
    return TextInputAdmission::Accepted;
}

b8 FakeClipboard::deliver(const ClipboardRequestToken token, const ClipboardStatus::Enum status, const AStringView text){
    if(!completeNativeRequest(token, status, text))
        return false;
    if(status == ClipboardStatus::Success && startedOperation == ClipboardOperation::WriteText && token == startedToken)
        document = startedText;
    return true;
}

void FakeClipboard::startNativeRequest(const ClipboardRequestToken token, const ClipboardOperation::Enum operation, const ClipboardChannel::Enum, const AStringView text){
    startedToken = token;
    startedOperation = operation;
    startedText.assign(text.data(), text.size());
    if(!delayed && !deliver(token, ClipboardStatus::Success, operation == ClipboardOperation::ReadText ? AStringView(document) : AStringView{}))
        ADD_FAILURE() << "Clipboard completion failed";
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

