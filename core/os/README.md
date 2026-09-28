# Native UI services

`Frame` owns `IClipboardService`; UI/ECS clients borrow it after startup and keep only request tokens. Clipboard requests copy their input, preserve FIFO ordering, complete through `poll`, and can be cancelled without borrowing client buffers. All native work and completion polling belong to the Frame event thread. Up to 32 requests and 16 MiB of UTF-8 text are admitted. Text contains no embedded NUL and must be valid UTF-8; an empty string is supported.

Win32 uses `CF_UNICODETEXT`, normalizes native CRLF to LF for reads, and makes clipboard write ownership explicit through movable global memory. The clipboard channel is supported; primary selection is reported unsupported.

Linux X11 borrows the existing Frame `Display` and forwards selection/property events before ordinary window input. A private owner window retains clipboard and primary text. Every external read has a unique request window and actual server timestamp, so cancelled/stale notifications cannot complete another request. Owners implement `TARGETS`, `TIMESTAMP`, `MULTIPLE`, UTF-8, and Latin-1 `STRING` when representable. Large text uses bounded ICCCM `INCR` transfers. Active outgoing transfers keep immutable bytes through ownership loss and restore requestor event masks when finished. Foreign window errors are checked. Reads and transfers time out after five seconds; at most eight outgoing transfers are active.

Linux Wayland borrows the Frame display and selected seat. It binds the core data-device manager and uses nonblocking, bounded reads and writes through native file descriptors. The receiving pipe leaves the foreign source's write endpoint blocking for toolkit interoperability. Owned source text and active transfers have separate lifetimes. Clipboard writes require keyboard focus and a serial from a real button/key event. Focus loss, keyboard capability loss, and seat removal clear eligibility; same-seat manager hotplug preserves focus while requiring a fresh input serial. Selection offers received before keyboard enter are retained. Primary selection is supported only when its protocol XML was available at build time and the compositor advertises `zwp_primary_selection_device_manager_v1`; otherwise capabilities report unsupported.

Both backends finish without nested native event loops or shell commands. Protocol and pipe progress is pumped during Frame updates, with a 64 KiB budget per pipe transfer and five-second deadlines. IME composition and widget edit/selection policy remain separate follow-up work; the OS service owns only native clipboard and input eligibility.

The common async queue and UTF-8 tests run on Windows and Linux. Linux pipe tests cover descriptor modes, partial transfer, EOF, backpressure, closed readers, and writer reuse. X11 integration tests are skipped in normal runs and never touch a logged-in user's selection. When `xvfb-run` is installed, CTest adds `nwb_os_x11_clipboard_isolated`, which runs them against a temporary isolated Xvfb display. They cover Unicode clipboard/primary round trips, ownership loss during `INCR`, cancellation followed by a fresh request, and a queued ownership-loss event delivered after reacquisition. Wayland live qualification requires a Linux compositor and is not claimed from this Windows development host.

On a Linux host with the repository toolchain and native X11 development packages installed, run:

```sh
cmake --preset linux-clang-x64
cmake --build --preset linux-clang-dbg --target nwb_os_tests
ctest --preset linux-clang-dbg -R '^nwb_os_tests$' --output-on-failure
```

Install `xvfb-run`/Xvfb before configuring to enable the isolated selection integration entry, then run:

```sh
ctest --preset linux-clang-dbg -R '^nwb_os_x11_clipboard_isolated$' --output-on-failure
```

Wayland additionally needs the existing `wayland-client`, `xkbcommon`, `wayland-scanner`, and `wayland-protocols` dependencies. The normal Linux preset discovers them; use `-DNWB_ENABLE_WAYLAND=OFF` at configure time for the X11-only build. Primary-selection XML is discovered separately from xdg-shell; its absence keeps the core Wayland clipboard enabled and reports primary selection unsupported.
