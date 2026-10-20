# Reaper Trainer

该项目由AI生成

## 构建

需要 Windows x64、Visual Studio 2026 的 C++ 桌面开发组件、Windows App SDK 1.8、CMake 4.2 或更高版本。

```powershell
cmake --preset vs2026
cmake --build --preset vs2026-release
ctest --preset vs2026-release
```

也可以在 Visual Studio 2026 中打开本目录的 `CMakeLists.txt`，或直接打开主项目 `ReaperTrainer.vcxproj`。`ReaperTrainer` 项目集中编译 WinUI、游戏进程探测、内存访问和游戏数据工具；独立的 `ReaperTrainerTests` 测试模块单独编译并验证同一组工具源码。CMake 解决方案将 `ReaperTrainer` 设为启动主项目。
