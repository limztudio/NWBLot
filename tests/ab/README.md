# A/B workflows

Only complete, independently runnable workflows live here. Invoke them through the repository launcher so the same commands work on supported hosts:

- `python launcher.py async-shadow-m4` validates the dedicated async-compute shadow queue against its synchronous baseline.
- `python launcher.py bindless-parity <soft-shadows|caustics|surfel-gi>` captures the paired hardware and forced-software bindless paths.
- `python launcher.py frame-lagged-async-lighting` validates the opt-in lighting history lifecycle.
- `python launcher.py transfer-queue` profiles repeated large setup uploads on a real dedicated Transfer family.

The directory-level `launch.py` is a router, not a workflow. Do not add an A/B script that needs manual source edits, hard-coded build paths, or a platform shell. Keep raw captures, logs, timing tables, diff images, and result summaries under `.cozter/out/ab-results/`.
