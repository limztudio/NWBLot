# Public UI input CPU benchmark

This opt-in workload measures public input routing, pressed-state queries, paint publication, and editor work. It is separate from correctness CTest suites and does not expose production test hooks.

Enable `NWB_BUILD_UI_INPUT_BENCHMARK=ON` in a normal configured build, then build the `nwb_ui_input_benchmark` target in `opt`. Run the executable directly to inspect its JSON case records. Each case warms its state, calibrates a batch to at least 20 milliseconds, records 11 samples, and checks that its semantic checksum stays constant independently of the calibrated iteration count.

Capture a complete runnable baseline executable and its dependencies before rebuilding the candidate. Use the same toolchain, configuration, host, runtime libraries, and workload sources. Do not build, run other tests, or edit either input while collecting timings.

Run `measure.py --baseline <baseline-executable> --candidate <candidate-executable> --baseline-source-hash <baseline-source-hash> --candidate-source-hash <candidate-source-hash> --output <evidence-directory>`. Add `--runtime-directory <directory>` when shared libraries are needed, and add `--baseline-source-manifest <json>` and `--candidate-source-manifest <json>` to preserve exact source inventories. Executable paths and source metadata are explicit; the runner does not select an older build when discovery fails.

The runner alternates four AB/BA process pairs by default, rejects setup or behavior checksum differences, records all samples and process medians, and writes `report.json`. It also records executable and supplied runtime-library hashes and verifies they did not change during measurement. Results describe these CPU workloads and do not establish end-to-end frame rate or GPU savings.
