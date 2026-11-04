// Счётчики с несколькими лейблами — mcp_tool_calls_total{tool,result,credential}
// и mcp_token_exchange_total{result,kind} (спека service-endpoints, mcp-access).
// util/metrics из wallet-common умеет счётчики только с одним лейблом; код
// нужен одному сервису, поэтому он здесь, а не в wallet-common. GET /metrics
// отдаёт metrics::render() + metrics2::render().
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace metrics2 {

using Labels = std::vector<std::pair<std::string, std::string>>;

// label-значения — только литералы из кода (имена инструментов из реестра,
// исходы, вид учётных данных), не пользовательский ввод. Имена и порядок
// лейблов у одного семейства — одинаковые при каждом вызове.
void inc_counter(const std::string& name, const std::string& help, const Labels& labels);

// Текстовый формат Prometheus 0.0.4, как у metrics::render().
std::string render();

} // namespace metrics2
