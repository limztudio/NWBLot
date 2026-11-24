# Public UI input CPU benchmark

This opt-in workload measures public input routing, pressed-state queries, paint publication, and editor work. It is separate from correctness CTest suites and does not expose production test hooks.

Enable `NWB_BUILD_TESTS=ON` and `NWB_BUILD_UI_INPUT_BENCHMARK=ON`, then build the `nwb_ui_input_benchmark` target in `opt`. The full presets already enable tests; engine and project presets require the explicit test option. Run the executable directly to inspect its JSON case records. Each case warms its state, calibrates a batch to at least 20 milliseconds, records 11 samples, and checks that its semantic checksum stays constant independently of the calibrated iteration count.

Capture a complete runnable baseline executable and its dependencies before rebuilding the candidate. Use the same toolchain, configuration, host, runtime libraries, and workload sources. Do not build, run other tests, or edit either input while collecting timings.

Run `measure.py --baseline <baseline-executable> --candidate <candidate-executable> --baseline-source-hash <baseline-source-hash> --candidate-source-hash <candidate-source-hash> --output <evidence-directory>`. Use `--runtime-directory <directory>` to add directories to `PATH` for runtime lookup, and add `--baseline-source-manifest <json>` and `--candidate-source-manifest <json>` to preserve source inventories. On Linux, arrange shared-library lookup separately through the executable runtime path or `LD_LIBRARY_PATH`. Executable paths and source metadata are explicit; the runner does not select an older build when discovery fails.

The runner alternates four AB/BA process pairs by default, rejects setup or behavior checksum differences, records all samples and process medians, and writes `report.json`. It hashes each executable and the `*.dll` files in supplied runtime directories, then verifies those files did not change during measurement. Other runtime files, including Linux shared objects, are not covered by that hash check. Results describe these CPU workloads and do not establish end-to-end frame rate or GPU savings.
