import base64
import hashlib
import html
import json
from pathlib import Path
import subprocess
import sys

from framebuffer_png_encoder import FramebufferPngEncoder
from window_capture_image_codec import Bmp24Codec
from window_capture_smoke import SKIP_EXIT_CODE, SmokeFailure

DEFAULT_CAPTURE_TIMEOUT_MARGIN_SECONDS = 90
DEFAULT_EVIDENCE_GALLERY_STYLE = (
    "body{background:#141922;color:#e7edf5;font:16px system-ui;margin:28px}"
    "main{display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:20px}"
    "article{background:#202937;padding:16px}img{display:block;width:100%;margin-top:12px}"
    "a{color:#8cf}pre{white-space:pre-wrap}h2{font-size:19px}"
)


def build_window_capture_command(
    args,
    output,
    *,
    frames=1,
    log_output=None,
    expect_logs=(),
    reject_logs=(),
    skip_logs=(),
    extra_args=(),
    script_path=None,
    gpu_validation=None,
    application_args=None,
):
    script = Path(script_path) if script_path is not None else Path(__file__).with_name("window_capture_smoke.py")
    command = [
        sys.executable,
        str(script),
        "--executable",
        str(args.executable),
        "--working-directory",
        str(args.working_directory),
        "--output",
        str(output),
        "--application-capture",
        "--application-capture-frame-count",
        str(frames),
        "--timeout",
        str(args.timeout),
    ]
    if log_output is not None:
        command += ["--log-output", str(log_output)]
    if gpu_validation is not None:
        command.append("--gpu-validation" if gpu_validation else "--no-gpu-validation")
    for message in expect_logs:
        command += ["--expect-log-message", message]
    for message in reject_logs:
        command += ["--reject-log-message", message]
    for message in skip_logs:
        command += ["--skip-log-message", message]
    logserver = getattr(args, "logserver_executable", None)
    if logserver:
        command += ["--logserver-executable", str(logserver)]
    else:
        command.append("--no-logserver")
    command.extend(extra_args)
    resolved_app_args = getattr(args, "application_arg", ()) if application_args is None else application_args
    command.extend("--application-arg=" + argument for argument in resolved_app_args)
    return command


def run_window_capture_subprocess(
    command,
    *,
    env,
    timeout,
    failure_message,
    timeout_margin=DEFAULT_CAPTURE_TIMEOUT_MARGIN_SECONDS,
    allow_skip=True,
):
    result = subprocess.run(command, env=env, check=False, timeout=timeout + timeout_margin)
    if allow_skip and result.returncode == SKIP_EXIT_CODE:
        return None
    if result.returncode:
        raise SmokeFailure(failure_message.format(code=result.returncode))
    return 0


def sha256_lf(path):
    return hashlib.sha256(Path(path).read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def sha256_source_files(mapping):
    return {key: sha256_lf(path) for key, path in mapping.items()}


def write_evidence_gallery_report(
    output_directory,
    *,
    manifest_name,
    html_name,
    metadata,
    title,
    heading,
    note,
    items,
    metrics,
    pending_message,
    extra_paragraphs=(),
    style=None,
):
    (output_directory / manifest_name).write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    cards = []
    for stem, card_heading, bmp_name, log_name, log_label, alt_text, frame in items:
        rows = frame if frame is not None else Bmp24Codec.read_bmp_24_rows(output_directory / bmp_name)
        png = FramebufferPngEncoder.encode(rows)
        (output_directory / (stem + ".png")).write_bytes(png)
        encoded = base64.b64encode(png).decode("ascii")
        cards.append(
            f"<article><h2>{html.escape(card_heading)}</h2>"
            f'<a href="{html.escape(bmp_name)}">Raw BMP</a> / '
            f'<a href="{html.escape(log_name)}">{html.escape(log_label)}</a>'
            f'<img alt="{html.escape(alt_text)}" src="data:image/png;base64,{encoded}"></article>'
        )
    paragraphs = "".join(f"<p>{html.escape(text)}</p>" for text in (note, *extra_paragraphs))
    report_style = DEFAULT_EVIDENCE_GALLERY_STYLE if style is None else style
    metrics_text = json.dumps(metrics, indent=2) if metrics else pending_message
    document = (
        f'<!doctype html><html lang="en"><meta charset="utf-8"><title>{html.escape(title)}</title>'
        f"<style>{report_style}</style>"
        f"<h1>{html.escape(heading)}</h1>{paragraphs}<main>{''.join(cards)}</main>"
        f"<pre>{html.escape(metrics_text)}</pre></html>"
    )
    (output_directory / html_name).write_text(document, encoding="utf-8")
