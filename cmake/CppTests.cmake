include("${PROJECT_SOURCE_DIR}/cmake/TestDependencies.cmake")
taffy_test_dependency(Catch2 Catch2::Catch2WithMain 3.5.4)
taffy_test_dependency(pugixml pugixml::pugixml 1.15)
taffy_test_dependency(nlohmann_json nlohmann_json::nlohmann_json 3.11.3)
# Platform adjustments shared by every native test executable.
function(taffy_test_platform_options target)
    # Ported fixtures pass int/double literals to float and int16_t parameters throughout.
    # GCC/Clang do not diagnose that under -Wall -Wextra, but MSVC /W4 does (C4244), so
    # relax just that warning for test code; the library itself keeps full /W4 /WX.
    if(MSVC)
        target_compile_options(${target} PRIVATE /wd4244)
    endif()
    # Deep-tree fixtures (100+ nested nodes) overflow Windows' 1 MiB default main-thread
    # stack in unoptimized builds. Match the common 8 MiB Linux default instead.
    if(WIN32)
        if(MINGW)
            target_link_options(${target} PRIVATE "LINKER:--stack,8388608")
        else()
            target_link_options(${target} PRIVATE "LINKER:/STACK:8388608")
        endif()
    endif()
endfunction()
function(taffy_cpp_test family)
    add_executable(taffy_cpp_${family}_tests ${ARGN})
    target_link_libraries(taffy_cpp_${family}_tests PRIVATE taffy::taffy Catch2::Catch2WithMain)
    taffy_project_options(taffy_cpp_${family}_tests)
    taffy_test_platform_options(taffy_cpp_${family}_tests)
    add_test(NAME cpp_${family} COMMAND taffy_cpp_${family}_tests)
    set_tests_properties(cpp_${family} PROPERTIES TIMEOUT 180 LABELS "native")
endfunction()
taffy_cpp_test(primary tests/oracle_test.cpp tests/baseline_test.cpp
    tests/measure_more_test.cpp tests/geometry_handwritten_test.cpp tests/measure_helpers_test.cpp
    tests/cpp_backend/primary_backend.cpp tests/cpp_backend/helper_backend.cpp
    tests/support/rust_backend.cpp tests/support/measure_helpers.cpp)
taffy_cpp_test(internal tests/internal_oracle_test.cpp tests/cpp_backend/internal_backend.cpp)
taffy_cpp_test(value tests/value_unit_test.cpp tests/cpp_backend/value_backend.cpp)
taffy_cpp_test(custom_tree tests/custom_tree_test.cpp tests/cpp_backend/custom_tree_backend.cpp)
taffy_cpp_test(private tests/private_suite_test.cpp tests/cpp_backend/private_value_backend.cpp
    tests/cpp_backend/private_grid_backend.cpp tests/cpp_backend/private_occupancy_backend.cpp)
target_compile_definitions(taffy_cpp_private_tests PRIVATE TAFFY_NATIVE_CPP_REPRESENTATION TAFFY_PRIVATE_NATIVE_COUNTERCHECKS)
target_link_libraries(taffy_cpp_private_tests PRIVATE nlohmann_json::nlohmann_json)
# Retained test fragments use compact lambdas that trigger this Clang diagnostic.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set_source_files_properties(tests/cpp_backend/private_value_backend.cpp PROPERTIES
        COMPILE_OPTIONS -Wno-error=misleading-indentation)
endif()
taffy_cpp_test(tree tests/tree_suite_test.cpp tests/cpp_backend/tree_backend.cpp
    tests/cpp_backend/tree_cache/cache_backend.cpp)
# Fixture callback count compares unsigned with a pinned signed expectation.
if(NOT MSVC)
    target_compile_options(taffy_cpp_tree_tests PRIVATE -Wno-error=sign-compare)
endif()
taffy_cpp_test(behavior tests/behavior_suite_test.cpp tests/behavior/grid.cpp
    tests/behavior/replaced.cpp tests/behavior/position.cpp tests/cpp_backend/behavior_backend.cpp)
add_test(NAME cpp_behavior_source_ignored COMMAND taffy_cpp_behavior_tests "[upstream-ignored]")
set_tests_properties(cpp_behavior_source_ignored PROPERTIES LABELS "native;source-ignored" TIMEOUT 180)
# Despite its historical name, rust_backend.cpp is a C++ RAII wrapper over the C
# facade supplied above; it links no Rust archive or runtime in these targets.
taffy_cpp_test(documentation tests/documentation_test.cpp tests/cpp_backend/primary_backend.cpp
    tests/support/rust_backend.cpp)
target_compile_definitions(taffy_cpp_documentation_tests PRIVATE TAFFY_DOCUMENTATION_SOURCE_ROOT="${PROJECT_SOURCE_DIR}")
foreach(family foundation grid flex block parse)
    taffy_cpp_test(${family} tests/cpp_${family}_test.cpp)
endforeach()
target_sources(taffy_cpp_foundation_tests PRIVATE tests/cpp_public_headers_test.cpp
    tests/production_tree_regressions.cpp)
target_sources(taffy_cpp_grid_tests PRIVATE tests/cpp_regression_test.cpp
    tests/production_names_regressions.cpp tests/production_grid_regressions.cpp)
target_sources(taffy_cpp_flex_tests PRIVATE tests/production_layout_regressions.cpp)
target_sources(taffy_cpp_parse_tests PRIVATE tests/production_parse_regressions.cpp)

# Compile each public entry-point header on its own, without a lucky include order.
# These compile-only checks need no generator script or additional test dependency.
file(GLOB _taffy_public_headers CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/include/taffy/*.hpp")
set(_taffy_header_checks)
foreach(header IN LISTS _taffy_public_headers)
    get_filename_component(name "${header}" NAME)
    set(check "${PROJECT_BINARY_DIR}/public_header_checks/${name}.cpp")
    file(GENERATE OUTPUT "${check}" CONTENT "#include <taffy/${name}>\nstatic_assert(true);\n")
    list(APPEND _taffy_header_checks "${check}")
endforeach()
add_library(taffy_public_header_checks OBJECT ${_taffy_header_checks})
target_link_libraries(taffy_public_header_checks PRIVATE taffy::taffy)
set_target_properties(taffy_public_header_checks PROPERTIES CXX_EXTENSIONS OFF)
taffy_project_options(taffy_public_header_checks)
unset(_taffy_public_headers)
unset(_taffy_header_checks)
unset(header)
unset(name)
unset(check)
# This production-only allocation check has its own process-local allocator.
add_executable(taffy_cpp_child_access_tests tests/cpp_child_access_test.cpp)
target_link_libraries(taffy_cpp_child_access_tests PRIVATE taffy::taffy)
taffy_project_options(taffy_cpp_child_access_tests)
taffy_test_platform_options(taffy_cpp_child_access_tests)
add_test(NAME cpp_child_access COMMAND taffy_cpp_child_access_tests)
set_tests_properties(cpp_child_access PROPERTIES TIMEOUT 30 LABELS "native")

set(xml_corpus "${PROJECT_SOURCE_DIR}/tests/fixtures/xml")
add_executable(taffy_cpp_xml_runner tests/xml/runner.cpp tests/xml/measure.cpp
    tests/cpp_backend/xml_backend.cpp)
target_compile_definitions(taffy_cpp_xml_runner PRIVATE XML_DEFAULT_CORPUS="${xml_corpus}"
    XML_BACKEND_LABEL="native-cpp-xml-with-cpp-intrinsic")
target_link_libraries(taffy_cpp_xml_runner PRIVATE taffy::taffy pugixml::pugixml)
taffy_project_options(taffy_cpp_xml_runner)
taffy_test_platform_options(taffy_cpp_xml_runner)
add_test(NAME cpp_xml COMMAND taffy_cpp_xml_runner --report "${PROJECT_BINARY_DIR}/cpp-xml-report.json")
set_tests_properties(cpp_xml PROPERTIES TIMEOUT 300 LABELS "native")
