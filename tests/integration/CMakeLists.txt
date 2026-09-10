# file(GLOB INT_TEST_SOURCES CONFIGURE_DEPENDS "*.cpp")

# add_executable(integration_tests ${INT_TEST_SOURCES})
# target_link_libraries(integration_tests PRIVATE test_common)

# gtest_discover_tests(integration_tests
#     DISCOVERY_MODE POST_BUILD
#     PROPERTIES
#         LABELS "integration"
# )