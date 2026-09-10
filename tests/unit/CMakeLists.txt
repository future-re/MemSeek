file(GLOB UNIT_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(unit_tests ${UNIT_TEST_SOURCES})
target_link_libraries(unit_tests PRIVATE test_common)

gtest_discover_tests(unit_tests
    DISCOVERY_MODE POST_BUILD
    PROPERTIES
        LABELS "unit"
)