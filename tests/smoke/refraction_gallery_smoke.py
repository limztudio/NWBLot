#!/usr/bin/env python3
"""Capture actual complex-refraction cases and package an offline comparison gallery."""

import argparse
import base64
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import zlib

from window_capture_smoke import (
    SKIP_EXIT_CODE, SmokeFailure, analyze_rgb_rows, read_bmp_24_rows,
)


CASES = {
    "single": ("Single solid", "A single closed glass volume in front of the stripe wall.",
        "The baseline supported case: one primary entry and exit. Compare the bent background with the straight red foreground panel."),
    "separate": ("Separate objects", "Distinct glass objects with separate visible silhouettes.",
        "Each pixel can retain its own nearest refractor. Spatially separate objects can therefore use different primary interfaces."),
    "stacked": ("Depth stack", "Multiple glass objects placed one behind another along the view.",
        "The first refractor is retained. Further refractive interfaces along the transmitted ray trigger the bounded screen-space fallback; a full chain of volumes is not traced."),
    "intersecting": ("Intersecting volumes", "Glass objects whose solid volumes overlap.",
        "Overlapping volumes do not have a tracked medium stack. When another instance interrupts the selected volume's exit path, the hardware resolver falls back per pixel."),
    "nested": ("Nested volumes", "One glass object contained inside another.",
        "Nested media are outside the first implementation's complete optical model. Encountering the inner instance can trigger the screen-space fallback."),
    "coincident": ("Coincident surfaces", "Separate glass instances occupying the same surface positions.",
        "Equal-depth interfaces are ambiguous. The retained primary can depend on draw or intersection ordering; use the geometry preview to inspect the arrangement."),
    "coincident_tinted": ("Coincident tinted glass", "Coincident glass instances with different authored optical tints.",
        "The renderer retains one primary optical interface. Coincident tint selection and remaining AVBOIT layers are approximations, so this is an ambiguity probe rather than a reference solution."),
    "torus": ("Concave torus", "A closed ring with a visible hole and a concave inner surface.",
        "Entry and exit queries handle the selected local volume segment. A bent ray can hit the same ring again; additional glass encounters take the bounded fallback."),
    "same_mesh": ("Disconnected components", "Multiple disconnected glass components inside one mesh instance.",
        "The primary identity is an instance, not a connected-volume label. This case exposes the limits of instance-based exclusion and continuation across disconnected surfaces."),
    "prism": ("Faceted prism", "A glass prism with planar faces and sharp changes in surface normal.",
        "The selected entry and exit faces bend the ray using their normals. Internal reflection is capped, so sharp-angle regions can lose unresolved reflected energy. This angle-stress scene does not verify that the total-internal-reflection branch executed."),
}

VARIANTS = ("geometry", "automatic", "screen", "disabled")
VARIANT_LABELS = {
    "geometry": "Geometry preview — translucent, refraction off", "automatic": "Automatic tracing",
    "screen": "Screen-space fallback", "disabled": "Refraction disabled",
}
COMMON_LIMIT = (
    "Current implementation: one primary refractor per pixel, bounded hardware continuation, "
    "and a screen-space fallback for unsupported interface sequences. A hardware dispatch can "
    "still fall back on individual pixels. These are real renderer outputs; the gallery measures "
    "visible changes and exposes limitations without certifying complex optical accuracy."
)


def parse_selection(text, allowed, option):
    selected = tuple(part.strip() for part in text.split(",") if part.strip())
    if not selected:
        raise argparse.ArgumentTypeError(f"{option} must select at least one item")
    invalid = [item for item in selected if item not in allowed]
    if invalid:
        raise argparse.ArgumentTypeError(f"unknown {option}: {', '.join(invalid)}; choices: {', '.join(allowed)}")
    if len(set(selected)) != len(selected):
        raise argparse.ArgumentTypeError(f"duplicate {option} selections are not allowed")
    return selected


def capture_environment(case, variant, inherited=None):
    env = dict(os.environ if inherited is None else inherited)
    for name in (
        "NWB_REFRACTION_SMOKE_CASE", "NWB_REFRACTION_SMOKE_GEOMETRY",
        "NWB_REFRACTION_SMOKE_ENABLED", "NWB_REFRACTION_SMOKE_HARDWARE",
        "NWB_RENDERER_BASELINE_CAPTURE_FREEZE_FRAME", "NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS",
        "NWB_SMOKE_FRAMEBUFFER_CAPTURE_PATH", "NWB_SMOKE_FRAMEBUFFER_CAPTURE_FRAME_COUNT",
    ):
        env.pop(name, None)
    env["NWB_REFRACTION_SMOKE_CASE"] = case
    env["NWB_REFRACTION_SMOKE_GEOMETRY"] = "1" if variant == "geometry" else "0"
    env["NWB_REFRACTION_SMOKE_ENABLED"] = "0" if variant in ("geometry", "disabled") else "1"
    env["NWB_REFRACTION_SMOKE_HARDWARE"] = "0" if variant == "screen" else "1"
    env["NWB_RENDERER_BASELINE_FIXED_DELTA_SECONDS"] = "0.016666667"
    return env


def frame_difference(reference, output):
    if reference[:2] != output[:2]:
        raise SmokeFailure("gallery captures have different framebuffer dimensions")
    width, height, reference_rows = reference
    changed = 0
    channel_sum = 0
    maximum = 0
    for before_row, after_row in zip(reference_rows, output[2]):
        for before, after in zip(before_row, after_row):
            deltas = tuple(abs(a - b) for a, b in zip(before, after))
            largest = max(deltas)
            changed += largest > 8
            channel_sum += sum(deltas)
            maximum = max(maximum, largest)
    pixels = width * height
    return {
        "compared_with": "disabled", "changed_pixels": changed, "total_pixels": pixels,
        "changed_fraction": changed / pixels, "mean_absolute_rgb_difference": channel_sum / (pixels * 3),
        "maximum_channel_difference": maximum,
    }


def validate_frame(frame, case, variant):
    width, height, rows = frame
    if (width, height) != (960, 720):
        raise SmokeFailure(f"{case}/{variant}: expected 960x720 framebuffer, got {width}x{height}")
    analysis = analyze_rgb_rows(rows)
    if not analysis.has_pixel_variation or analysis.appears_empty_or_white:
        raise SmokeFailure(f"{case}/{variant}: framebuffer is empty or lacks scene variation")


def png_rgb_bytes(frame):
    """Losslessly encode the unchanged framebuffer RGB pixels without imaging dependencies."""
    width, height, rows = frame
    if width <= 0 or height <= 0 or len(rows) != height or any(len(row) != width for row in rows):
        raise SmokeFailure("cannot encode a malformed framebuffer as PNG")

    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff)

    # Filter 0 stores each channel exactly as read; DEFLATE only compresses bytes.
    scanlines = bytearray()
    for row in rows:
        scanlines.append(0)
        scanlines.extend(channel for pixel in row for channel in pixel)
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(scanlines, 9)) + chunk(b"IEND", b"")


def capture(args, case, variant):
    output = args.output_directory / f"{case}_{variant}.bmp"
    command = [
        sys.executable, str(Path(__file__).with_name("window_capture_smoke.py")),
        "--executable", str(args.executable), "--working-directory", str(args.working_directory),
        "--output", str(output), "--application-capture",
        "--application-capture-frame-count", str(args.frames), "--timeout", str(args.timeout),
        "--expect-log-message", f"RefractionSmokeProject: gallery case {case} created",
        "--expect-log-message", "RefractionSmokeProject: shutdown",
        "--expect-log-message", "RefractionSmokeProject: refraction " + (
            "disabled" if variant in ("geometry", "disabled") else "enabled"
        ),
    ]
    if args.logserver_executable:
        command += ["--logserver-executable", str(args.logserver_executable)]
    else:
        command.append("--no-logserver")
    if variant == "automatic":
        command += ["--expect-log-message", "AVBOIT refraction resolve: hardware" if args.require_hardware
            else "AVBOIT refraction resolve:"]
    elif variant == "screen":
        command += ["--expect-log-message", "AVBOIT refraction resolve: screen-space"]
    else:
        command += ["--reject-log-message", "AVBOIT refraction resolve:"]
    command.extend("--application-arg=" + argument for argument in args.application_arg)
    print(f"Capturing {case}/{variant} ({args.frames} frames)...", flush=True)
    completed = subprocess.run(command, env=capture_environment(case, variant), check=False, timeout=args.timeout + 30)
    if completed.returncode == SKIP_EXIT_CODE:
        return None
    if completed.returncode != 0:
        raise SmokeFailure(f"{case}/{variant}: capture failed with exit {completed.returncode}")
    frame = read_bmp_24_rows(output)
    validate_frame(frame, case, variant)
    return frame


HTML = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>AVBOIT refraction: rendered case gallery</title>
<style>
:root{color-scheme:dark;font:15px/1.5 system-ui,sans-serif;background:#10151d;color:#e6edf5}
*{box-sizing:border-box}body{margin:0}header,main{max-width:1500px;margin:auto;padding:24px 28px}header{padding-bottom:12px}
h1{font-size:28px;line-height:1.2;margin:0 0 8px}h2{margin:0;font-size:21px}p{max-width:1100px;margin:8px 0;color:#bdc8d8}
.eyebrow{color:#74d5d3;text-transform:uppercase;font-size:11px;letter-spacing:.13em;font-weight:700;margin-bottom:10px}
.small{font-size:12px;color:#9daec2}.notice{border-left:3px solid #d5ad65;background:#1c222c;padding:12px 16px;margin:16px 0}
.case-tabs{display:flex;flex-wrap:wrap;gap:7px;margin:10px 0 20px}button,select,input{font:inherit}button{border:1px solid #354457;background:#1c2634;color:inherit;border-radius:7px;padding:7px 11px;cursor:pointer}button:hover{background:#26354a}button[aria-pressed=true]{background:#244b54;border-color:#68c8c8}
.controls{display:flex;flex-wrap:wrap;gap:14px;align-items:center;padding:12px 0;margin:12px 0;border-block:1px solid #2a3545}.controls label{display:flex;gap:7px;align-items:center}.controls input[type=range]{width:120px}.checks{display:flex;flex-wrap:wrap;gap:12px}select{color:inherit;background:#1c2634;border:1px solid #354457;padding:5px;border-radius:5px}
.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.grid.single{grid-template-columns:1fr}.card{border:1px solid #2c394a;border-radius:10px;overflow:hidden;background:#151e2b}.card-head{display:flex;justify-content:space-between;align-items:center;padding:11px 14px;gap:10px}.card-head strong{font-size:14px}.viewport{overflow:hidden;aspect-ratio:4/3;background:#080c12;cursor:zoom-in}.viewport img{width:100%;height:100%;object-fit:contain;display:block;transform:scale(var(--zoom,1));transform-origin:center}.caption{padding:10px 14px;border-top:1px solid #2c394a;color:#a9b9ca;font-size:12px;min-height:60px}.caption p{margin:0;font-size:12px}
.key{display:flex;flex-wrap:wrap;gap:16px;margin:16px 0;color:#a9b9ca;font-size:12px}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:6px}.red{background:#ff5f64}.blue{background:#6b8eff}.gray{background:#dce3ec}.metrics{font-variant-numeric:tabular-nums}.case-note{min-height:55px}footer{max-width:1500px;padding:24px 28px;margin:auto;color:#8fa1b7;font-size:12px}
dialog{padding:0;max-width:96vw;max-height:96vh;width:1300px;background:#10151d;color:inherit;border:1px solid #526276;border-radius:10px}dialog::backdrop{background:#000c}.dialog-head{display:flex;justify-content:space-between;align-items:center;padding:10px 16px}.dialog-view{overflow:auto;max-height:82vh;background:#080c12;text-align:center}.dialog-view img{display:block;margin:auto;max-width:none}.dialog-controls{display:flex;align-items:center;gap:12px}
@media(max-width:760px){header,main{padding:18px 14px}.grid{grid-template-columns:1fr}h1{font-size:24px}.case-note{min-height:0}.card-head strong{font-size:13px}}
</style>
<header><div class="eyebrow">Actual 960 × 720 framebuffer captures</div><h1>AVBOIT refraction: rendered case gallery</h1>
<p>Inspect geometry, hardware-preferred tracing, screen-space fallback, and refraction disabled from the same fixed camera.</p>
<p class="small" id="provenance"></p><div class="notice" id="limits"></div></header>
<main><nav class="case-tabs" id="tabs" aria-label="Refraction case"></nav>
<h2 id="case-title"></h2><p id="case-description"></p><p class="case-note" id="case-note"></p>
<div class="key"><span><i class="dot red"></i>Red: foreground AVBOIT panel</span><span><i class="dot blue"></i>Blue: background AVBOIT panel</span><span><i class="dot gray"></i>Stripes: opaque background</span></div>
<div class="controls"><div class="checks" id="variant-controls"></div><label>Scene zoom <input id="zoom" type="range" min="1" max="3" step=".25" value="1.5"><output id="zoom-value">1.5×</output></label><label>Layout <select id="layout"><option value="two">Side by side</option><option value="single">One per row</option></select></label><button id="reset">Reset view</button></div>
<section class="grid" id="cards" aria-label="Captured variants"></section></main>
<footer>All images are actual framebuffer readbacks, encoded losslessly as PNG with unchanged RGB pixels. The original BMPs are retained beside this file. This gallery works offline and needs no image server or external libraries. Click a capture to inspect pixels. Differences compare the full frame with the disabled variant and are descriptive, not a physical-accuracy score.</footer>
<dialog id="viewer"><div class="dialog-head"><strong id="viewer-title"></strong><div class="dialog-controls"><label>Pixel scale <select id="pixel-scale"><option value="1">100%</option><option value="2">200%</option><option value="3">300%</option></select></label><button id="close-viewer">Close</button></div></div><div class="dialog-view"><img id="viewer-image" alt="Expanded actual framebuffer capture"></div></dialog>
<script id="capture-data" type="application/json">__CAPTURE_DATA__</script>
<script>
const data=JSON.parse(document.getElementById('capture-data').textContent);
const byId=id=>document.getElementById(id);let active=data.cases[0].id;const enabled=new Set(data.variants);
const routeLabel=data.variants.includes('automatic')?(data.hardware_required?'Hardware dispatch verified for automatic variants':'Automatic hardware preference'):'Explicit variants only';
byId('provenance').textContent=`${data.cases.length} cases · ${data.image_count} captures · ${data.frames} accepted frames per capture · ${data.captured_at} · ${routeLabel}`;
byId('limits').textContent=data.limits;
const label=id=>data.variant_labels[id];
function openImage(item,variant){byId('viewer-title').textContent=`${item.title} / ${label(variant.id)}`;byId('viewer-image').src=variant.image;byId('pixel-scale').value='1';setPixelScale();byId('viewer').showModal()}
function setPixelScale(){byId('viewer-image').style.width=(960*Number(byId('pixel-scale').value))+'px';byId('viewer-image').style.imageRendering=Number(byId('pixel-scale').value)>1?'pixelated':'auto'}
function updateZoom(){const zoom=Number(byId('zoom').value);byId('cards').style.setProperty('--zoom',zoom);byId('zoom-value').textContent=zoom+'×'}
function render(){const item=data.cases.find(c=>c.id===active);byId('case-title').textContent=item.title;byId('case-description').textContent=item.description;byId('case-note').textContent=item.note;for(const button of byId('tabs').children)button.setAttribute('aria-pressed',button.dataset.case===active);byId('cards').replaceChildren();
for(const variant of item.captures){if(!enabled.has(variant.id))continue;const card=document.createElement('article');card.className='card';const head=document.createElement('div');head.className='card-head';const title=document.createElement('strong');title.textContent=label(variant.id);const dimensions=document.createElement('span');dimensions.className='small';dimensions.textContent='960 × 720';head.append(title,dimensions);const viewport=document.createElement('div');viewport.className='viewport';viewport.tabIndex=0;viewport.setAttribute('role','button');viewport.setAttribute('aria-label','Inspect '+label(variant.id));const image=document.createElement('img');image.src=variant.image;image.alt=item.title+' — '+label(variant.id)+' — actual renderer output';viewport.append(image);viewport.addEventListener('click',()=>openImage(item,variant));viewport.addEventListener('keydown',e=>{if(e.key==='Enter')openImage(item,variant)});const caption=document.createElement('div');caption.className='caption metrics';const text=document.createElement('p');const metric=variant.difference;text.textContent=variant.id==='geometry'?'Colored translucent materials and a neutral backdrop reveal the geometry. Refraction is disabled; this preview is excluded from optical differences.':variant.id==='disabled'?'Reference image with refraction disabled; ordinary AVBOIT remains active.':metric?`${metric.changed_pixels.toLocaleString()} changed pixels (${(metric.changed_fraction*100).toFixed(2)}% of frame) · mean RGB difference ${metric.mean_absolute_rgb_difference.toFixed(2)} / 255`:'Disabled baseline was not selected; no difference metric was computed.';caption.append(text);card.append(head,viewport,caption);byId('cards').append(card)}updateZoom()}
for(const item of data.cases){const button=document.createElement('button');button.dataset.case=item.id;button.textContent=item.title;button.setAttribute('aria-pressed',item.id===active);button.addEventListener('click',()=>{active=item.id;render()});byId('tabs').append(button)}
for(const variant of data.variants){const wrapper=document.createElement('label');const input=document.createElement('input');input.type='checkbox';input.checked=true;input.addEventListener('change',()=>{if(input.checked)enabled.add(variant);else enabled.delete(variant);render()});wrapper.append(input,document.createTextNode(label(variant)));byId('variant-controls').append(wrapper)}
byId('zoom').addEventListener('input',updateZoom);byId('layout').addEventListener('change',()=>byId('cards').classList.toggle('single',byId('layout').value==='single'));byId('reset').addEventListener('click',()=>{byId('zoom').value='1';byId('layout').value='two';byId('cards').classList.remove('single');updateZoom()});byId('close-viewer').addEventListener('click',()=>byId('viewer').close());byId('pixel-scale').addEventListener('change',setPixelScale);render();
</script></html>'''


def write_gallery(output_directory, manifest):
    # Preserve raw BMPs alongside the report. Embedded PNGs contain the same RGB
    # pixels; no resizing, repainting, or generated image is involved.
    (output_directory / "gallery_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    embedded = json.loads(json.dumps(manifest))
    for case in embedded["cases"]:
        for capture_item in case["captures"]:
            content = png_rgb_bytes(read_bmp_24_rows(output_directory / capture_item["file"]))
            (output_directory / Path(capture_item["file"]).with_suffix(".png")).write_bytes(content)
            capture_item["image"] = "data:image/png;base64," + base64.b64encode(content).decode("ascii")
    data = json.dumps(embedded, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
    prefix, suffix = HTML.split("__CAPTURE_DATA__")
    with (output_directory / "gallery.html").open("w", encoding="utf-8", newline="\n") as output:
        output.write(prefix)
        output.write(data)
        output.write(suffix)


def parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--working-directory", type=Path, required=True)
    parser.add_argument("--output-directory", type=Path, required=True)
    parser.add_argument("--logserver-executable", type=Path)
    parser.add_argument("--cases", type=lambda value: parse_selection(value, CASES, "cases"), default=tuple(CASES),
        help="Comma-separated selection; defaults to all: " + ", ".join(CASES))
    parser.add_argument("--variants", type=lambda value: parse_selection(value, VARIANTS, "variants"), default=VARIANTS,
        help="Comma-separated selection; defaults to all: " + ", ".join(VARIANTS))
    parser.add_argument("--frames", type=int, default=16)
    parser.add_argument("--timeout", type=float, default=60.0, help="Capture timeout in seconds for each child run.")
    parser.add_argument("--require-hardware", action="store_true")
    parser.add_argument("--application-arg", action="append", default=[])
    args = parser.parse_args(argv)
    if args.frames <= 0 or args.timeout <= 0:
        parser.error("frames and timeout must be positive")
    args.output_directory = args.output_directory.resolve()
    return args


def main(argv):
    args = parse_args(argv)
    args.output_directory.mkdir(parents=True, exist_ok=True)
    labels = dict(VARIANT_LABELS)
    if args.require_hardware:
        labels["automatic"] = "Hardware RT (per-pixel fallback allowed)"
    manifest = {
        "captured_at": datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M UTC"),
        "frames": args.frames, "hardware_required": args.require_hardware,
        "application_args": list(args.application_arg),
        "variants": list(args.variants), "variant_labels": labels, "limits": COMMON_LIMIT,
        "image_count": len(args.cases) * len(args.variants), "cases": [],
        "validation": "Strict runtime logs, successful route dispatch, normal shutdown, nontrivial frames, and visible changes when a disabled baseline is present.",
    }
    try:
        for case_id in args.cases:
            title, description, note = CASES[case_id]
            entry = {"id": case_id, "title": title, "description": description, "note": note, "captures": []}
            frames = {}
            for variant in args.variants:
                frame = capture(args, case_id, variant)
                if frame is None:
                    print(f"SKIP: framebuffer capture unavailable for {case_id}/{variant}", file=sys.stderr)
                    return SKIP_EXIT_CODE
                frames[variant] = frame
                entry["captures"].append({"id": variant, "file": f"{case_id}_{variant}.bmp"})
            if "disabled" in frames:
                for captured in entry["captures"]:
                    variant = captured["id"]
                    if variant in ("disabled", "geometry"):
                        continue
                    metrics = frame_difference(frames["disabled"], frames[variant])
                    captured["difference"] = metrics
                    if metrics["changed_pixels"] < 64:
                        raise SmokeFailure(f"{case_id}/{variant}: fewer than 64 pixels changed from disabled baseline; expected a visible scene effect")
            manifest["cases"].append(entry)
            del frames
        write_gallery(args.output_directory, manifest)
        print(f"PASS: {manifest['image_count']} actual captures; offline gallery: {args.output_directory / 'gallery.html'}", flush=True)
        return 0
    except (OSError, SmokeFailure, subprocess.TimeoutExpired) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
