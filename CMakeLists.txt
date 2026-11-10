cmake_minimum_required(VERSION 3.25)
project(taffy_cpp VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/TaffyOptions.cmake)
include(cmake/TaffyLibrary.cmake)
taffy_project_options(taffy)

include(CMakePackageConfigHelpers)
install(TARGETS taffy EXPORT taffy_cppTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY include/ DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
    FILES_MATCHING PATTERN "*.hpp")
install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/taffy_cpp")
set(_taffy_config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/taffy_cpp")
configure_package_config_file(cmake/taffy_cppConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/taffy_cppConfig.cmake"
    INSTALL_DESTINATION "${_taffy_config_dir}")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/taffy_cppConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}" COMPATIBILITY SameMajorVersion)
install(EXPORT taffy_cppTargets NAMESPACE taffy:: DESTINATION "${_taffy_config_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/taffy_cppConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/taffy_cppConfigVersion.cmake" DESTINATION "${_taffy_config_dir}")

# Do not enable a parent's tests or fetch packages just because it embeds Taffy.
if(PROJECT_IS_TOP_LEVEL)
    include(CTest)
endif()
set(_taffy_default_cpp_tests "${PROJECT_IS_TOP_LEVEL}")
if(DEFINED BUILD_TESTING AND NOT BUILD_TESTING)
    set(_taffy_default_cpp_tests OFF)
endif()
option(TAFFY_BUILD_CPP_TESTS "Build the native C++ test suites" "${_taffy_default_cpp_tests}")
unset(_taffy_default_cpp_tests)
if(TAFFY_BUILD_CPP_TESTS AND (NOT DEFINED BUILD_TESTING OR BUILD_TESTING))
    enable_testing()
    include(cmake/CppTests.cmake)
endif()
