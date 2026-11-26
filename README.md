# Marae Engine

Marae Engine is a Windows game engine project with an integrated editor, 2D and 3D workflows, C# scripting, scene tools, audio, physics, lighting, and asset management.

This repository contains the source code and build dependencies. It does not include a ready-to-run installer or prebuilt editor. The project is under active development; expect incomplete features and compatibility changes.

## Build on Windows

1. Install Visual Studio with the C++ desktop workload and the MSVC toolset selected by MaraeEngine.vcxproj.
2. Install the .NET 8 SDK.
3. Open MaraeEngine.slnx or build MaraeEngine.vcxproj for x64.
4. Restore the C# scripting project when prompted by MSBuild.

The solution is currently configured for the v145 MSVC toolset. If that toolset is unavailable, install it or change the project to a toolset available on your machine.

The editor uses the bundled third-party dependencies under ThirdParty/. Their licenses and notices are listed in THIRD_PARTY_NOTICES.md.

## Project data

User projects, scenes, settings, generated scripts, and build output are intentionally not part of this repository. Create a fresh project from the editor after building it.

## Rendering status

OpenGL is the established renderer. Vulkan support is experimental and may fall back to OpenGL based on the detected GPU and driver. This source snapshot does not claim full Vulkan parity or a packaged release build.

## Local AI integration (MCP)

The source includes an opt-in local MCP server for AI clients. It runs over `stdio`, stays scoped to one project folder, and provides tools for inspecting scenes/assets and editing scene entities and C# scripts. It does not expose a shell, network access, or arbitrary filesystem access. See the [MCP setup](Docs/MCP.md) and the [complete MCP tools tutorial](Docs/MCP_TOOL_TUTORIAL.md). Scene edits are written to disk with backups; close the project in the editor before requesting MCP writes.

## Engine overview

Marae is a Windows-first, C++ game-engine project with 2D and 3D editor workflows, C# scripting, scene and hierarchy editing, physics, lighting, materials, animation, UI, audio, and local asset management. The project is under active development, and feature completeness varies by subsystem. OpenGL is the established renderer; Vulkan remains experimental and may fall back according to hardware and driver compatibility.

## License and brand

Original Marae Engine source code is licensed under the MIT License in LICENSE, except where a file or directory states otherwise. Third-party components retain their own licenses. The Marae name, logos, and other brand assets are not licensed by the MIT grant.

Project website: [Marae Engine](https://pietromenezes56-boop.github.io/marae-engine/)
