// Счётчик с двумя лейблами — mcp_tool_calls_total{tool,result} (спека
// service-endpoints). util/metrics из wallet-common умеет счётчики только с
// одним лейблом; код нужен одному сервису, поэтому он здесь, а не в
// wallet-common. GET /metrics отдаёт metrics::render() + metrics2::render().
#pragma once

#include <string>

namespace metrics2 {

// label-значения — только литералы из кода (имена инструментов из реестра,
// исходы), не пользовательский ввод.
void inc_counter(const std::string& name, const std::string& help, const std::string& label1,
                 const std::string& value1, const std::string& label2, const std::string& value2);

// Текстовый формат Prometheus 0.0.4, как у metrics::render().
std::string render();

} // namespace metrics2
