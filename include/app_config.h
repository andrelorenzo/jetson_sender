#pragma once

#include "app_types.h"

namespace rsapp {

bool LoadAppConfig(int argc, char **argv, AppConfig *config);
void LogAppConfig(const AppConfig &config);

} // namespace rsapp
