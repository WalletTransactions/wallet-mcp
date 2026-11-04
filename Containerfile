# syntax=docker/dockerfile:1
#
# Multi-stage build wallet-mcp — тот же шаблон, что у wallet-ai и
# wallet-telegram: deps -> build (образ), deps -> test (юнит-тесты,
# `--target test`), runtime. Базы нет — libpq не нужен; OpenSSL — SHA-256,
# UUID v4 и проверка RS256 входящего JWT (add-jwt-access). tzdata в рантайме — пояс MCP_TZ (design D7), curl — для
# healthcheck в compose (GET /healthz).

FROM debian:bookworm-slim AS deps

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        libssl-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY VERSION ./
COPY openapi.yaml ./
COPY src/ src/
COPY third_party/ third_party/

FROM deps AS build
# GIT_COMMIT — снаружи (`--build-arg`, см. compose.yaml wallet-infra): у
# сборки в контейнере нет .git.
ARG GIT_COMMIT=unknown
# APP_BUILD — тег образа (`0.1.1-rc.2`), его передаёт release.yml; пусто —
# CMake подставит `<VERSION>-dev` (GET /version, поле build).
ARG APP_BUILD=
ARG BUILD_JOBS=3
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DWALLET_MCP_BUILD_TESTS=OFF -DGIT_COMMIT="${GIT_COMMIT}" -DAPP_BUILD="${APP_BUILD}" \
    && cmake --build build --config Release -j"${BUILD_JOBS}"

FROM deps AS test
# Тестам нужна tzdata (проверка MCP_TZ по /usr/share/zoneinfo).
RUN apt-get update && apt-get install -y --no-install-recommends tzdata \
    && rm -rf /var/lib/apt/lists/*
COPY tests/ tests/
ARG BUILD_JOBS=3
RUN cmake -B build-tests -DCMAKE_BUILD_TYPE=Debug \
    && cmake --build build-tests --target wallet_mcp_tests -j"${BUILD_JOBS}" \
    && ctest --test-dir build-tests --output-on-failure

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libssl3 \
        tzdata \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /app --shell /usr/sbin/nologin appuser

WORKDIR /app
COPY --from=build /src/build/wallet_mcp /app/wallet_mcp
# openapi.yaml + Swagger UI читаются с диска при GET /openapi.yaml, /docs.
COPY openapi.yaml /app/openapi.yaml
COPY third_party/swagger-ui/ /app/vendor/swagger-ui/

# LOG_DIR не задан -> логи в stdout/stderr.
USER appuser
EXPOSE 8088

ENTRYPOINT ["/app/wallet_mcp"]
