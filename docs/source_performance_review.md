# Source performance review

Measurements use Windows ARM64 CPU workloads, one warm-up and seven alternating before/after runs. Values are medians in milliseconds. Each comparison includes the same functional workload; setup is excluded unless described. These are CPU operation measurements, not FPS or GPU timing claims. Allocation-owner telemetry remains enabled and its memory payload remains version 1.

## Asset input selection

The selector builds one operation-local sorted index over discovered canonical paths. Exact file inputs and directory prefixes use indexed ranges, with a separate case-preserving directory index on non-Windows hosts. Discovery order, duplicate records, overlapping inputs, empty directories, root boundaries, and validation-before-compaction are preserved. The benchmark includes path resolution, filesystem queries, selection, and owning scratch teardown for 2,048 files.

| Workload | dbg before → after | opt before → after |
| --- | ---: | ---: |
| 2,048 explicit file inputs | 183.4631 → 102.1824 | 57.4520 → 42.8689 |
| One directory containing 2,048 files | 41.0431 → 0.5545 | 1.6796 → 0.0925 |

Four input-selection regressions pass in dbg, opt, and fin. The staged pipeline tools and asset integration target build in all three configurations. Host case behavior is encoded in the tests; this machine executes the Windows branch.
