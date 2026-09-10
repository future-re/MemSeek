include(FetchContent)
find_package(GTest 1.14 CONFIG QUIET)
if(NOT TARGET GTest::gtest_main)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_subdirectory(common)

add_subdirectory(unit)
add_subdirectory(integration)