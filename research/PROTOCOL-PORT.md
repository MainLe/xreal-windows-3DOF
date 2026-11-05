# Android 服务接口到 Windows 的移植证据

来源：用户提供的 ControlGlasses 3.1.0.20251118115716，现有 `ServerAppResearch/API-MAP.md`。

Android 的 Java 调用 `Startup.nativeImuInit`、`Control.nativeSet2D3DMode` 经 `libnr_service.so` 进入 ARM64 的 `libnr_glasses_api.so` / `libnr_libusb.so`。Java/Binder 接口及二进制跟踪引擎不能由 x64 Windows 直接装载。此版本移植 USB 协议与应用行为，没有声称具备官方 Windows 跟踪运行库。

`libnr_glasses_api.so` SHA256：`4493b6ea93833ab63848625bbc3c4d45a41bc6cecd6de704e209f95edd56ec42`。

| Android 原生证据 | Windows 实现 |
|---|---|
| `cmd_build_imu` @ `0xa92e0`：0xAA、CRC32、长度含 2 字节长度与 1 字节命令、payload @ 8 | `tracking.hpp::imuCommand` |
| `cmd_build_sdk` @ `0xa91dc`：0xFD、CRC32、requestId @ 7、cmd @ 15、payload @ 22 | `tracking.hpp::mcuCommand` |
| 库内 JSON：`MSG_R_DISPLAY_2D_3D=0x07`、`MSG_W_DISPLAY_2D_3D=0x08` | 读取原显示模式、切换临时模式 3、正常退出尝试恢复 |
| 原生 HID 与 libusb 后端 | Windows SetupDi / HidD / HidP / overlapped ReadFile、WriteFile |

Air 2 Ultra 本机枚举为 `3318:0426`：MI_00 控制、MI_02 IMU；Windows input/output reports 均为 513 字节。Windows 写入额外的零 report ID 并补齐 report 长度；读取移除零 report ID。保留 MI_01 与 MI_08，未发送固件/持久显示模式写入命令。

IMU 开关 0x19，标定长度/分块 0x14/0x15，原始 0x01 0x02 报告、24-bit 有符号字段与坐标映射参考 MIT 的 ar-drivers-rs，并通过本机硬件成功解析。样本时间戳按纳秒处理；重复/倒序与大于 50ms 间隔不积分；无效/超过 120ms 的数据不当作已跟踪状态输出。

协议参考文件 `nreal_air.rs` SHA256：`4b1140b8883bcc7c3d5708d4103c38e190c3a76d2012f43f573d09cdbebb343b`。

`tests/compare_android_packets.py` 在 Unicorn 中执行附件中两段真实序列化函数。验证 IMU 开/关、标定长度/块、显示查询和 SBS60 六条完整 64-byte payload 一致。证据为成品目录的 `android-protocol-comparison.json`。这个比对只证明序列化一致，不代替命令语义与原生姿态算法的实机验证。

Windows 实机中发现：0x08→模式3 成功后，显卡把眼镜与内屏放在同一 source（复制拓扑），因此 DXGI 只枚举一台可采集显示器。`SetDisplayConfig(SDC_APPLY|SDC_TOPOLOGY_EXTEND)` 返回0后恢复为独立 source，眼镜为3840×1080，桌面采集与渲染约60fps。程序在 SBS 请求后等待3秒执行相同恢复并刷新列表。用户已确认无预测版3DoF方向与显示正常，轻微转头延迟保留为观察项。

融合、双眼近似投影、桌面采集和 UI 是本项目实现；没有移植/装载 XREAL Android 的闭源融合和渲染二进制。后续如需完全一致的光学补偿和预测，需要继续恢复相应参数/算法或取得 Windows 版本运行库。
