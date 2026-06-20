// NWB: the vendored PIX decoder relies on MSVC's implicit transitive standard includes. clang needs them
// explicit, so this header is force-included (via -include) ahead of every decoder translation unit.
#pragma once
#include <string_view>
#include <optional>
#include <cstdint>