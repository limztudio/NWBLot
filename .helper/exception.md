# Dependency Inversion Exceptions

Read this before changing code for the dependency-inversion cleanup schedule.

## Accepted Shape

Project-owned assets may implement an engine-defined runtime contract without being treated as dependency inversion.

Test asset trees and their test-only code must remain under `tests/`. These dependency-inversion exceptions do not permit test-related code or assets in production directories; follow `.helper/standard.md` section 20.

Project material + BXDF policy is project-owned. Do not move a project's material shader stages into `impl/assets` or any `engine/...` virtual asset path. This covers the surface/geometry mesh + pixel shaders. It covers the material shader includes (the surface material interface + color helpers). It covers per-material BXDF lighting models and material metadata selectors. Each project or test asset tree that needs materials must own its own project-local material interface, such as `project/shaders/surface`. It must own its `project/shaders/surface*` / `project/shaders/material*` stage + helper assets. It must own its per-material BXDF lighting models (e.g. `project/shaders/lambert_bxdf`) selected via the material `bxdf` field. (Note: "BXDF" means only the deferred lighting shading model; the G-buffer-writing surface/geometry pipeline is "surface"/"material", not "bxdf".)

This includes project material/shader assets under a project asset tree. Examples are the surface/geometry shader stages + the per-material BXDF lighting models. Project material metadata references them. The engine consumes them through typed asset references, virtual asset paths, or declared shader-stage contracts.

It also includes engine render-pass shaders declaring a project include root for a project shader hook. An example is the deferred lighting harness. It consumes the cook-generated per-material BXDF dispatch module (assembled from each material's project-owned BXDF). This applies when that hook is the project's implementation of the material/lighting contract.

The ownership direction is:

```text
engine/runtime defines contract or shader hook
project asset implements contract or hook
project metadata selects asset, or shader metadata declares the project include root
engine/runtime consumes selected asset through asset APIs or declared shader hook
```

That shape is allowed because the engine depends on the abstract asset/material contract, not on project code or project-specific implementation details.

## Asset Payload Ownership

Shader asset binary payload helpers, including SPIR-V bytecode payload validation, are owned by the shader asset implementation domain at `impl/assets_shader/types.adhd`.

Runtime SPIR-V entry-point parsing is owned by `core/graphics/spirv_entry_point.*`. It maps SPIR-V execution models to engine `ShaderType` values for runtime shader setup. Do not move shader payload helpers into `core/graphics` only because runtime parsing also needs SPIR-V constants. Do not make core graphics depend on `impl/assets_shader` for runtime parsing.

Concrete shader assets reuse that neutral parser at their admission boundary. They validate the exact entry point and physical stage. `IShader`, concrete stage types, their codecs, and shared payload encoding remain owned by `impl/assets_shader`. Archive roles do not change physical asset types. They do not justify provider-specific consumer includes.

## Still Invalid

Do not use this exception to justify these shapes:

- Core modules including or linking implementation/project modules.
- Compatibility aliases in `core/*` that expose implementation ECS types.
- Engine default assets owning project-specific material/BXDF policy (the engine ships no default BXDF; every material declares its own).
- Material/surface shader stages, material includes, material interfaces, per-material BXDF lighting models, or material metadata selectors placed under `impl/assets` or referenced as `engine/...`.
- Project-specific shader or material implementations placed under `impl/assets/graphics/` or `impl/assets/shaders/` only because an engine path needs a default.
- Engine shaders depending on arbitrary project shader internals instead of a narrow declared hook.
- Hardcoded engine lookups of one project asset when the project should provide metadata or typed `AssetRef<T>` selection.
- Moving shader asset binary payload ownership into `core/graphics` solely to share SPIR-V validation details with Vulkan runtime code.

## Scheduler Rule

When the dependency-inversion cleanup schedule runs, read this file first. If a dependency shape matches the accepted shape above, leave it in place and continue looking for real inversions elsewhere.
