#pragma once

/// @file
/// Shared-library visibility for exported constants. CMake supplies the build/import
/// defines automatically through taffy::taffy; consumers normally need no manual flags.

// Public out-of-line data needs explicit DLL import/export. Function exports
// remain the build system's responsibility; do not decorate whole classes.
#if defined(_WIN32) && defined(TAFFY_SHARED_LIBRARY)
#if defined(TAFFY_BUILDING_LIBRARY)
#define TAFFY_API __declspec(dllexport)
#else
#define TAFFY_API __declspec(dllimport)
#endif
#else
#define TAFFY_API
#endif
