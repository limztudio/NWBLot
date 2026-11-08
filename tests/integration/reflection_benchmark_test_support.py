"""Shared fixtures for reflection benchmark tests."""


def report(rows, cpu_frames=500):
    text = f"=== interval: {cpu_frames} frames / 0.500000s ===\n"
    for scope, total, samples in rows:
        text += (f"  {scope}: window_avg_ms=999 window_min_ms=888 window_max_ms=1000 published_windows=2 total_ms={total} " f"gpu_samples={samples} sample_avg_ms=777\n")
    return text
