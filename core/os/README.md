# Home OS helpers

`Frame` owns `IClipboardService`. UI/ECS users borrow it after start and keep
only ask tokens. Board asks copy their input, keep first-in-first-out order,
end through `poll`, and can drop with no lent user stores. All home work and
done checks sit on the Frame event thread. At most 32 asks and 16 MiB of
UTF-8 text go in. Text holds no hid NUL and must be good UTF-8. A blank string
works. Board and text-in take-in share `IsValidUtf8TextWithoutNul` from
`global/utf8_text.h`. It turns down NUL, long forms, half pairs, and out-of-set
codes before each queue copies its bytes.

Win32 uses `CF_UNICODETEXT`, turns home CRLF into LF for reads, and makes
board write hold clear through moved global store. The board lane works. Main
pick reports not held.

Linux X11 borrows the set Frame `Display` and sends pick/field events before
plain window input. A closed owner window keeps board and main text. Each
outside read has its own ask window and true server time mark, so dead/stale
notes cannot end another ask. Owners do `TARGETS`, `TIMESTAMP`, `MULTIPLE`,
UTF-8, and Latin-1 `STRING` when they can show it. Big text uses capped ICCCM
`INCR` moves. Live out moves keep fixed bytes through hold loss and fix
ask-side state on new owner asks. Unreadable asks still answer asks in a
fixed order. The owner window and its X11 links free on drop. This keeps live
X11 state off the board ask. X11 asks which toolkits may also serve run in
the ask window thread. Only treated X11 faults show in the run faults. Real
apps own the thread and start/stop rules. Toolkits may not keep ask state in
shared statics. Wayland asks live on the Frame Wayland run queue. Each top
window links its own board state. `wl_data_device_manager` v1 or newer is
needed. With no `zwp_primary_selection_device_manager_v1`, reads and writes
of main pick report not held. Board asks and main pick asks stay split. MIME
texts use the marked UTF-8 form with plain-text fall back. Board reads take
both copy and paste forms. Event-thread asks of the same kind stay in order.
The run queue owns the seat, known MIME set, and last shown pick serial. New
board source state replaces old state before it shows. Stale serials fail, and
broad moves may fix old text. Byte caps are shared on each queue. Mid-offer
faults send fail with no half state held.

Set up `xvfb-run`/Xvfb before setup to turn on the lone pick check entry.
Then run:

```sh
ctest --preset linux-clang-dbg -R '^nwb_os_x11_clipboard_isolated$' --output-on-failure
```

Wayland also needs the set `wayland-client`, `xkbcommon`,
`wayland-scanner`, and `wayland-protocols` files. The plain Linux form finds
them. Pass `-D NWB_ENABLE_WAYLAND=OFF` to the launcher build call for the
X11-only build. Main-pick XML is found apart from xdg-shell. With none, the
core Wayland board stays on and reports main pick not held.

## Text-in runs

`Frame` owns `ITextInputService` next to the board helper. Users borrow the
helper on the home event thread and keep only
`TextInputSessionToken { service, version }`. One helper takes one live run.
`begin` copies a UTF-8 near shot, byte-marked pick, and home-user-pixel mark
box. `updateSurrounding` copies new shots, keeps `InputMethod` vs `Other`
change roots for home feed-back, and bumps a helper count.
`surroundingRevision(token)` lets a hook fence a late home cut with the exact
model shot it sent. `updateCaret` moves home make/candidate spot. Skill flags
note direct puts, pre-show, near-text hold, and home cut split.

`poll` gives owned `Commit`, `Preedit`, `DeleteSurrounding`, or `Cancelled`
notes with no UI call-backs. All text drops hid NUL and uses firm UTF-8.
Pre-show start/mark spots mark its own text in bytes. `caretVisible` keeps a
home hid-mark ask. Cut lengths count bytes near the mark or the outer pick
edges by the note cut base and hold that shot count. An old stated count
fails. It does not cut other model text.

Helpers start with no focus. Frame sends home key focus before plain input
focus send. This holds Win32 `WM_SETFOCUS`/`WM_KILLFOCUS` when wake does not
move. Lost focus voids the live token, drops its queued work, stops home
make, and leaves one end drop note for that token. One poll of it, one end
of it, or start of another run voids it for good. Helper IDs and version
checks turn down strange or old home replies. End/drop voids right before it
calls home clean-up, with re-do drop notes in it.

The queue takes at most 64 notes, 64 KiB per note, and 256 KiB of queued
text. Near shots take 1 MiB. Only side-by-side pre-show notes join. Puts and
cuts stay first-in-first-out edge walls. Count or byte flood drops the half
queue and gives a clear flood drop note, so a user cannot quietly use a cut
edit stream. Bad UTF-8 or off-edge spans fail take-in. Home backs turn bad
send into clear fail drop notes.

Win32 uses IMM32 make/result strings, turns UTF-16 make marks into UTF-8
byte spots, and puts the home make and pick windows at the user-pixel mark.
Live plain `WM_CHAR`/`WM_SYSCHAR` and `WM_UNICHAR` puts go only into the
helper. Rule chars stay with key edit/move rules. UTF-16 half-pair state sits
with the helper for both live edit and the plain scene-char fall back. It
clears at each run/focus edge. The Frame fall back borrows the home decoder
and turns down no-focus char send. So a half scene pair cannot join a char
typed after UI hold moves, or a late char after the run ends.

Linux X11 IME state is a plain XIM front with no state kept past a focus or
run edge. Frame owns the XIM links and the tied input context for each
top-level window. The helper takes the UTF-8 form of made text and home
spot/focus asks. Good char sets and style flags follow the open XIM. XIM open
faults mark the helper down rather than killing setup. An idle XIM check
entry runs under `xvfb-run`. Wayland text-in is an off stub. Start and end
note not-held. Poll gives no notes. Home focus asks still flow, and end/drop
rules stay firm.

## Board and text-in user rules

Borrow helpers only on the Frame event thread. Keep lent helper and token
views short. Never hold a lent view past a Frame state move. Own ask bytes
and result notes. Idle asks may stay owned for the app life. Board reads must
take each `poll` follow-up exactly once. Text-in runs must poll run drops
before they reuse a token. Board and text-in limit faults (`TooManyRequests`,
`TooLarge`, `QueueFull`, `StaleWindow`, `NoDisplay`, `NoSelectionOwner`,
`UnsupportedSelection`, `InvalidUtf8`) read as plain take-in or state faults,
not as card faults. Only the shown `std::system_error` and
`std::runtime_error` forms may cross the public door.
