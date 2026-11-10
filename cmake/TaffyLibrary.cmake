# Standalone, standard-library-only production target. No test/tool dependencies.
include_guard(GLOBAL)
include(GNUInstallDirs)
get_filename_component(_taffy_library_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_library(taffy
    "${_taffy_library_root}/src/geometry.cpp"
    "${_taffy_library_root}/src/numeric.cpp"
    "${_taffy_library_root}/src/style.cpp"
    "${_taffy_library_root}/src/parse.cpp"
    "${_taffy_library_root}/src/layout.cpp"
    "${_taffy_library_root}/src/cache.cpp"
    "${_taffy_library_root}/src/leaf.cpp"
    "${_taffy_library_root}/src/tree.cpp"
    "${_taffy_library_root}/src/tree_layout.cpp"
    "${_taffy_library_root}/src/dispatch.cpp"
    "${_taffy_library_root}/src/compute_common.cpp"
    "${_taffy_library_root}/src/compute_oof.cpp"
    "${_taffy_library_root}/src/compute_block.cpp"
    "${_taffy_library_root}/src/compute_flex.cpp"
    "${_taffy_library_root}/src/compute_grid.cpp"
    "${_taffy_library_root}/src/root.cpp"
    "${_taffy_library_root}/src/rounding.cpp"
    "${_taffy_library_root}/src/block/float.cpp"
    "${_taffy_library_root}/src/flex/cross_size.cpp"
    "${_taffy_library_root}/src/flex/setup.cpp"
    "${_taffy_library_root}/src/flex/placement.cpp"
    "${_taffy_library_root}/src/flex/lines.cpp"
    "${_taffy_library_root}/src/flex/main_size.cpp"
    "${_taffy_library_root}/src/grid/grid_item.cpp"
    "${_taffy_library_root}/src/grid/explicit_grid.cpp"
    "${_taffy_library_root}/src/grid/implicit_grid.cpp"
    "${_taffy_library_root}/src/grid/track_sizing.cpp"
    "${_taffy_library_root}/src/grid/placement.cpp"
    "${_taffy_library_root}/src/grid/named.cpp"
    "${_taffy_library_root}/src/grid/metadata.cpp"
    "${_taffy_library_root}/src/grid/alignment.cpp")
add_library(taffy::taffy ALIAS taffy)
get_target_property(_taffy_library_type taffy TYPE)
if(_taffy_library_type STREQUAL "SHARED_LIBRARY")
    target_compile_definitions(taffy PUBLIC TAFFY_SHARED_LIBRARY PRIVATE TAFFY_BUILDING_LIBRARY)
    if(MINGW)
        # Explicit data exports otherwise disable MinGW's implicit function exports.
        target_link_options(taffy PRIVATE -Wl,--export-all-symbols)
    endif()
endif()
unset(_taffy_library_type)
target_compile_features(taffy PUBLIC cxx_std_23)
set_target_properties(taffy PROPERTIES CXX_EXTENSIONS OFF WINDOWS_EXPORT_ALL_SYMBOLS ON
    VERSION "${PROJECT_VERSION}" SOVERSION 0)
if(CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang|AppleClang)$")
    target_compile_options(taffy PUBLIC -fno-fast-math -ffp-contract=off)
elseif(MSVC)
    target_compile_options(taffy PUBLIC /fp:strict)
endif()
target_include_directories(taffy PUBLIC
    "$<BUILD_INTERFACE:${_taffy_library_root}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
unset(_taffy_library_root)
