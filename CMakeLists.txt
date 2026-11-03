cmake_minimum_required(VERSION 3.16)
project(wallet_mcp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Общий код сервисов (util/, expense/, clients/) — копия репозитория
# wallet-common, обновляется его scripts/vendor.sh; руками не править.
set(WALLET_COMMON_DIR ${CMAKE_SOURCE_DIR}/third_party/wallet-common)

# OpenSSL — SHA-256 токена (ключ кешей) и UUID v4 (Mcp-Session-Id). Базы у
# сервиса нет, поэтому и libpq нет; JWT сервис не проверяет (design D3).
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# Версия — тот же приём, что у остальных сервисов (см. CMakeLists.txt
# wallet-report, "Версия приложения"): номер из VERSION, хэш — из
# -DGIT_COMMIT (Containerfile) или из git при нативной сборке.
file(STRINGS ${CMAKE_SOURCE_DIR}/VERSION APP_VERSION_LINES LIMIT_COUNT 1)
list(GET APP_VERSION_LINES 0 APP_VERSION)
string(STRIP "${APP_VERSION}" APP_VERSION)

# Сборка — тег образа (`0.1.1-rc.2`), отдельно от VERSION: по VERSION сверяет
# выкат smoke в wallet-infra deploy.sh. Release передаёт тег через
# --build-arg APP_BUILD (Containerfile → -DAPP_BUILD); сборка без него
# помечается `-dev`, чтобы её не спутать с релизной (GET /version, поле
# build; wallet-gateway openspec/changes/service-versions, design D1/D2).
if(NOT DEFINED APP_BUILD OR "${APP_BUILD}" STREQUAL "")
    set(APP_BUILD "${APP_VERSION}-dev")
endif()

if(NOT DEFINED GIT_COMMIT OR "${GIT_COMMIT}" STREQUAL "")
    find_program(GIT_EXECUTABLE git)
    set(GIT_COMMIT "unknown")
    if(GIT_EXECUTABLE)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} describe --tags --always --dirty --abbrev=8
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            OUTPUT_VARIABLE GIT_COMMIT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE GIT_DESCRIBE_RESULT
            ERROR_QUIET
        )
        if(NOT GIT_DESCRIBE_RESULT EQUAL 0 OR "${GIT_COMMIT}" STREQUAL "")
            set(GIT_COMMIT "unknown")
        endif()
    endif()
endif()
message(STATUS "wallet_mcp version: ${APP_VERSION}, build ${APP_BUILD} (${GIT_COMMIT})")

configure_file(
    ${CMAKE_SOURCE_DIR}/src/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/version.hpp
    @ONLY
)

# Всё, кроме main.cpp и docs.cpp, — библиотека: её же линкуют юнит-тесты.
add_library(wallet_mcp_core STATIC
    src/access.cpp
    src/auth_client.cpp
    src/dates.cpp
    src/decimal.cpp
    src/directory.cpp
    src/mcp.cpp
    src/mcp_http.cpp
    src/metrics2.cpp
    src/tools.cpp
    ${WALLET_COMMON_DIR}/clients/reports_client.cpp
    ${WALLET_COMMON_DIR}/expense/resolver.cpp
    ${WALLET_COMMON_DIR}/expense/strict.cpp
    ${WALLET_COMMON_DIR}/expense/text.cpp
    ${WALLET_COMMON_DIR}/util/base64.cpp
    ${WALLET_COMMON_DIR}/util/crypto.cpp
    ${WALLET_COMMON_DIR}/util/dotenv.cpp
    ${WALLET_COMMON_DIR}/util/log.cpp
    ${WALLET_COMMON_DIR}/util/metrics.cpp
    ${WALLET_COMMON_DIR}/util/uuid.cpp
)
target_include_directories(wallet_mcp_core PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
    ${WALLET_COMMON_DIR}
    ${CMAKE_BINARY_DIR}/generated
)
target_compile_definitions(wallet_mcp_core PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
)
target_compile_options(wallet_mcp_core PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
)
target_link_libraries(wallet_mcp_core PUBLIC
    OpenSSL::Crypto
    Threads::Threads
)
if(WIN32)
    target_link_libraries(wallet_mcp_core PUBLIC ws2_32 crypt32)
endif()

add_executable(wallet_mcp
    src/main.cpp
    src/docs.cpp
)
target_link_libraries(wallet_mcp PRIVATE wallet_mcp_core)

add_custom_command(TARGET wallet_mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_SOURCE_DIR}/openapi.yaml
            $<TARGET_FILE_DIR:wallet_mcp>/openapi.yaml
    COMMENT "Копирую openapi.yaml рядом с wallet_mcp (GET /openapi.yaml, /docs)"
)
add_custom_command(TARGET wallet_mcp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/third_party/swagger-ui
            $<TARGET_FILE_DIR:wallet_mcp>/vendor/swagger-ui
    COMMENT "Копирую вендоренный Swagger UI рядом с wallet_mcp (GET /docs)"
)

# Юнит-тесты — без внешней сети: чистые функции, фейковые google-auth и
# wallet-report (в том числе на локальном httplib-сервере). В образе
# запускаются стадией `test` Containerfile:
#   docker build --target test -f wallet-mcp/Containerfile wallet-mcp
option(WALLET_MCP_BUILD_TESTS "Собирать юнит-тесты wallet_mcp_tests" ON)
if(WALLET_MCP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
