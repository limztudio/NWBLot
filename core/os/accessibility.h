// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/general.h>

#include <global/expected.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AccessibilityBackend{
    enum Enum : u8{
        Unsupported = 0u,
        Win32Uia,
        LinuxAtSpi
    };
};

namespace AccessibilityRole{
    enum Enum : u8{
        None = 0u,
        Window,
        Dialog,
        Panel,
        Group,
        Region,
        Text,
        Heading,
        Button,
        ToggleButton,
        CheckBox,
        RadioGroup,
        RadioButton,
        TextField,
        TextArea,
        Slider,
        SpinButton,
        ProgressBar,
        ComboBox,
        List,
        ListItem,
        Tree,
        TreeItem,
        Table,
        TableHeader,
        TableRow,
        TabList,
        Tab,
        MenuBar,
        Menu,
        MenuItem,
        ScrollBar,
        Splitter,
        Link,
        Image,
        Canvas,
        Tooltip,
        Alert,
        Status,
        Toolbar,
        ColorPicker,
        DockSpace,
        kCount
    };
};

namespace AccessibilityLiveMode{
    enum Enum : u8{
        Off = 0u,
        Polite,
        Assertive
    };
};

namespace AccessibilityActionKind{
    enum Enum : u8{
        Invoke = 0u,
        Toggle,
        Select,
        Focus,
        Expand,
        Collapse,
        Increment,
        Decrement,
        SetValue,
        ContextMenu
    };
};

namespace AccessibilityStateBits{
    inline constexpr u32 None = 0u;
    inline constexpr u32 Enabled = 1u << 0u;
    inline constexpr u32 Focusable = 1u << 1u;
    inline constexpr u32 Focused = 1u << 2u;
    inline constexpr u32 Checked = 1u << 3u;
    inline constexpr u32 Selected = 1u << 4u;
    inline constexpr u32 Expanded = 1u << 5u;
    inline constexpr u32 Collapsed = 1u << 6u;
    inline constexpr u32 Pressed = 1u << 7u;
    inline constexpr u32 Hovered = 1u << 8u;
    inline constexpr u32 ReadOnly = 1u << 9u;
    inline constexpr u32 Password = 1u << 10u;
    inline constexpr u32 Multiline = 1u << 11u;
    inline constexpr u32 Modal = 1u << 12u;
    inline constexpr u32 HasPopup = 1u << 13u;
    inline constexpr u32 Offscreen = 1u << 14u;
};

namespace AccessibilityActionBits{
    inline constexpr u32 None = 0u;
    inline constexpr u32 Invoke = 1u << 0u;
    inline constexpr u32 Toggle = 1u << 1u;
    inline constexpr u32 Select = 1u << 2u;
    inline constexpr u32 Focus = 1u << 3u;
    inline constexpr u32 Expand = 1u << 4u;
    inline constexpr u32 Collapse = 1u << 5u;
    inline constexpr u32 Increment = 1u << 6u;
    inline constexpr u32 Decrement = 1u << 7u;
    inline constexpr u32 SetValue = 1u << 8u;
    inline constexpr u32 ContextMenu = 1u << 9u;
};

inline constexpr usize s_AccessibilityMaxNodes = 4096u;
inline constexpr usize s_AccessibilityMaxTextBytes = 512u * 1024u;
inline constexpr usize s_AccessibilityMaxActions = 64u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AccessibilityCapabilities{
    b8 tree = false;
    b8 announcements = false;
    b8 actions = false;
    AccessibilityBackend::Enum backend = AccessibilityBackend::Unsupported;
};

struct AccessibilityNodeData{
    u64 id = 0u;
    u64 parentId = 0u;
    AccessibilityRole::Enum role = AccessibilityRole::None;
    AccessibilityLiveMode::Enum liveMode = AccessibilityLiveMode::Off;
    u32 stateBits = 0u;
    u32 actionBits = 0u;
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width = 0.0f;
    f32 height = 0.0f;
    u32 nameOffset = 0u;
    u32 nameLength = 0u;
    u32 descriptionOffset = 0u;
    u32 descriptionLength = 0u;
    u32 valueOffset = 0u;
    u32 valueLength = 0u;
    f64 numericValue = 0.0;
    f64 numericMin = 0.0;
    f64 numericMax = 0.0;
    f64 numericStep = 0.0;
    i32 positionInSet = 0;
    i32 setSize = 0;
    i32 level = 0;
};

struct AccessibilityActionEvent{
    u64 targetId = 0u;
    AccessibilityActionKind::Enum kind = AccessibilityActionKind::Invoke;
    f64 numericValue = 0.0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


interface IAccessibilityService : private NoCopy{
public:
    virtual ~IAccessibilityService()noexcept = default;


public:
    [[nodiscard]] virtual AccessibilityCapabilities capabilities()const noexcept = 0;
    [[nodiscard]] virtual b8 isOwnerThread()const noexcept = 0;
    [[nodiscard]] virtual b8 publishTree(
        Span<const AccessibilityNodeData> nodes,
        AStringView textStorage,
        u64 focusedId,
        u64 version,
        f32 scaleX,
        f32 scaleY
    ) = 0;
    [[nodiscard]] virtual b8 updateFocus(u64 focusedId) = 0;
    [[nodiscard]] virtual b8 announce(AStringView text, AccessibilityLiveMode::Enum politeness) = 0;
    [[nodiscard]] virtual b8 pollAction(AccessibilityActionEvent& outEvent)noexcept = 0;
    [[nodiscard]] virtual u64 publishedVersion()const noexcept = 0;
    [[nodiscard]] virtual usize publishedNodeCount()const noexcept = 0;
    [[nodiscard]] virtual u64 publishedFocusedId()const noexcept = 0;
    [[nodiscard]] virtual usize announcementCount()const noexcept = 0;
};

[[nodiscard]] Expected<GlobalUniquePtr<IAccessibilityService>> CreateAccessibilityService(Alloc::GlobalArena& arena, void* nativeWindowHandle);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
