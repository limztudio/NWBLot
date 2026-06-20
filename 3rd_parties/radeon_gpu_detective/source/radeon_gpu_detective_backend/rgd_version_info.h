//=============================================================================
// Copyright (c) 2025 Advanced Micro Devices, Inc. All rights reserved.
/// @author AMD Developer Tools Team
/// @file
/// @brief Radeon GPU Detective version header.
///
/// NWB: this is a vendored, hand-written stand-in for the build-time-generated
/// rgd_version_info.h (upstream configures it from rgd_version_info.h.in via a
/// git-describe step we do not run in the offline/vendored build). Only RGD_TITLE
/// is consumed by the backend serializers, so the version numbers are pinned to
/// the vendored snapshot rather than discovered from git.
//=============================================================================

#ifndef RGD_FRONTEND_VERSION_H_
#define RGD_FRONTEND_VERSION_H_

#define STRINGIFY_MACRO_(a) #a
#define STRINGIFY_MACRO(a) STRINGIFY_MACRO_(a)
#define STRINGIFY_VERSION(major, minor, patch, build) STRINGIFY_MACRO(major) "." STRINGIFY_MACRO(minor) "." STRINGIFY_MACRO(patch) "." STRINGIFY_MACRO(build)

#define RGD_MAJOR_VERSION 1   ///< Major version number.
#define RGD_MINOR_VERSION 5   ///< Minor version number.
#define RGD_PATCH_NUMBER  0   ///< Patch number.
#define RGD_BUILD_NUMBER  0   ///< Build number.
#define RGD_BUILD_SUFFIX  ""  ///< Build suffix
#define RGD_BUILD_YEAR "2025"
#define RGD_VERSION STRINGIFY_VERSION(RGD_MAJOR_VERSION, RGD_MINOR_VERSION, RGD_PATCH_NUMBER, RGD_BUILD_NUMBER)
#define RGD_APP_NAME "Radeon GPU Detective"
#define RGD_TITLE RGD_APP_NAME " " RGD_VERSION " " RGD_BUILD_SUFFIX ///< Title
#define RGD_COPYRIGHT_STRING "Copyright (C) " RGD_BUILD_YEAR " Advanced Micro Devices, Inc. All rights reserved."

#endif
