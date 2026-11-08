"""Plan a balanced refraction workload profiling campaign."""

from collections import Counter
from datetime import datetime, timezone
import itertools

import refraction_density_benchmark as density
from experiment_statistics import PairedExperimentAnalysis
from gpu_load_host import GpuHostInventory

CELLS = tuple(density.Cell(extent, coverage, "hardware") for extent, coverage in itertools.product(("960x720", "1600x900"), (50, 100)))


class RefractionProfilePlan:
    @staticmethod
    def orders(blocks, seed):
        return PairedExperimentAnalysis(seed).balanced_orders(CELLS, blocks)

    @staticmethod
    def order_plan(rows):
        result, previous = [], None
        for block, row in enumerate(rows):
            for position, cell in enumerate(row):
                result.append({"block": block, "position": position, "cell": cell.key, "predecessor": previous,
                    "predecessor_within_block": row[position - 1].key if position else None})
                previous = cell.key
        return result

    @staticmethod
    def create_plan(args, arm, frozen, runner_identity):
        sequence = RefractionProfilePlan.order_plan(RefractionProfilePlan.orders(args.blocks, args.order_seed))
        positions = Counter((trial["cell"], trial["position"]) for trial in sequence)
        preceding = Counter((trial["predecessor_within_block"], trial["cell"]) for trial in sequence if trial["predecessor_within_block"] is not None)
        inventory = GpuHostInventory.collect()
        no_clock_source = not inventory["gpu_clock_counter_candidates"] and inventory["nvidia_smi"] is None
        return {"created_utc": datetime.now(timezone.utc).isoformat(), "executable": str(arm.executable), "runtime": str(arm.runtime), "identity": frozen,
            "host_inventory": inventory,
            "runner_identity": runner_identity(), "cells": [{"key": cell.key, "extent": cell.extent, "coverage": cell.density, "route": cell.route} for cell in CELLS],
            "blocks": args.blocks, "trial_count": len(sequence), "sequence": sequence, "order_seed": args.order_seed,
            "position_counts": {f"{cell}/position_{position}": count for (cell, position), count in positions.items()},
            "within_block_preceding_counts": {f"{before} -> {after}": count for (before, after), count in preceding.items()},
            "sample_intervals": args.sample_intervals, "warmup_intervals": args.warmup_intervals, "warmup_minimum_frames": args.warmup_minimum_frames,
            "warmup_minimum_seconds": args.warmup_minimum_seconds, "minimum_measured_gpu_frames": args.minimum_frames,
            "minimum_interval_runtime_seconds": len(sequence) * (args.warmup_minimum_seconds + args.sample_intervals * .5),
            "telemetry": {"period_seconds": 1, "provider": "persistent Windows PDH GPU Engine query", "GPU_frequency_available": False,
                "frequency_limitation": "Host inventory found no GPU clock counter candidate or nvidia-smi. Load cannot establish clock state." if no_clock_source else "Clock candidates require separate supported instrumentation; this campaign does not collect frequency values. Load cannot establish clock state.",
                "GPU_temperature_available": False, "temperature_limitation": "ACPI zones are not identified as GPU temperatures."},
            "capture": False, "validation": False, "diagnostic_instrumentation": False,
            "policy": "One frozen renderer; four workloads, eight equal-weight independent blocks. Complete Williams cycles balance position and within-block preceding treatment. Cross-block predecessor is recorded. Fixed target-only warmup, no adaptive plateau or outlier removal, selective repeats, or favorable extension. Telemetry is observational and has approximate publication alignment; no clock/DVFS causal attribution. All raw reports retained. Coverage comparisons change the optical workload, so control movement is described rather than treated as a fixed-scene optimization A/B."}
