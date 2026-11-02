add_executable(wallet_mcp_tests
    test_main.cpp
    test_access.cpp
    test_auth_client.cpp
    test_dates.cpp
    test_decimal.cpp
    test_mcp.cpp
    test_mcp_http.cpp
    test_tools.cpp
)
target_include_directories(wallet_mcp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(wallet_mcp_tests PRIVATE wallet_mcp_core)

add_test(NAME wallet_mcp_unit_tests COMMAND wallet_mcp_tests)
