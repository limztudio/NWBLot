# UI GPU draw

`nwb_ui_gpu` draws a still `Ui::DrawSnapshot` into a one-shot,
full-out-size `RGBA16_FLOAT` shot. It borrows `GraphicsRuntime`,
`AssetManager`, a shader path fix, and the caller `GlobalArena`. It has no
ECS and no ImGui need.

Call `validateResources(width, height)` once the card swap-chain frame
buffers live. Then call `setSkin(identity, loadedSkin, skinGeneration)`.
Store check makes the show pipe from that frame-buffer fit. It keeps four
CPU frame slots for the three GPU slots plus a held set. `setSkin` can also
bind a next skin set with no GPU idle wait. It loads and checks the named
face. It makes its true GPU shot and tag. It saves the load true still
up-load done token. Skin name and set must match the shot sent by
`submit(Move(snapshot))`. Note names name notes. The grabbed store form
owns the fixed GPU shot.

Each new mark gets a graph-owned clear from its true raw home state. Then
a main Graphics done in `ShaderResource` runs. Then its free read tag
goes live. The mark form saves that exact took start token. Each draw
graph brings it in as ready. Work sent to a next real queue thus also
waits for start. Set and half-set fails keep home shot links through took
GPU work.

At most one CPU set can hold. `submit` turns down a next held frame. It
does not move its shot. `hasPendingFrame()` lets the caller keep that set
for retry. Send reuses a kept frame slot only past took-work done and
graph pack free of CPU hold. The draw has three mark/box slots. Ready
makes or grows shape boxes first, then takes a frame only when a slot is
free. When all slots stay in flight, ready wins with no stated coat. It
keeps the held shot. No frame waits for GPU idle.

`prepareTaskGraphOutputLayer(acquired)` grabs the exact got frame.
`declareTaskGraphOutputLayer(graph)` states graph-owned shape up-loads, a
clear mark, and `ui.raster` for a drawn frame. The draw makes a clear
color store form. It has no scene need, so the graph can plan it next to
scene work. The end out user needs `readyTask`. It reads `colorVersion`.
It reads `sampledImage` first, then out move code. A blank scene-UI shot
states no UI graph work and no color store. It gives back only
`frameGeneration`. The took end user still frees that exact shot. A
turned-down graph leaves the set held for retry. Only took end calls
`acceptTaskGraphOutputLayer(frameGeneration, token)`.

Each graph pack owns the still shot. It owns the true skin, mark, shape,
pipe, sampler, and tag forms it reads. Tag slots stay held through those
holds. Home heap bind notes sent tag use. No long-held world held-write
lease lives. Draw take does not eat the CPU frame. The draw saves the
exact took point-up, mark-up, clear, draw, and end-user tokens. Slot reuse
needs end use plus took-work done. A turned-down graph keeps its raw
source. Rebuild past a took head waits (no block) for those head tokens
first, then reuses its stores. A half head cannot show an old got set to
a new end user.

Spots use the shot true units and saved pixel scales. Point RGB is
pre-mixed straight Rec.709. The skin shader reads the atlas at base mip
with a straight edge sampler. Face RGB is already straight from off-line
make. Read plain alpha joins pre-mixed tint. The draw mixes source-one
over one-minus-source-alpha. It keeps the coat straight. The stock skin
and made skins share this path. The end mix puts SDR face code or HDR10
code in once. UI-only HDR uses the shared 203-nit white-page help.

`renderStandalone(acquired)` gives UI-only show over black. It uses the
same coat and an exact brought-in got back-buffer with a graph show end.
A blank sent shot still clears a true clear coat. It puts out set black.
A frame owned by the scene graph skips lone use. Took use leaves no held
work. Only the exact got name took in can skip out with no held work.
Lone use turns down a new got shot with no shot, a full slot, or a half
retry head. An opt show helper readies first, then states past `ui.output`.
Its work is trailed by a main Graphics done tail used as the show end. UI
slot free still uses the exact took out-user token. It does not lean on
that next show tail. `UiLayerSystem` picks `Scene` or `Standalone` plan.
Its now draw-pass call cannot drop got show on a turned-down lone send.
So that fit asks for card remake, not more show with vague hold.

The caller must stop makers first. It must join graph/home GPU work. It
must free old graph packs. Do this first, then `invalidateResources()`.
Do this first, then kill the card or caller field. Size/card remake uses
that same joined drop/make edge. Main-thread set/send calls give no
side-by-side maker take-in.

Draw and out draws take straight home write and call IR replay. Graph
store brings, forms, walls, up-loads, clears, and `ui.raster`/`ui.output`
task times feed graph grab and speed notes. Lone show also writes the
`render.frame` time span round its graph work. Tools, kept touch state,
text shape/fonts, point/key flow, and OS IME/board borrow live in split
fields past this draw.
