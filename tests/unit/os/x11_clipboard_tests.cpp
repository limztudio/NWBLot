// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/os/linux/x11/clipboard.h>
#include <tests/common/test_context.h>

#include <global/environment.h>
#include <global/thread.h>

#include <gtest/gtest.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>

#ifdef Success
#undef Success
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_x11_clipboard_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class X11ClipboardFixture : public testing::Test{
protected:
    virtual void SetUp()override{
        if(!EnvironmentVariableEquals(m_arena.arena, "NWB_X11_CLIPBOARD_TEST_ISOLATED", "1"))
            GTEST_SKIP() << "Native clipboard tests require the isolated Xvfb CTest entry";
        for(usize index = 0u; index < m_displays.size(); ++index){
            m_displays[index] = XOpenDisplay(nullptr);
            ASSERT_NE(m_displays[index], nullptr);
            m_services[index] = CreateX11ClipboardService(m_arena.arena, *m_displays[index]);
            ASSERT_TRUE(m_services[index]);
        }
    }

    virtual void TearDown()override{
        for(auto& service : m_services)
            service.reset();
        for(Display*& display : m_displays){
            if(display){
                EXPECT_EQ(XCloseDisplay(display), 0);
                display = nullptr;
            }
        }
    }

    [[nodiscard]] bool pumpAll(){
        for(usize index = 0u; index < m_displays.size(); ++index){
            if(!m_services[index]->pump())
                return false;
            usize dispatched = 0u;
            while(XPending(m_displays[index]) > 0 && dispatched < 256u){
                XEvent event{};
                if(XNextEvent(m_displays[index], &event) != 0)
                    return false;
                if(event.type == SelectionRequest)
                    m_sawSelectionRequest = true;
                if(DispatchX11ClipboardEvent(*m_services[index], event)){
                    ++dispatched;
                    continue;
                }
                ++dispatched;
            }
        }
        return true;
    }

    [[nodiscard]] bool awaitCompletion(IClipboardService& service, const ClipboardRequestToken token, ClipboardCompletion& completion){
        const Timer deadline = TimerAddMS(TimerNow(), 6000u);
        while(TimerNow() < deadline){
            if(!pumpAll())
                return false;
            const ClipboardPollResult::Enum result = service.poll(token, completion);
            if(result == ClipboardPollResult::Completed)
                return true;
            if(result != ClipboardPollResult::Pending)
                return false;
            SleepMS(1u);
        }
        return false;
    }

    [[nodiscard]] bool writeText(const usize index, const ClipboardChannel::Enum channel, const AStringView text){
        ClipboardCompletion completion(m_arena.arena);
        const ClipboardRequestResult request = m_services[index]->requestWriteText(channel, text);
        return
            request.token.valid() && awaitCompletion(*m_services[index], request.token, completion)
            && completion.status == ClipboardStatus::Success
        ;
    }


protected:
    NWB::Tests::TestArena<> m_arena;
    Array<Display*, 2u> m_displays{};
    Array<GlobalUniquePtr<IClipboardService>, 2u> m_services;
    bool m_sawSelectionRequest = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(X11ClipboardFixture, IndependentClipboardAndPrimaryUtf8RoundTrip){
    const AStringView clipboard = "clipboard \xec\x95\x88\xeb\x85\x95 \xf0\x9f\x99\x82";
    const AStringView primary = "primary caf\xc3\xa9";
    ASSERT_TRUE(m_services[0]->capabilities(ClipboardChannel::PrimarySelection).readText);
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, clipboard));
    ASSERT_TRUE(writeText(0u, ClipboardChannel::PrimarySelection, primary));
    ClipboardCompletion completion(m_arena.arena);
    const ClipboardRequestToken clipToken = m_services[1]->requestReadText(ClipboardChannel::Clipboard).token;
    ASSERT_TRUE(awaitCompletion(*m_services[1], clipToken, completion));
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, clipboard);
    const ClipboardRequestToken primaryToken = m_services[1]->requestReadText(ClipboardChannel::PrimarySelection).token;
    ASSERT_TRUE(awaitCompletion(*m_services[1], primaryToken, completion));
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, primary);
}

TEST_F(X11ClipboardFixture, IncrementalReadRetainsSnapshotAfterOwnerLosesSelection){
    AString<Alloc::GlobalArena> original(200000u, 'x', m_arena.arena);
    original.replace(65534u, 1u, "\xf0\x9f\x99\x82");
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, original));
    m_sawSelectionRequest = false;
    ClipboardCompletion completion(m_arena.arena);
    const ClipboardRequestToken token = m_services[1]->requestReadText(ClipboardChannel::Clipboard).token;
    const Timer deadline = TimerAddMS(TimerNow(), 3000u);
    while(!m_sawSelectionRequest && TimerNow() < deadline){
        ASSERT_TRUE(pumpAll());
        SleepMS(1u);
    }
    ASSERT_TRUE(m_sawSelectionRequest);
    const Window replacement = XCreateSimpleWindow(m_displays[1], DefaultRootWindow(m_displays[1]), 0, 0, 1, 1, 0, 0, 0);
    ASSERT_NE(replacement, 0u);
    const Atom clipboard = XInternAtom(m_displays[1], "CLIPBOARD", False);
    XSetSelectionOwner(m_displays[1], clipboard, replacement, CurrentTime);
    XSync(m_displays[1], False);
    ASSERT_EQ(XGetSelectionOwner(m_displays[1], clipboard), replacement);
    ASSERT_TRUE(awaitCompletion(*m_services[1], token, completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, original);
    XDestroyWindow(m_displays[1], replacement);
    XSync(m_displays[1], False);
}

TEST_F(X11ClipboardFixture, CancelledRequestWindowCannotCompleteFreshRequest){
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "first"));
    const ClipboardRequestToken cancelled = m_services[1]->requestReadText(ClipboardChannel::Clipboard).token;
    ASSERT_TRUE(m_services[1]->pump());
    ASSERT_TRUE(m_services[1]->cancel(cancelled));
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "after cancellation"));
    ClipboardCompletion completion(m_arena.arena);
    const ClipboardRequestToken fresh = m_services[1]->requestReadText(ClipboardChannel::Clipboard).token;
    ASSERT_TRUE(awaitCompletion(*m_services[1], fresh, completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, "after cancellation");
    EXPECT_EQ(m_services[1]->poll(cancelled, completion), ClipboardPollResult::InvalidRequest);
}

TEST_F(X11ClipboardFixture, QueuedOwnershipLossCannotClearNewlyReacquiredText){
    ASSERT_TRUE(writeText(0u, ClipboardChannel::Clipboard, "before ownership loss"));
    const Atom clipboard = XInternAtom(m_displays[1], "CLIPBOARD", False);
    const Window temporaryOwner = XCreateSimpleWindow(m_displays[1], DefaultRootWindow(m_displays[1]), 0, 0, 1, 1, 0, 0, 0);
    ASSERT_NE(temporaryOwner, 0u);
    XSetSelectionOwner(m_displays[1], clipboard, temporaryOwner, CurrentTime);
    XSync(m_displays[1], False);
    ASSERT_EQ(XGetSelectionOwner(m_displays[1], clipboard), temporaryOwner);

    ClipboardCompletion completion(m_arena.arena);
    const ClipboardRequestToken write = m_services[0]->requestWriteText(ClipboardChannel::Clipboard, "after reacquisition").token;
    ASSERT_TRUE(m_services[0]->pump());
    bool completed = false;
    const Timer deadline = TimerAddMS(TimerNow(), 3000u);
    while(!completed && TimerNow() < deadline){
        XEvent event{};
        while(XCheckTypedEvent(m_displays[0], PropertyNotify, &event))
            ASSERT_TRUE(DispatchX11ClipboardEvent(*m_services[0], event));
        completed = m_services[0]->poll(write, completion) == ClipboardPollResult::Completed;
        if(!completed)
            SleepMS(1u);
    }
    ASSERT_TRUE(completed);
    ASSERT_EQ(completion.status, ClipboardStatus::Success);
    ASSERT_NE(XGetSelectionOwner(m_displays[1], clipboard), temporaryOwner);
    ASSERT_TRUE(pumpAll());
    const ClipboardRequestToken read = m_services[1]->requestReadText(ClipboardChannel::Clipboard).token;
    ASSERT_TRUE(awaitCompletion(*m_services[1], read, completion));
    EXPECT_EQ(completion.status, ClipboardStatus::Success);
    EXPECT_EQ(completion.text, "after reacquisition");
    XDestroyWindow(m_displays[1], temporaryOwner);
    XSync(m_displays[1], False);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

