# NWBLot Notes

Updated: 2026-02-26

## Important Rules

1. `IDevice` is an essential object in `Graphics`. Do not add runtime null checks for local `IDevice* device` retrievals in this layer.
2. Use `getDevice()` and rely on invariant/assert behavior instead of defensive null handling that adds overhead and hides invalid states.
