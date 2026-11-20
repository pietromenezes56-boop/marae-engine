#pragma once

#include <filesystem>
#include <array>

#ifdef _WIN32
// Declare this small kernel32 API directly so this utility header does not
// pull windows.h into engine translation units (the engine has a GUID type).
struct HINSTANCE__;
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameW(
    HINSTANCE__* module, wchar_t* filename, unsigned long size);
#endif

namespace RuntimePaths
{
    inline std::filesystem::path ExecutableDirectory()
    {
#ifdef _WIN32
        std::array<wchar_t, 32768> modulePath{};
        const unsigned long length = GetModuleFileNameW(
            nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
        if (length > 0 && length < modulePath.size())
            return std::filesystem::path(std::wstring(modulePath.data(), length)).parent_path();
#endif
        return std::filesystem::current_path();
    }

    inline std::filesystem::path ResolveEngineAsset(
        const char* sourceFile,
        const std::filesystem::path& relativePath)
    {
        const std::filesystem::path executablePath =
            ExecutableDirectory() / relativePath;
        if (std::filesystem::exists(executablePath))
            return executablePath;

        const std::filesystem::path workingDirectoryPath =
            std::filesystem::current_path() / relativePath;
        if (std::filesystem::exists(workingDirectoryPath))
            return workingDirectoryPath;

        if (sourceFile && *sourceFile)
        {
            const std::filesystem::path sourceRoot =
                std::filesystem::path(sourceFile).parent_path().parent_path();
            const std::filesystem::path sourcePath = sourceRoot / relativePath;
            if (std::filesystem::exists(sourcePath))
                return sourcePath;
        }

        return executablePath;
    }
}
