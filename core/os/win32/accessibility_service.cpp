// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "accessibility_service.h"
#include "accessibility.h"

#include <windows.h>
#include <oleauto.h>
#include <UIAutomation.h>

#include <global/simd/vector.h>

#include <cstdlib>
#include <cwchar>
#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_win32_accessibility{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using UiaReturnRawElementProviderFn = LRESULT (WINAPI*)(HWND, WPARAM, LPARAM, IRawElementProviderSimple*);
using UiaHostProviderFromHwndFn = HRESULT (WINAPI*)(HWND, IRawElementProviderSimple**);
using UiaClientsAreListeningFn = BOOL (WINAPI*)();
using UiaRaiseAutomationEventFn = HRESULT (WINAPI*)(IRawElementProviderSimple*, EVENTID);
using UiaRaiseStructureChangedEventFn = HRESULT (WINAPI*)(IRawElementProviderSimple*, enum StructureChangeType, int*, int);
using UiaRaiseNotificationEventFn = HRESULT (WINAPI*)(IRawElementProviderSimple*, enum NotificationKind, enum NotificationProcessing, BSTR, BSTR);
using UiaDisconnectProviderFn = HRESULT (WINAPI*)(IRawElementProviderSimple*);

static constexpr wchar s_NotificationActivityId[] = L"NWB.UI.Announcement";
inline constexpr Name s_SharedStateArenaName("core/os/win32_uia_state");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SharedUiaState final : NoCopy{
    Alloc::GlobalArena arena;
    Futex mutex;
    Atomic<u32> refCount{ 1u };
    HWND hwnd = nullptr;
    Win32AccessibilityService* owner = nullptr;
    UiaHostProviderFromHwndFn hostProviderFromHwnd = nullptr;
    Vector<AccessibilityNodeData, Alloc::GlobalArena> nodes;
    AString<Alloc::GlobalArena> textStorage;
    u64 focusedId = 0u;
    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;
    b8 alive = true;


public:
    SharedUiaState()
        : arena(s_SharedStateArenaName)
        , nodes(arena)
        , textStorage(arena)
    {}

    void addRef()noexcept{
        refCount.fetch_add(1u, MemoryOrder::relaxed);
    }
    void release()noexcept{
        if(refCount.fetch_sub(1u, MemoryOrder::acq_rel) == 1u)
            delete this;
    }
};

static BSTR AllocBstrFromUtf8(const AStringView text)noexcept{
    if(text.empty())
        return SysAllocStringLen(L"", 0u);
    const i32 wideLength = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<i32>(text.size()), nullptr, 0);
    if(wideLength <= 0)
        return nullptr;
    BSTR result = SysAllocStringLen(nullptr, static_cast<UINT>(wideLength));
    if(!result)
        return nullptr;
    const i32 written = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<i32>(text.size()), result, wideLength);
    if(written != wideLength){
        SysFreeString(result);
        return nullptr;
    }
    return result;
}

static AStringView SliceNodeText(const SharedUiaState& state, const u32 offset, const u32 length)noexcept{
    if(length == 0u)
        return {};
    const usize start = static_cast<usize>(offset);
    const usize count = static_cast<usize>(length);
    if(start > state.textStorage.size() || count > state.textStorage.size() - start)
        return {};
    return AStringView(state.textStorage.data() + start, count);
}

static const AccessibilityNodeData* FindNodeInSnapshot(const SharedUiaState& state, const u64 nodeId)noexcept{
    for(const AccessibilityNodeData& node : state.nodes){
        if(node.id == nodeId)
            return &node;
    }
    return nullptr;
}

static long MapUiaControlType(const AccessibilityRole::Enum role)noexcept{
    switch(role){
    case AccessibilityRole::Window: return UIA_WindowControlTypeId;
    case AccessibilityRole::Dialog: return UIA_WindowControlTypeId;
    case AccessibilityRole::Panel: return UIA_PaneControlTypeId;
    case AccessibilityRole::Group: return UIA_GroupControlTypeId;
    case AccessibilityRole::Region: return UIA_PaneControlTypeId;
    case AccessibilityRole::Text: return UIA_TextControlTypeId;
    case AccessibilityRole::Heading: return UIA_TextControlTypeId;
    case AccessibilityRole::Button: return UIA_ButtonControlTypeId;
    case AccessibilityRole::ToggleButton: return UIA_ButtonControlTypeId;
    case AccessibilityRole::CheckBox: return UIA_CheckBoxControlTypeId;
    case AccessibilityRole::RadioGroup: return UIA_GroupControlTypeId;
    case AccessibilityRole::RadioButton: return UIA_RadioButtonControlTypeId;
    case AccessibilityRole::TextField: return UIA_EditControlTypeId;
    case AccessibilityRole::TextArea: return UIA_DocumentControlTypeId;
    case AccessibilityRole::Slider: return UIA_SliderControlTypeId;
    case AccessibilityRole::SpinButton: return UIA_SpinnerControlTypeId;
    case AccessibilityRole::ProgressBar: return UIA_ProgressBarControlTypeId;
    case AccessibilityRole::ComboBox: return UIA_ComboBoxControlTypeId;
    case AccessibilityRole::List: return UIA_ListControlTypeId;
    case AccessibilityRole::ListItem: return UIA_ListItemControlTypeId;
    case AccessibilityRole::Tree: return UIA_TreeControlTypeId;
    case AccessibilityRole::TreeItem: return UIA_TreeItemControlTypeId;
    case AccessibilityRole::Table: return UIA_TableControlTypeId;
    case AccessibilityRole::TableHeader: return UIA_HeaderItemControlTypeId;
    case AccessibilityRole::TableRow: return UIA_DataItemControlTypeId;
    case AccessibilityRole::TabList: return UIA_TabControlTypeId;
    case AccessibilityRole::Tab: return UIA_TabItemControlTypeId;
    case AccessibilityRole::MenuBar: return UIA_MenuBarControlTypeId;
    case AccessibilityRole::Menu: return UIA_MenuControlTypeId;
    case AccessibilityRole::MenuItem: return UIA_MenuItemControlTypeId;
    case AccessibilityRole::ScrollBar: return UIA_ScrollBarControlTypeId;
    case AccessibilityRole::Splitter: return UIA_ThumbControlTypeId;
    case AccessibilityRole::Link: return UIA_HyperlinkControlTypeId;
    case AccessibilityRole::Image: return UIA_ImageControlTypeId;
    case AccessibilityRole::Canvas: return UIA_CustomControlTypeId;
    case AccessibilityRole::Tooltip: return UIA_ToolTipControlTypeId;
    case AccessibilityRole::Alert: return UIA_TextControlTypeId;
    case AccessibilityRole::Status: return UIA_StatusBarControlTypeId;
    case AccessibilityRole::Toolbar: return UIA_ToolBarControlTypeId;
    case AccessibilityRole::ColorPicker: return UIA_CustomControlTypeId;
    case AccessibilityRole::DockSpace: return UIA_PaneControlTypeId;
    case AccessibilityRole::None:
    case AccessibilityRole::kCount:
    default:
        return UIA_CustomControlTypeId;
    }
}

static enum LiveSetting MapUiaLiveSetting(const AccessibilityLiveMode::Enum mode)noexcept{
    switch(mode){
    case AccessibilityLiveMode::Polite: return Polite;
    case AccessibilityLiveMode::Assertive: return Assertive;
    case AccessibilityLiveMode::Off:
    default:
        return Off;
    }
}

static UiaRect ComputeScreenUiaRect(const SharedUiaState& state, const f32 x, const f32 y, const f32 w, const f32 h)noexcept{
    UiaRect result = {};
    if(!state.hwnd || !IsWindow(state.hwnd))
        return result;
    const SIMDVectorSingle coords = VectorSet(x, y, x + Max(w, 0.0f), y + Max(h, 0.0f));
    const SIMDVectorSingle scaled = VectorMultiply(coords, VectorSet(state.scaleX, state.scaleY, state.scaleX, state.scaleY));
    POINT topLeft = { static_cast<LONG>(VectorGetX(scaled)), static_cast<LONG>(VectorGetY(scaled)) };
    POINT bottomRight = { static_cast<LONG>(VectorGetZ(scaled)), static_cast<LONG>(VectorGetW(scaled)) };
    ClientToScreen(state.hwnd, &topLeft);
    ClientToScreen(state.hwnd, &bottomRight);
    result.left = static_cast<double>(topLeft.x);
    result.top = static_cast<double>(topLeft.y);
    result.width = static_cast<double>(Max<LONG>(bottomRight.x - topLeft.x, 0L));
    result.height = static_cast<double>(Max<LONG>(bottomRight.y - topLeft.y, 0L));
    return result;
}

class Win32UiaRootProvider;
class Win32UiaNodeProvider;

static HRESULT MakeNodeFragment(SharedUiaState* shared, u64 nodeId, IRawElementProviderFragment** outFragment)noexcept;


class Win32UiaNodeProvider final
    : public IRawElementProviderSimple
    , public IRawElementProviderFragment
    , public IInvokeProvider
    , public IToggleProvider
    , public ISelectionItemProvider
    , public IExpandCollapseProvider
{
public:
    Win32UiaNodeProvider(SharedUiaState* const shared, const u64 nodeId)noexcept
        : m_shared(shared)
        , m_nodeId(nodeId)
    {
        m_shared->addRef();
    }
    ~Win32UiaNodeProvider()noexcept{
        if(m_shared){
            m_shared->release();
            m_shared = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)override{
        if(!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;
        if(riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple))
            *ppvObject = static_cast<IRawElementProviderSimple*>(this);
        else if(riid == __uuidof(IRawElementProviderFragment))
            *ppvObject = static_cast<IRawElementProviderFragment*>(this);
        else if(riid == __uuidof(IInvokeProvider))
            *ppvObject = static_cast<IInvokeProvider*>(this);
        else if(riid == __uuidof(IToggleProvider))
            *ppvObject = static_cast<IToggleProvider*>(this);
        else if(riid == __uuidof(ISelectionItemProvider))
            *ppvObject = static_cast<ISelectionItemProvider*>(this);
        else if(riid == __uuidof(IExpandCollapseProvider))
            *ppvObject = static_cast<IExpandCollapseProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef()override{
        return m_refCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release()override{
        const u32 remaining = m_refCount.fetch_sub(1u, MemoryOrder::acq_rel) - 1u;
        if(remaining == 0u)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(enum ProviderOptions* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = static_cast<enum ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID patternId, IUnknown** pRetVal)override;

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        VariantInit(pRetVal);
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;

        switch(propertyId){
        case UIA_ControlTypePropertyId:
            pRetVal->vt = VT_I4;
            pRetVal->lVal = MapUiaControlType(node->role);
            return S_OK;
        case UIA_NamePropertyId: {
            const AStringView name = SliceNodeText(*m_shared, node->nameOffset, node->nameLength);
            if(!name.empty()){
                pRetVal->vt = VT_BSTR;
                pRetVal->bstrVal = AllocBstrFromUtf8(name);
                if(!pRetVal->bstrVal){
                    pRetVal->vt = VT_EMPTY;
                    return E_OUTOFMEMORY;
                }
            }
            return S_OK;
        }
        case UIA_HelpTextPropertyId: {
            const AStringView desc = SliceNodeText(*m_shared, node->descriptionOffset, node->descriptionLength);
            if(!desc.empty()){
                pRetVal->vt = VT_BSTR;
                pRetVal->bstrVal = AllocBstrFromUtf8(desc);
                if(!pRetVal->bstrVal){
                    pRetVal->vt = VT_EMPTY;
                    return E_OUTOFMEMORY;
                }
            }
            return S_OK;
        }
        case UIA_IsEnabledPropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (node->stateBits & AccessibilityStateBits::Enabled) != 0u ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsKeyboardFocusablePropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (node->stateBits & AccessibilityStateBits::Focusable) != 0u ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_HasKeyboardFocusPropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (node->id == m_shared->focusedId || (node->stateBits & AccessibilityStateBits::Focused) != 0u) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsOffscreenPropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (node->stateBits & AccessibilityStateBits::Offscreen) != 0u ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_IsPasswordPropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = (node->stateBits & AccessibilityStateBits::Password) != 0u ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        case UIA_LiveSettingPropertyId:
            pRetVal->vt = VT_I4;
            pRetVal->lVal = static_cast<LONG>(MapUiaLiveSetting(node->liveMode));
            return S_OK;
        case UIA_PositionInSetPropertyId:
            if(node->positionInSet > 0){
                pRetVal->vt = VT_I4;
                pRetVal->lVal = node->positionInSet;
            }
            return S_OK;
        case UIA_SizeOfSetPropertyId:
            if(node->setSize > 0){
                pRetVal->vt = VT_I4;
                pRetVal->lVal = node->setSize;
            }
            return S_OK;
        case UIA_LevelPropertyId:
            if(node->level > 0){
                pRetVal->vt = VT_I4;
                pRetVal->lVal = node->level;
            }
            return S_OK;
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = VARIANT_TRUE;
            return S_OK;
        default:
            return S_OK;
        }
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Navigate(enum NavigateDirection direction, IRawElementProviderFragment** pRetVal)override;

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        SAFEARRAY* const runtimeId = SafeArrayCreateVector(VT_I4, 0, 2);
        if(!runtimeId)
            return E_OUTOFMEMORY;
        i32 data[2] = {
            UiaAppendRuntimeId,
            static_cast<i32>((m_nodeId ^ (m_nodeId >> 32u)) & 0x7FFFFFFFu)
        };
        if(data[1] == 0)
            data[1] = 1;
        for(LONG i = 0; i < 2; ++i){
            if(FAILED(SafeArrayPutElement(runtimeId, &i, &data[i]))){
                SafeArrayDestroy(runtimeId);
                return E_FAIL;
            }
        }
        *pRetVal = runtimeId;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(struct UiaRect* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = {};
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = ComputeScreenUiaRect(*m_shared, node->x, node->y, node->width, node->height);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus()override{
        return enqueueSimpleAction(AccessibilityActionKind::Focus, 0.0);
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)override;

    HRESULT STDMETHODCALLTYPE Invoke()override{
        return enqueueSimpleAction(AccessibilityActionKind::Invoke, 0.0);
    }

    HRESULT STDMETHODCALLTYPE Toggle()override{
        return enqueueSimpleAction(AccessibilityActionKind::Toggle, 0.0);
    }

    HRESULT STDMETHODCALLTYPE get_ToggleState(enum ToggleState* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = (node->stateBits & AccessibilityStateBits::Checked) != 0u ? ToggleState_On : ToggleState_Off;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Select()override{
        return enqueueSimpleAction(AccessibilityActionKind::Select, 0.0);
    }

    HRESULT STDMETHODCALLTYPE AddToSelection()override{
        return enqueueSimpleAction(AccessibilityActionKind::Select, 0.0);
    }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection()override{
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = (node->stateBits & AccessibilityStateBits::Selected) != 0u ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node || node->parentId == 0u)
            return S_OK;
        IRawElementProviderFragment* parentFrag = nullptr;
        const HRESULT hr = MakeNodeFragment(m_shared, node->parentId, &parentFrag);
        if(FAILED(hr) || !parentFrag)
            return hr;
        *pRetVal = static_cast<Win32UiaNodeProvider*>(parentFrag);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Expand()override{
        return enqueueSimpleAction(AccessibilityActionKind::Expand, 0.0);
    }

    HRESULT STDMETHODCALLTYPE Collapse()override{
        return enqueueSimpleAction(AccessibilityActionKind::Collapse, 0.0);
    }

    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(enum ExpandCollapseState* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if((node->stateBits & AccessibilityStateBits::Expanded) != 0u)
            *pRetVal = ExpandCollapseState_Expanded;
        else if((node->stateBits & AccessibilityStateBits::Collapsed) != 0u)
            *pRetVal = ExpandCollapseState_Collapsed;
        else
            *pRetVal = ExpandCollapseState_LeafNode;
        return S_OK;
    }

    [[nodiscard]] HRESULT enqueueSimpleAction(const AccessibilityActionKind::Enum kind, const f64 numericValue)noexcept;


private:
    SharedUiaState* m_shared = nullptr;
    Atomic<u32> m_refCount{ 1u };
    u64 m_nodeId = 0u;
};

class Win32UiaRangeValueTearoff final : public IRangeValueProvider{
public:
    Win32UiaRangeValueTearoff(SharedUiaState* const shared, const u64 nodeId)noexcept
        : m_shared(shared)
        , m_nodeId(nodeId)
    {
        m_shared->addRef();
    }
    ~Win32UiaRangeValueTearoff()noexcept{
        if(m_shared)
            m_shared->release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)override{
        if(!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;
        if(riid == IID_IUnknown || riid == __uuidof(IRangeValueProvider)){
            *ppvObject = static_cast<IRangeValueProvider*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()override{
        return m_refCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release()override{
        const u32 remaining = m_refCount.fetch_sub(1u, MemoryOrder::acq_rel) - 1u;
        if(remaining == 0u)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE SetValue(double val)override;

    HRESULT STDMETHODCALLTYPE get_Value(double* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = node->numericValue;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = ((node->stateBits & AccessibilityStateBits::ReadOnly) != 0u || (node->stateBits & AccessibilityStateBits::Enabled) == 0u) ? TRUE : FALSE;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Maximum(double* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = node->numericMax;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_Minimum(double* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = node->numericMin;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_LargeChange(double* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const f64 span = node->numericMax - node->numericMin;
        *pRetVal = node->numericStep > 0.0 ? node->numericStep * 4.0 : (span > 0.0 ? span * 0.1 : 1.0);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_SmallChange(double* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = node->numericStep > 0.0 ? node->numericStep : 1.0;
        return S_OK;
    }


private:
    SharedUiaState* m_shared = nullptr;
    Atomic<u32> m_refCount{ 1u };
    u64 m_nodeId = 0u;
};

class Win32UiaValueTearoff final : public IValueProvider{
public:
    Win32UiaValueTearoff(SharedUiaState* const shared, const u64 nodeId)noexcept
        : m_shared(shared)
        , m_nodeId(nodeId)
    {
        m_shared->addRef();
    }
    ~Win32UiaValueTearoff()noexcept{
        if(m_shared)
            m_shared->release();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)override{
        if(!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;
        if(riid == IID_IUnknown || riid == __uuidof(IValueProvider)){
            *ppvObject = static_cast<IValueProvider*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef()override{
        return m_refCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release()override{
        const u32 remaining = m_refCount.fetch_sub(1u, MemoryOrder::acq_rel) - 1u;
        if(remaining == 0u)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR val)override{
        if(!val)
            return E_INVALIDARG;
        wchar* endPtr = nullptr;
        const f64 parsed = std::wcstod(val, &endPtr);
        if(endPtr == val)
            return UIA_E_NOTSUPPORTED;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive || !m_shared->owner)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if((node->stateBits & AccessibilityStateBits::Enabled) == 0u || (node->stateBits & AccessibilityStateBits::ReadOnly) != 0u)
            return UIA_E_ELEMENTNOTENABLED;
        if((node->actionBits & AccessibilityActionBits::SetValue) == 0u)
            return UIA_E_NOTSUPPORTED;
        AccessibilityActionEvent ev;
        ev.targetId = m_nodeId;
        ev.kind = AccessibilityActionKind::SetValue;
        ev.numericValue = parsed;
        return m_shared->owner->enqueueActionFromProvider(ev) ? S_OK : UIA_E_NOTSUPPORTED;
    }

    HRESULT STDMETHODCALLTYPE get_Value(BSTR* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if((node->stateBits & AccessibilityStateBits::Password) != 0u){
            *pRetVal = SysAllocStringLen(L"", 0u);
            return *pRetVal ? S_OK : E_OUTOFMEMORY;
        }
        const AStringView valueText = SliceNodeText(*m_shared, node->valueOffset, node->valueLength);
        *pRetVal = AllocBstrFromUtf8(valueText);
        return *pRetVal ? S_OK : E_OUTOFMEMORY;
    }

    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
        if(!node)
            return UIA_E_ELEMENTNOTAVAILABLE;
        *pRetVal = ((node->stateBits & AccessibilityStateBits::ReadOnly) != 0u || (node->stateBits & AccessibilityStateBits::Enabled) == 0u) ? TRUE : FALSE;
        return S_OK;
    }


private:
    SharedUiaState* m_shared = nullptr;
    Atomic<u32> m_refCount{ 1u };
    u64 m_nodeId = 0u;
};

class Win32UiaRootProvider final
    : public IRawElementProviderSimple
    , public IRawElementProviderFragment
    , public IRawElementProviderFragmentRoot
{
public:
    explicit Win32UiaRootProvider(SharedUiaState* const shared)noexcept
        : m_shared(shared)
    {
        m_shared->addRef();
    }
    ~Win32UiaRootProvider()noexcept{
        if(m_shared){
            m_shared->release();
            m_shared = nullptr;
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject)override{
        if(!ppvObject)
            return E_POINTER;
        *ppvObject = nullptr;
        if(riid == IID_IUnknown || riid == __uuidof(IRawElementProviderSimple))
            *ppvObject = static_cast<IRawElementProviderSimple*>(this);
        else if(riid == __uuidof(IRawElementProviderFragment))
            *ppvObject = static_cast<IRawElementProviderFragment*>(this);
        else if(riid == __uuidof(IRawElementProviderFragmentRoot))
            *ppvObject = static_cast<IRawElementProviderFragmentRoot*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef()override{
        return m_refCount.fetch_add(1u, MemoryOrder::relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release()override{
        const u32 remaining = m_refCount.fetch_sub(1u, MemoryOrder::acq_rel) - 1u;
        if(remaining == 0u)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(enum ProviderOptions* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = static_cast<enum ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider([[maybe_unused]] PATTERNID patternId, IUnknown** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID propertyId, VARIANT* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        VariantInit(pRetVal);
        if(propertyId == UIA_ControlTypePropertyId){
            pRetVal->vt = VT_I4;
            pRetVal->lVal = UIA_PaneControlTypeId;
        }
        else if(propertyId == UIA_IsControlElementPropertyId || propertyId == UIA_IsContentElementPropertyId){
            pRetVal->vt = VT_BOOL;
            pRetVal->boolVal = VARIANT_TRUE;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive || !m_shared->hwnd || !m_shared->hostProviderFromHwnd)
            return S_OK;
        return m_shared->hostProviderFromHwnd(m_shared->hwnd, pRetVal);
    }

    HRESULT STDMETHODCALLTYPE Navigate(enum NavigateDirection direction, IRawElementProviderFragment** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        if(direction != NavigateDirection_FirstChild && direction != NavigateDirection_LastChild)
            return S_OK;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if(direction == NavigateDirection_FirstChild){
            for(const AccessibilityNodeData& node : m_shared->nodes){
                if(node.parentId == 0u || !FindNodeInSnapshot(*m_shared, node.parentId))
                    return MakeNodeFragment(m_shared, node.id, pRetVal);
            }
        }
        else{
            for(usize i = m_shared->nodes.size(); i > 0u; --i){
                const AccessibilityNodeData& node = m_shared->nodes[i - 1u];
                if(node.parentId == 0u || !FindNodeInSnapshot(*m_shared, node.parentId))
                    return MakeNodeFragment(m_shared, node.id, pRetVal);
            }
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(struct UiaRect* pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = {};
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive || !m_shared->hwnd || !IsWindow(m_shared->hwnd))
            return S_OK;
        RECT client = {};
        if(GetClientRect(m_shared->hwnd, &client)){
            POINT topLeft = { client.left, client.top };
            POINT bottomRight = { client.right, client.bottom };
            ClientToScreen(m_shared->hwnd, &topLeft);
            ClientToScreen(m_shared->hwnd, &bottomRight);
            pRetVal->left = static_cast<double>(topLeft.x);
            pRetVal->top = static_cast<double>(topLeft.y);
            pRetVal->width = static_cast<double>(Max<LONG>(bottomRight.x - topLeft.x, 0L));
            pRetVal->height = static_cast<double>(Max<LONG>(bottomRight.y - topLeft.y, 0L));
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE SetFocus()override{
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = static_cast<IRawElementProviderFragmentRoot*>(this);
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive || !m_shared->hwnd || !IsWindow(m_shared->hwnd))
            return UIA_E_ELEMENTNOTAVAILABLE;
        POINT clientPt = { static_cast<LONG>(x), static_cast<LONG>(y) };
        if(!ScreenToClient(m_shared->hwnd, &clientPt))
            return S_OK;
        const f32 logicalX = static_cast<f32>(clientPt.x) / Max(m_shared->scaleX, 0.01f);
        const f32 logicalY = static_cast<f32>(clientPt.y) / Max(m_shared->scaleY, 0.01f);
        for(usize i = m_shared->nodes.size(); i > 0u; --i){
            const AccessibilityNodeData& node = m_shared->nodes[i - 1u];
            if((node.stateBits & AccessibilityStateBits::Offscreen) != 0u)
                continue;
            if(logicalX >= node.x && logicalY >= node.y && logicalX < node.x + node.width && logicalY < node.y + node.height)
                return MakeNodeFragment(m_shared, node.id, pRetVal);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** pRetVal)override{
        if(!pRetVal)
            return E_POINTER;
        *pRetVal = nullptr;
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        if(!m_shared->alive)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if(m_shared->focusedId != 0u && FindNodeInSnapshot(*m_shared, m_shared->focusedId))
            return MakeNodeFragment(m_shared, m_shared->focusedId, pRetVal);
        return S_OK;
    }


private:
    SharedUiaState* m_shared = nullptr;
    Atomic<u32> m_refCount{ 1u };
};

static HRESULT MakeNodeFragment(SharedUiaState* const shared, const u64 nodeId, IRawElementProviderFragment** const outFragment)noexcept{
    Win32UiaNodeProvider* const node = new (std::nothrow) Win32UiaNodeProvider(shared, nodeId);
    if(!node)
        return E_OUTOFMEMORY;
    *outFragment = static_cast<IRawElementProviderFragment*>(node);
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32UiaNodeProvider::GetPatternProvider(const PATTERNID patternId, IUnknown** const pRetVal){
    if(!pRetVal)
        return E_POINTER;
    *pRetVal = nullptr;
    const FutexGuard lock = AcquireFutex(m_shared->mutex);
    if(!m_shared->alive)
        return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
    if(!node)
        return UIA_E_ELEMENTNOTAVAILABLE;

    if(patternId == UIA_InvokePatternId && (node->actionBits & AccessibilityActionBits::Invoke) != 0u){
        *pRetVal = static_cast<IInvokeProvider*>(this);
        AddRef();
        return S_OK;
    }
    if(patternId == UIA_TogglePatternId
        && ((node->actionBits & AccessibilityActionBits::Toggle) != 0u || node->role == AccessibilityRole::CheckBox || node->role == AccessibilityRole::ToggleButton)
    ){
        *pRetVal = static_cast<IToggleProvider*>(this);
        AddRef();
        return S_OK;
    }
    if(patternId == UIA_SelectionItemPatternId
        && ((node->actionBits & AccessibilityActionBits::Select) != 0u
            || node->role == AccessibilityRole::RadioButton
            || node->role == AccessibilityRole::ListItem
            || node->role == AccessibilityRole::TreeItem
            || node->role == AccessibilityRole::Tab)
    ){
        *pRetVal = static_cast<ISelectionItemProvider*>(this);
        AddRef();
        return S_OK;
    }
    if(patternId == UIA_ExpandCollapsePatternId
        && ((node->actionBits & (AccessibilityActionBits::Expand | AccessibilityActionBits::Collapse)) != 0u
            || (node->stateBits & (AccessibilityStateBits::Expanded | AccessibilityStateBits::Collapsed)) != 0u)
    ){
        *pRetVal = static_cast<IExpandCollapseProvider*>(this);
        AddRef();
        return S_OK;
    }
    if(patternId == UIA_RangeValuePatternId
        && (node->role == AccessibilityRole::Slider || node->role == AccessibilityRole::SpinButton || node->role == AccessibilityRole::ProgressBar)
    ){
        Win32UiaRangeValueTearoff* const tearoff = new (std::nothrow) Win32UiaRangeValueTearoff(m_shared, m_nodeId);
        if(!tearoff)
            return E_OUTOFMEMORY;
        *pRetVal = static_cast<IRangeValueProvider*>(tearoff);
        return S_OK;
    }
    if(patternId == UIA_ValuePatternId
        && (node->role == AccessibilityRole::TextField || node->role == AccessibilityRole::TextArea || node->role == AccessibilityRole::ComboBox)
    ){
        Win32UiaValueTearoff* const tearoff = new (std::nothrow) Win32UiaValueTearoff(m_shared, m_nodeId);
        if(!tearoff)
            return E_OUTOFMEMORY;
        *pRetVal = static_cast<IValueProvider*>(tearoff);
        return S_OK;
    }
    return S_OK;
}

HRESULT STDMETHODCALLTYPE Win32UiaNodeProvider::Navigate(const enum NavigateDirection direction, IRawElementProviderFragment** const pRetVal){
    if(!pRetVal)
        return E_POINTER;
    *pRetVal = nullptr;
    const FutexGuard lock = AcquireFutex(m_shared->mutex);
    if(!m_shared->alive)
        return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibilityNodeData* const self = FindNodeInSnapshot(*m_shared, m_nodeId);
    if(!self)
        return UIA_E_ELEMENTNOTAVAILABLE;

    const auto logicalParentId = [&](const AccessibilityNodeData& entry)noexcept -> u64{
        if(entry.parentId == 0u || !FindNodeInSnapshot(*m_shared, entry.parentId))
            return 0u;
        return entry.parentId;
    };

    const u64 selfParent = logicalParentId(*self);
    switch(direction){
    case NavigateDirection_Parent:
        if(selfParent == 0u){
            if(!m_shared->owner)
                return S_OK;
            IRawElementProviderSimple* const root = m_shared->owner->rootProvider();
            if(!root)
                return S_OK;
            Win32UiaRootProvider* const rootProv = static_cast<Win32UiaRootProvider*>(root);
            *pRetVal = static_cast<IRawElementProviderFragment*>(rootProv);
            (*pRetVal)->AddRef();
            return S_OK;
        }
        return MakeNodeFragment(m_shared, selfParent, pRetVal);

    case NavigateDirection_FirstChild:
        for(const AccessibilityNodeData& candidate : m_shared->nodes){
            if(candidate.id != m_nodeId && logicalParentId(candidate) == m_nodeId)
                return MakeNodeFragment(m_shared, candidate.id, pRetVal);
        }
        return S_OK;

    case NavigateDirection_LastChild:
        for(usize i = m_shared->nodes.size(); i > 0u; --i){
            const AccessibilityNodeData& candidate = m_shared->nodes[i - 1u];
            if(candidate.id != m_nodeId && logicalParentId(candidate) == m_nodeId)
                return MakeNodeFragment(m_shared, candidate.id, pRetVal);
        }
        return S_OK;

    case NavigateDirection_NextSibling: {
        b8 seenSelf = false;
        for(const AccessibilityNodeData& candidate : m_shared->nodes){
            if(candidate.id == m_nodeId){
                seenSelf = true;
                continue;
            }
            if(seenSelf && logicalParentId(candidate) == selfParent)
                return MakeNodeFragment(m_shared, candidate.id, pRetVal);
        }
        return S_OK;
    }

    case NavigateDirection_PreviousSibling: {
        u64 prevId = 0u;
        for(const AccessibilityNodeData& candidate : m_shared->nodes){
            if(candidate.id == m_nodeId)
                break;
            if(logicalParentId(candidate) == selfParent)
                prevId = candidate.id;
        }
        if(prevId != 0u)
            return MakeNodeFragment(m_shared, prevId, pRetVal);
        return S_OK;
    }

    default:
        return S_OK;
    }
}

HRESULT STDMETHODCALLTYPE Win32UiaNodeProvider::get_FragmentRoot(IRawElementProviderFragmentRoot** const pRetVal){
    if(!pRetVal)
        return E_POINTER;
    *pRetVal = nullptr;
    const FutexGuard lock = AcquireFutex(m_shared->mutex);
    if(!m_shared->alive || !m_shared->owner)
        return UIA_E_ELEMENTNOTAVAILABLE;
    IRawElementProviderSimple* const root = m_shared->owner->rootProvider();
    if(!root)
        return UIA_E_ELEMENTNOTAVAILABLE;
    Win32UiaRootProvider* const rootProv = static_cast<Win32UiaRootProvider*>(root);
    *pRetVal = static_cast<IRawElementProviderFragmentRoot*>(rootProv);
    (*pRetVal)->AddRef();
    return S_OK;
}

HRESULT Win32UiaNodeProvider::enqueueSimpleAction(const AccessibilityActionKind::Enum kind, const f64 numericValue)noexcept{
    const FutexGuard lock = AcquireFutex(m_shared->mutex);
    if(!m_shared->alive || !m_shared->owner)
        return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
    if(!node)
        return UIA_E_ELEMENTNOTAVAILABLE;
    if((node->stateBits & AccessibilityStateBits::Enabled) == 0u)
        return UIA_E_ELEMENTNOTENABLED;
    AccessibilityActionEvent ev;
    ev.targetId = m_nodeId;
    ev.kind = kind;
    ev.numericValue = numericValue;
    return m_shared->owner->enqueueActionFromProvider(ev) ? S_OK : UIA_E_NOTSUPPORTED;
}

HRESULT STDMETHODCALLTYPE Win32UiaRangeValueTearoff::SetValue(const double val){
    const FutexGuard lock = AcquireFutex(m_shared->mutex);
    if(!m_shared->alive || !m_shared->owner)
        return UIA_E_ELEMENTNOTAVAILABLE;
    const AccessibilityNodeData* const node = FindNodeInSnapshot(*m_shared, m_nodeId);
    if(!node)
        return UIA_E_ELEMENTNOTAVAILABLE;
    if((node->stateBits & AccessibilityStateBits::Enabled) == 0u || (node->stateBits & AccessibilityStateBits::ReadOnly) != 0u)
        return UIA_E_ELEMENTNOTENABLED;
    AccessibilityActionEvent ev;
    ev.targetId = m_nodeId;
    ev.kind = AccessibilityActionKind::SetValue;
    ev.numericValue = val;
    return m_shared->owner->enqueueActionFromProvider(ev) ? S_OK : UIA_E_NOTSUPPORTED;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Win32AccessibilityService::Win32AccessibilityService(Alloc::GlobalArena& arena, const NotNull<void*> nativeWindowHandle)
    : TrackedAccessibilityService(arena)
    , m_nativeWindowHandle(nativeWindowHandle)
{
    m_shared = new (std::nothrow) __hidden_win32_accessibility::SharedUiaState();
    if(m_shared){
        m_shared->hwnd = static_cast<HWND>(nativeWindowHandle.get());
        m_shared->owner = this;
        m_rootProvider = new (std::nothrow) __hidden_win32_accessibility::Win32UiaRootProvider(m_shared);
    }

    HMODULE lib = LoadLibraryW(L"uiautomationcore.dll");
    if(lib){
        m_uiaLibrary = static_cast<void*>(lib);
        m_returnRawElementProviderProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaReturnRawElementProvider"));
        m_hostProviderFromHwndProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaHostProviderFromHwnd"));
        m_clientsAreListeningProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaClientsAreListening"));
        m_raiseAutomationEventProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaRaiseAutomationEvent"));
        m_raiseStructureChangedEventProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaRaiseStructureChangedEvent"));
        m_raiseNotificationEventProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaRaiseNotificationEvent"));
        m_disconnectProviderProc = reinterpret_cast<void*>(GetProcAddress(lib, "UiaDisconnectProvider"));
        if(m_shared)
            m_shared->hostProviderFromHwnd = reinterpret_cast<__hidden_win32_accessibility::UiaHostProviderFromHwndFn>(m_hostProviderFromHwndProc);
        m_uiaAvailable = m_returnRawElementProviderProc != nullptr && m_rootProvider != nullptr;
    }
}

Win32AccessibilityService::~Win32AccessibilityService()noexcept{
    if(m_shared){
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        m_shared->alive = false;
        m_shared->owner = nullptr;
        m_shared->nodes.clear();
        m_shared->textStorage.clear();
    }
    if(m_rootProvider){
        if(m_disconnectProviderProc){
            const auto disconnect = reinterpret_cast<__hidden_win32_accessibility::UiaDisconnectProviderFn>(m_disconnectProviderProc);
            disconnect(m_rootProvider);
        }
        m_rootProvider->Release();
        m_rootProvider = nullptr;
    }
    if(m_shared){
        m_shared->release();
        m_shared = nullptr;
    }
    if(m_uiaLibrary){
        FreeLibrary(static_cast<HMODULE>(m_uiaLibrary));
        m_uiaLibrary = nullptr;
    }
}

AccessibilityCapabilities Win32AccessibilityService::capabilities()const noexcept{
    return {
        .tree = m_uiaAvailable,
        .announcements = m_uiaAvailable,
        .actions = m_uiaAvailable,
        .backend = AccessibilityBackend::Win32Uia
    };
}

IRawElementProviderSimple* Win32AccessibilityService::rootProvider()noexcept{
    return m_rootProvider;
}

Optional<isize> Win32AccessibilityService::handleObjectMessage(const usize wParam, const isize lParam)noexcept{
    if(!isOwnerThread() || !m_uiaAvailable || !m_rootProvider || !m_returnRawElementProviderProc)
        return {};
    if(static_cast<long>(lParam) != static_cast<long>(UiaRootObjectId))
        return {};
    const auto returnProvider = reinterpret_cast<__hidden_win32_accessibility::UiaReturnRawElementProviderFn>(m_returnRawElementProviderProc);
    const LRESULT result = returnProvider(
        static_cast<HWND>(m_nativeWindowHandle.get()),
        static_cast<WPARAM>(wParam),
        static_cast<LPARAM>(lParam),
        m_rootProvider
    );
    return Optional<isize>(static_cast<isize>(result));
}

void Win32AccessibilityService::onTreePublished(
    const Span<const AccessibilityNodeData> nodes,
    const AStringView textStorage,
    const u64 focusedId,
    [[maybe_unused]] const u64 version,
    const f32 scaleX,
    const f32 scaleY
){
    if(!m_shared)
        return;

    const u64 previousFocus = m_lastFocusId;
    const usize previousNodeCount = m_lastNodeCount;
    {
        const FutexGuard lock = AcquireFutex(m_shared->mutex);
        m_shared->nodes.clear();
        m_shared->nodes.reserve(nodes.size());
        for(const AccessibilityNodeData& node : nodes)
            m_shared->nodes.push_back(node);
        if(textStorage.empty())
            m_shared->textStorage.clear();
        else
            m_shared->textStorage.assign(textStorage.data(), textStorage.size());
        m_shared->focusedId = focusedId;
        m_shared->scaleX = scaleX;
        m_shared->scaleY = scaleY;
    }
    m_lastFocusId = focusedId;
    m_lastNodeCount = nodes.size();

    if(!m_uiaAvailable || !m_rootProvider || !m_clientsAreListeningProc)
        return;
    const auto clientsListening = reinterpret_cast<__hidden_win32_accessibility::UiaClientsAreListeningFn>(m_clientsAreListeningProc);
    if(!clientsListening())
        return;

    if(previousNodeCount != nodes.size() && m_raiseStructureChangedEventProc){
        const auto raiseStructure = reinterpret_cast<__hidden_win32_accessibility::UiaRaiseStructureChangedEventFn>(m_raiseStructureChangedEventProc);
        raiseStructure(m_rootProvider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
    if(previousFocus != focusedId && focusedId != 0u && m_raiseAutomationEventProc){
        IRawElementProviderFragment* focusedFragment = nullptr;
        if(SUCCEEDED(__hidden_win32_accessibility::MakeNodeFragment(m_shared, focusedId, &focusedFragment)) && focusedFragment){
            const auto raiseEvent = reinterpret_cast<__hidden_win32_accessibility::UiaRaiseAutomationEventFn>(m_raiseAutomationEventProc);
            IRawElementProviderSimple* const simple = static_cast<__hidden_win32_accessibility::Win32UiaNodeProvider*>(focusedFragment);
            raiseEvent(simple, UIA_AutomationFocusChangedEventId);
            focusedFragment->Release();
        }
    }
}

void Win32AccessibilityService::onAnnouncement(const AStringView text, const AccessibilityLiveMode::Enum politeness){
    if(!m_uiaAvailable || !m_rootProvider || !m_clientsAreListeningProc || !m_raiseNotificationEventProc)
        return;
    const auto clientsListening = reinterpret_cast<__hidden_win32_accessibility::UiaClientsAreListeningFn>(m_clientsAreListeningProc);
    if(!clientsListening())
        return;
    BSTR display = __hidden_win32_accessibility::AllocBstrFromUtf8(text);
    BSTR activityId = SysAllocString(__hidden_win32_accessibility::s_NotificationActivityId);
    if(display && activityId){
        const auto raiseNotification = reinterpret_cast<__hidden_win32_accessibility::UiaRaiseNotificationEventFn>(m_raiseNotificationEventProc);
        const enum NotificationProcessing processing = politeness == AccessibilityLiveMode::Assertive
            ? NotificationProcessing_ImportantMostRecent
            : NotificationProcessing_All;
        raiseNotification(m_rootProvider, NotificationKind_Other, processing, display, activityId);
    }
    if(display)
        SysFreeString(display);
    if(activityId)
        SysFreeString(activityId);
}

Optional<isize> DispatchWin32AccessibilityMessage(IAccessibilityService& service, const u32 message, const usize wParam, const isize lParam)noexcept{
    if(message != WM_GETOBJECT || !service.isOwnerThread() || service.capabilities().backend != AccessibilityBackend::Win32Uia)
        return {};
    return MakeNotNull(checked_cast<Win32AccessibilityService*>(&service))->handleObjectMessage(wParam, lParam);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
