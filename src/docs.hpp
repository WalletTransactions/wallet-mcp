// Служебные пути без аутентификации (спека service-endpoints): /healthz,
// /version, /metrics, /openapi.yaml и /docs (Swagger UI из вендоренного
// бандла third_party/swagger-ui/, без CDN — LAN-only MVP).
//
// openapi.yaml и swagger-ui читаются с диска по относительному пути (рабочая
// директория — рядом с исполняемым файлом): CMakeLists.txt копирует их туда
// при сборке (POST_BUILD), Containerfile — при сборке образа.
#pragma once

#include "httplib.h"

namespace docsapi {

void register_routes(httplib::Server& svr);

} // namespace docsapi
