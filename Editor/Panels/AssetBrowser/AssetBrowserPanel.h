#pragma once

#include <filesystem>
#include "../../../Assets/AssetDatabase/AssetDatabase.h"

class Application;

class AssetBrowserPanel
{
public:

    void Draw(
        Application& app
    );

    const AssetInfo* GetSelectedAsset() const;

private:

    const AssetInfo* m_SelectedAsset = nullptr;

    std::filesystem::path m_CurrentPath;
    std::string m_ProjectRoot;
};
