# Reaper Trainer

该项目由AI生成，README也由AI辅助生成。

适用于Microsoft Store中的Reaper 1.4.2-204，由Hexage开发的手机RPG游戏。

## 编译说明

需要 Windows x64、Visual Studio 2026 的 C++ 桌面开发组件及 Windows SDK，以及 CMake 4.2 或更高版本。首次构建需要能够访问 `NuGet.Config` 中的包源；Windows App SDK 和 C++/WinRT 由 CMake 的 NuGet 包恢复流程按项目声明的版本自动恢复。


```powershell
cmake --preset vs2026
cmake --build --preset vs2026-debug
```

发布构建使用：

```powershell
cmake --build --preset vs2026-release
```

## 测试使用说明

配置并编译后，在项目根目录运行对应配置的测试：

```powershell
ctest --preset vs2026-debug
```

或运行发布配置测试：

```powershell
ctest --preset vs2026-release
```
