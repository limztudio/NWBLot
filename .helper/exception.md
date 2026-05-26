# Dependency Inversion Exceptions

Updated: 2026-05-25

Read this before changing code for the dependency-inversion cleanup schedule.

## Accepted Shape

Project-owned assets may implement an engine-defined runtime contract without being treated as dependency inversion.

BxDF material policy is project-owned. Do not move BxDF shader stages, BxDF shader includes, BxDF material interfaces, or BxDF material metadata selectors into `impl/assets` or any `engine/...` virtual asset path. Each project or test asset tree that needs BxDF must own its own `project/material_interfaces/bxdf` and `project/shaders/bxdf*` assets.

This includes project material/shader assets, such as BxDF shader stages under a project asset tree, being referenced by project material metadata and consumed by the engine through typed asset references, virtual asset paths, or declared shader-stage contracts.

It also includes engine render-pass shaders declaring a project include root for a named project shader hook, such as a project-owned BxDF lighting include, when that include is the project's implementation of the material/lighting contract.

The ownership direction is:

```text
engine/runtime defines contract or shader hook
project asset implements contract or hook
project metadata selects asset, or shader metadata declares the project include root
engine/runtime consumes selected asset through asset APIs or declared shader hook
```

That shape is allowed because the engine depends on the abstract asset/material contract, not on project code or project-specific implementation details.

## Asset Payload Ownership

Shader asset binary payload helpers, including SPIR-V bytecode payload validation, are owned by `impl/assets_shader/shader_binary_payload.h`.

Runtime SPIR-V entry-point parsing is owned by `core/graphics/spirv_entry_point.*` because it maps SPIR-V execution models to engine `ShaderType` values for runtime shader setup. Do not move shader payload helpers into `core/graphics` only because runtime parsing also needs SPIR-V constants, and do not make core graphics depend on `impl/assets_shader` for runtime parsing.

## Still Invalid

Do not use this exception to justify these shapes:

- Core modules including or linking implementation/project modules.
- Compatibility aliases in `core/*` that expose implementation ECS types.
- Engine default assets owning project-specific BxDF/material policy.
- BxDF shader stages, BxDF includes, BxDF material interfaces, or BxDF material metadata selectors placed under `impl/assets` or referenced as `engine/...`.
- Project-specific shader or material implementations placed under `impl/assets/graphics/` or `impl/assets/shaders/` only because an engine path needs a default.
- Engine shaders depending on arbitrary project shader internals instead of a narrow declared hook.
- Hardcoded engine lookups of one project asset when the project should provide metadata or typed `AssetRef<T>` selection.
- Moving shader asset binary payload ownership into `core/graphics` solely to share SPIR-V validation details with Vulkan runtime code.

## Scheduler Rule

When the dependency-inversion cleanup schedule runs, read this file first. If a dependency shape matches the accepted shape above, leave it in place and continue looking for real inversions elsewhere.
