add_library(test_common INTERFACE)

target_link_libraries(test_common 
    INTERFACE 
        memseek_lib 
        GTest::gtest_main
)

target_include_directories(test_common 
    INTERFACE 
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)