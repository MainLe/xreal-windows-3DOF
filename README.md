
# XREAL Windows 3DoF

Air 2 Ultra 的 Windows x64 桌面稳屏原型，当前版本 **0.1.1**。使用 HID/SetupAPI 读取 IMU 和标定，DXGI 采集桌面，Direct3D 11 渲染 3DoF 悬停屏幕。无需 Android 手机、Android Server 或 Unity。

## 运行

解压 [Windows 0.1.1 下载包](downloads/XREAL-StableScreen-v0.1.1-win-x64.zip)，运行 `XrealStableScreen.exe`。连接眼镜、切换 SBS 60Hz、选择独立的来源/眼镜显示器后启动。首次使用先做“静置校准（5秒）”。详细操作、已知限制及验证范围见 [中文说明](README.zh-CN.md)。

## 构建与检查

需要 Visual Studio 2022 C++ 桌面开发工具、Windows SDK 10.0.22621.0 和 CMake 3.20+。在仓库根目录运行：

```powershell
.\build.ps1
.\build.ps1 -Test
```

成品输出到 `dist/XREAL-StableScreen-v0.1.1`。测试覆盖协议、姿态数值、零偏校准、D3D 渲染及真实 DXGI 桌面采集，后者需要交互桌面。它不自动证明眼镜的端到端跟踪或长期稳定性。

`tests/compare_android_packets.py` 是额外的离线原生命令包比对工具，需要另行提供原 Android 研究工作区、对应原生库和 Python `unicorn` / `pyelftools` 依赖。设置 `XREAL_ANDROID_PROJECT` 指向该工作区，再传入包含 `self-test.json` 的成品目录；日常构建与运行不需要这些 Android 输入。

源码分支不含个人设置、眼镜序列号对应的校准、运行日志或诊断快照。下载包只包含 EXE、使用说明及第三方许可。成品校验值见 [release](release/artifact.json)，第三方协议参考和 JSON 库许可见 [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt)。
