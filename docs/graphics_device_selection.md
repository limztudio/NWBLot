# Card pick

The build-time `NWB_GRAPHICS_BACKEND` CMake pick defaults to `Vulkan`. That
is the only built back. `Metal` is kept for a next back and fails setup now.
CMake puts out match `NWB_GRAPHICS_BACKEND_VULKAN=1` and
`NWB_GRAPHICS_BACKEND_METAL=0` marks.

The Vulkan back needs set card skills at start. It needs `VK_EXT_descriptor_buffer`.
The run checks them and fails start when they miss.
