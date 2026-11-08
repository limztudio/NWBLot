"""Balanced experiment ordering and paired bootstrap inference."""

import math
import random
import statistics

from window_capture_outcome import SmokeFailure


class PairedExperimentAnalysis:
    def __init__(self, seed=0, draws=10000):
        self.seed = seed
        self.draws = draws

    def balanced_orders(self, variants, blocks):
        """Williams rows balance position and preceding treatment over a full cycle."""
        count = len(variants)
        cycle = count if count % 2 == 0 else 2 * count
        if blocks < 5 or blocks % cycle:
            raise SmokeFailure(f"blocks must be at least 5 and a multiple of the balanced cycle ({cycle})")
        permutation = list(variants)
        random.Random(self.seed).shuffle(permutation)
        base = [0]
        for offset in range(1, count):
            base.append((offset + 1) // 2 if offset % 2 else count - offset // 2)
        rows = [[permutation[(value + shift) % count] for value in base] for shift in range(count)]
        if count % 2:
            rows += [list(reversed(row)) for row in rows]
        return [rows[index % cycle] for index in range(blocks)]

    def _percentile(self, sorted_values, fraction):
        position = (len(sorted_values) - 1) * fraction
        low = math.floor(position)
        high = math.ceil(position)
        return sorted_values[low] + (sorted_values[high] - sorted_values[low]) * (position - low)

    def paired_statistics(self, differences):
        if len(differences) < 5 or any(not math.isfinite(value) for value in differences):
            raise SmokeFailure("paired inference needs at least five finite independent block differences")
        rng = random.Random(self.seed)
        means = sorted(statistics.fmean(rng.choices(differences, k=len(differences))) for _ in range(self.draws))
        return {"blocks": len(differences), "mean_ms": statistics.fmean(differences), "median_ms": statistics.median(differences), "ci95_mean_ms": [self._percentile(means, .025), self._percentile(means, .975)], "block_differences_ms": differences}
