"""Window-capture result validation: image oracles per smoke mode and required, rejected, and skip log messages."""


from window_capture_outcome import SmokeSkip, WindowsDesktopUnavailable, SmokeFailure, write_status  # noqa: E402
from window_capture_logs import choose_free_port, wait_for_tcp_port, snapshot_log_files, collect_log_delta, standalone_log_pattern, wait_for_log_message, wait_for_log_drain  # noqa: E402


FRAMEBUFFER_CAPTURE_READY_MESSAGE = "FramebufferCapture: capture ready"
FRAMEBUFFER_CAPTURE_SKIP_MESSAGE = "FramebufferCapture: skipped because swap-chain transfer-source usage is unavailable"


def validate_capture_result(result):
    if result.appears_empty_or_white:
        raise SmokeFailure(f"captured window 0x{result.handle:x}, but the image appears blank or white")
    if not result.has_pixel_variation:
        raise SmokeFailure(f"captured window 0x{result.handle:x}, but the image appears flat")


def validate_transparent_multi_result(result):
    analysis = result.transparent_multi
    min_pixels = max(180, (result.width * result.height) // 5000)
    missing = []
    if analysis.left_pixels < min_pixels:
        missing.append(f"left={analysis.left_pixels}")
    if analysis.center_pixels < min_pixels:
        missing.append(f"center={analysis.center_pixels}")
    if analysis.right_pixels < min_pixels:
        missing.append(f"right={analysis.right_pixels}")

    if missing:
        observed = (f"left={analysis.left_pixels}, " f"center={analysis.center_pixels}, " f"right={analysis.right_pixels}, " f"min={min_pixels}")
        raise SmokeFailure(f"transparent multi-object scene did not show all expected color regions ({observed})")


def validate_transparent_csg_result(result, pose):
    analysis = result.transparent_csg[pose]
    min_void_pixels = max(160, analysis.cut_region_pixels // 3)
    min_remaining_pixels = max(160, analysis.remaining_region_pixels // 5)
    missing = []
    if analysis.cut_void_pixels < min_void_pixels:
        missing.append(f"cut_void={analysis.cut_void_pixels}")
    if analysis.remaining_center_pixels < min_remaining_pixels:
        missing.append(f"remaining_center={analysis.remaining_center_pixels}")

    if missing:
        observed = (f"cut_void={analysis.cut_void_pixels}/{analysis.cut_region_pixels}, " f"remaining_center={analysis.remaining_center_pixels}/{analysis.remaining_region_pixels}, " f"min_void={min_void_pixels}, " f"min_remaining={min_remaining_pixels}")
        raise SmokeFailure(f"transparent CSG {pose} pose did not show the expected clipped center region ({observed})")


def validate_texture_smoke_result(result):
    analysis = result.texture_smoke
    min_pixels = max(220, (result.width * result.height) // 4000)
    # The receiver-only region is deliberately tight enough to exclude authored sphere texels.
    # Require substantial coverage within that region while retaining a fixed floor that rejects isolated red noise.
    min_receiver_red_pixels = max(256, analysis.receiver_pixel_count // 4)
    missing = []
    if analysis.red_pixels < min_pixels:
        missing.append(f"red={analysis.red_pixels}")
    if analysis.green_pixels < min_pixels:
        missing.append(f"green={analysis.green_pixels}")
    if analysis.blue_pixels < min_pixels:
        missing.append(f"blue={analysis.blue_pixels}")
    if analysis.receiver_red_pixels < min_receiver_red_pixels:
        missing.append(f"receiver_red={analysis.receiver_red_pixels}")

    if missing:
        observed = (f"red={analysis.red_pixels}, " f"green={analysis.green_pixels}, " f"blue={analysis.blue_pixels}, " f"receiver_red={analysis.receiver_red_pixels}/{analysis.receiver_pixel_count}, " f"min={min_pixels}, receiver_min={min_receiver_red_pixels}")
        raise SmokeFailure(f"texture smoke did not show the expected sampled texture and receiver-side GI ({observed})")


def validate_expected_log_text(log_text, required_needles, rejected_needles, skip_needles=(), skip_blocking_needles=(),):
    if not required_needles and not rejected_needles and not skip_needles and not skip_blocking_needles:
        return
    if not log_text:
        raise SmokeFailure("runtime log validation captured no output")
    for needle in skip_needles:
        if needle in log_text:
            for blocking_needle in skip_blocking_needles:
                if blocking_needle in log_text:
                    raise SmokeFailure(f"capability skip also contained blocking log message '{blocking_needle}'\n{log_text[-4000:]}")
            return needle
    for needle in rejected_needles:
        if needle in log_text:
            raise SmokeFailure(f"rejected log message '{needle}' was found\n{log_text[-4000:]}")
    for needle in required_needles:
        if needle not in log_text:
            raise SmokeFailure(f"missing log message '{needle}'\n{log_text[-4000:]}")
    return None


def validate_expected_log_messages(log_directory, log_baseline, log_pattern, required_needles, rejected_needles, skip_needles=(), skip_blocking_needles=(),):
    if not required_needles and not rejected_needles and not skip_needles and not skip_blocking_needles:
        return
    if not log_directory:
        raise SmokeFailure("cannot validate expected log messages without captured runtime logs")

    return validate_expected_log_text(collect_log_delta(log_directory, log_baseline, log_pattern), required_needles, rejected_needles, skip_needles, skip_blocking_needles,)


def validate_application_capture_log_text(log_text, args):
    required_needles = [FRAMEBUFFER_CAPTURE_READY_MESSAGE]
    required_needles.extend(args.expect_log_message)
    skip_needles = [FRAMEBUFFER_CAPTURE_SKIP_MESSAGE]
    skip_needles.extend(args.skip_log_message)
    return validate_expected_log_text(log_text, required_needles, args.reject_log_message, skip_needles, args.skip_blocking_log_message,)


def validate_capture_for_args(args, result):
    validate_capture_result(result)
    if args.expect_transparent_multi:
        validate_transparent_multi_result(result)
    if args.expect_transparent_csg:
        validate_transparent_csg_result(result, args.transparent_csg_pose)
    if args.expect_texture_smoke:
        validate_texture_smoke_result(result)
