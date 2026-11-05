# XREAL Windows 3DoF 0.1.1

首次 GitHub Release，Windows x64 / Visual C++ Release / 静态 C++ 运行库。

- 提供 Air 2 Ultra 的 HID 姿态读取、DXGI 桌面采集和 Direct3D 11 的 3DoF 悬停屏幕。
- 包含显式静置零偏校准及已校准后的保守微调。首次使用请先执行“静置校准（5秒）”。
- 新构建通过 32 项自检，包括协议、姿态、校准、D3D 渲染和真实 DXGI 桌面采集。
- ZIP 只包含 EXE、中文使用说明和第三方许可，不包含个人设置、校准文件或运行日志。

解压 `XREAL-StableScreen-v0.1.1-win-x64.zip`，运行 `XrealStableScreen.exe`。无需 Android Server、Unity、Python 或 .NET SDK。
这是 3DoF 原型；投影参数为近似值，不能保证长期航向绝对无漂移。此次没有新增眼镜佩戴实测。

用 `SHA256SUMS` 校验下载内容。
