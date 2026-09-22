# Keep the library source list explicit. In particular, main.cpp belongs only
# to the executable; putting it in the library makes GoogleTest binaries pick
# up the application's main() instead of GTest's main().
set(MEMSEEK_LIBRARY_SOURCES
    memory_reader.cpp
    memory_region.cpp
    memory_scan.cpp
    memseek.cpp
    scanmem.cpp
)

add_library(memseek_lib STATIC ${MEMSEEK_LIBRARY_SOURCES})

find_package(ICU REQUIRED COMPONENTS uc i18n)

target_compile_features(memseek_lib PUBLIC cxx_std_23)

target_include_directories(memseek_lib PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)

target_link_libraries(memseek_lib PUBLIC ICU::uc ICU::i18n)

add_executable(memseek main.cpp
    memory_reader.cpp)
target_link_libraries(memseek PRIVATE memseek_lib)
