# Frozen surfel gather reference

`surfel_gather_field.slangi` is the exact production helper immediately before the five-head prefetch experiment, SHA-256 `0ac7c6d3bbc7ae81f33d4c5a77426a6e71964761f89ca9d5de1bdf689b9b00c4`. Keep this file unchanged when production gathering changes.

The native wrapper compiles this historical helper and the actual current production helper independently, each under both existing index-validity policies. It does not rewrite either helper or expose private accumulator state. The reference retains its original relative includes, resolved through the production surfel include directory. The shared record/constants/hash/SH implementation is unchanged by this experiment. A future change to those dependencies needs its own qualified oracle; this reference freezes the gather traversal, not all future surfel math.

Exact output coverage and half RGB words are required, including nonfinite words. The wrapper converts each already-half result through float into an explicit 16-bit representation in a uint so that raw buffer output does not add image-store conversion behavior. NaN payload canonicalization can be backend-dependent, but both variants run the same conversion on the same device; no tolerance or NaN exception weakens the parity assertion.
