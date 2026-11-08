"""Publish a self-contained gallery from captured refraction frames."""
import base64
import json
from pathlib import Path

from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image import read_bmp_24_rows

LIT_GALLERY_HTML = "gallery.html"
LIT_CASES = "cases"
LIT_CAPTURES = "captures"
LIT_FILE = "file"
LIT_UTF_8 = "utf-8"


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


class RefractionGalleryPublisher:
    @staticmethod
    def write(output_directory, manifest):
        # Embed lossless PNGs with the exact captured RGB pixels alongside the raw BMPs.
        (output_directory / "gallery_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding=LIT_UTF_8)
        embedded = json.loads(json.dumps(manifest))
        for case in embedded[LIT_CASES]:
            for capture_item in case[LIT_CAPTURES]:
                content = FramebufferPngEncoder.encode(read_bmp_24_rows(output_directory / capture_item[LIT_FILE]))
                (output_directory / Path(capture_item[LIT_FILE]).with_suffix(".png")).write_bytes(content)
                capture_item["image"] = "data:image/png;base64," + base64.b64encode(content).decode("ascii")
        data = json.dumps(embedded, ensure_ascii=False, separators=(",", ":")).replace("<", "\\u003c")
        prefix, suffix = HTML.split("__CAPTURE_DATA__")
        with (output_directory / LIT_GALLERY_HTML).open("w", encoding=LIT_UTF_8, newline="\n") as output:
            output.write(prefix)
            output.write(data)
            output.write(suffix)

