include_guard(GLOBAL)
include(FetchContent)

option(TAFFY_FETCH_TEST_DEPENDENCIES
    "Download missing test dependencies after checking existing targets and installed packages" ON)

# The embedding application owns dependency versions. Release archives below are
# standalone fallbacks, not exact-version requirements on parent-provided targets.
function(taffy_test_dependency name target minimum_version)
    if(TARGET "${target}")
        return()
    endif()
    if(name STREQUAL "Catch2" AND TARGET Catch2::Catch2)
        message(FATAL_ERROR "Taffy tests need Catch2::Catch2WithMain as well as Catch2::Catch2")
    endif()
    find_package(${name} ${minimum_version} CONFIG QUIET)
    if(TARGET "${target}")
        return()
    endif()
    if(NOT TAFFY_FETCH_TEST_DEPENDENCIES)
        message(FATAL_ERROR
            "Taffy tests need ${target}. Provide it before add_subdirectory(taffy), "
            "install ${name} >= ${minimum_version}, or enable TAFFY_FETCH_TEST_DEPENDENCIES.")
    endif()

    # Function-local defaults affect only a dependency Taffy actually downloads.
    # Honor parent options; never FORCE cache settings or alter existing targets.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    if(name STREQUAL "Catch2")
        FetchContent_Declare(Catch2
            URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
            URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        foreach(setting CATCH_BUILD_TESTING CATCH_INSTALL_DOCS CATCH_INSTALL_EXTRAS)
            if(NOT DEFINED ${setting})
                set(${setting} OFF)
            endif()
        endforeach()
        set(owned_targets Catch2 Catch2WithMain)
    elseif(name STREQUAL "pugixml")
        FetchContent_Declare(pugixml
            URL https://github.com/zeux/pugixml/releases/download/v1.15/pugixml-1.15.tar.gz
            URL_HASH SHA256=655ade57fa703fb421c2eb9a0113b5064bddb145d415dd1f88c79353d90d511a
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        if(NOT DEFINED PUGIXML_INSTALL)
            set(PUGIXML_INSTALL OFF)
        endif()
        set(owned_targets pugixml-static pugixml-shared)
    elseif(name STREQUAL "nlohmann_json")
        FetchContent_Declare(nlohmann_json
            URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
            URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
            DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
        foreach(setting JSON_BuildTests JSON_Install)
            if(NOT DEFINED ${setting})
                set(${setting} OFF)
            endif()
        endforeach()
        set(owned_targets)
    else()
        message(FATAL_ERROR "Unknown Taffy test dependency: ${name}")
    endif()
    FetchContent_MakeAvailable(${name})
    if(NOT TARGET "${target}")
        message(FATAL_ERROR "${name} did not provide required target ${target}")
    endif()

    # Mark/instrument only dependencies fetched here, never imported/parent targets.
    foreach(dep IN LISTS owned_targets)
        if(TARGET "${dep}")
            get_target_property(imported "${dep}" IMPORTED)
            if(NOT imported)
                set_property(TARGET "${dep}" PROPERTY TAFFY_FETCHED_TEST_DEPENDENCY TRUE)
                if(TAFFY_SANITIZE AND COMMAND taffy_project_options)
                    taffy_project_options("${dep}")
                endif()
            endif()
        endif()
    endforeach()
endfunction()
