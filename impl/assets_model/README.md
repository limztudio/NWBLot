# Model metadata

Models declare named local objects in `skeletons`, `static_meshes`, and `skinned_meshes`. A skeleton object binds its `skeleton` field to a typed skeleton asset reference. A skinned mesh binds its `mesh` and `skin` fields to their assets and selects its `skeleton` by the exact canonical identity of a local skeleton object name. Skeleton asset paths cannot select local objects. Duplicate local identities, missing targets, and targets of the wrong object kind fail validation.

Every authored `transform` is a compact three-row, four-column affine matrix. Translation occupies the last column. An omitted transform supplies identity. Four-row homogeneous matrices must be rewritten to the current 3x4 source shape; the cooker rejects them instead of discarding the fourth row. Model, skeleton, and skin source matrices share the same finite-value and dimensional validation.

The FBX converter emits this current metadata shape directly. Runtime model objects retain their local identities and typed asset references through cooking and serialization.
