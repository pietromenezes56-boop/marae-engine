#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

class ProjectMcpServer
{
public:
    static int Run(const std::filesystem::path& projectRoot,
                   std::istream& input,
                   std::ostream& output,
                   std::ostream& diagnostics);
};

