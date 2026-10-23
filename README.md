# Reaper Trainer

该项目由AI生成，README也由AI生成

## 编译说明

需要 Windows x64、Visual Studio 2026 的 C++ 桌面开发组件及 Windows SDK，以及 CMake 4.2 或更高版本。首次构建需要能够访问 `NuGet.Config` 中的包源；Windows App SDK 和 C++/WinRT 由 CMake 的 NuGet 包恢复流程按项目声明的版本自动恢复，不依赖固定安装目录、vcpkg 或预先存在的本地缓存。

clone 后，在项目根目录配置并编译：

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

发布构建使用：

```powershell
cmake --build --preset vs2026-release
```

解决方案和两个 `.vcxproj` 均由 CMake 生成在 `build\vs2026\` 中；源码目录不保留 Visual Studio 项目、解决方案或额外构建脚本。`build`、`bin` 和 NuGet 缓存均由配置或构建流程正常生成，不需要从其他环境复制。

如果移动项目时一并复制了旧 CMake 缓存，先运行 `cmake --fresh --preset vs2026`，再编译；不要直接复用移动前生成的解决方案。

主项目 `ReaperTrainer` 包含程序的全部非测试源码、WinUI 文件和配置文件。Debug 和 Release 程序分别输出至 `bin\Debug\` 和 `bin\Release\`。运行时配置从可执行文件所在目录读取，不依赖启动时的工作目录。

`cmake` 目录中的 `.props` 和 `.targets` 是 WinUI 构建规则，不是 Visual Studio 项目文件，由 CMake 生成的主项目自动导入。

## 测试使用说明

配置并编译后，在项目根目录运行对应配置的测试：

```powershell
ctest --preset vs2026-debug
```

或运行发布配置测试：

```powershell
ctest --preset vs2026-release
```

测试模块为 `ReaperTrainerTests`，仅包含测试源码，通过链接主项目生成的运行库复用工具实现，由 CTest 调用并读取 `config\trainer.yaml`。测试程序同样输出至对应的 `bin\Debug\` 或 `bin\Release\`。
