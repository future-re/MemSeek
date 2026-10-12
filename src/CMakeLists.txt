# Keep the current scanning library source list explicit.
set(MEMSEEK_LIBRARY_SOURCES
    app/scan_session.cpp
    memory/change.cpp
    memory/reader.cpp
    memory/region.cpp
    memory/scan.cpp
    memory/snapshot.cpp
)

add_library(memseek_lib STATIC ${MEMSEEK_LIBRARY_SOURCES})

# Boost.Asio's thread_pool (header-only) parallelizes region scans. Keep it
# PRIVATE so consumers of memseek_lib do not need Boost headers.
find_package(Boost CONFIG REQUIRED)

target_compile_features(memseek_lib PUBLIC cxx_std_23)

target_include_directories(memseek_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(memseek_lib PRIVATE Boost::headers)

add_executable(memseek cli/main.cpp)
target_link_libraries(memseek PRIVATE memseek_lib CLI11::CLI11)
