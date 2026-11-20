#pragma once

#include <string>

std::string AIHttpPostJson(
    const std::string& url,
    const std::string& bearerToken,
    const std::string& jsonBody,
    std::string& error);

std::string AIGetExecutablePath();
std::string AIGetAppDataPath();
