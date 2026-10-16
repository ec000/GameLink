# GameLink 0.3 MVP

Windows 游戏联机客户端（C++20 / Qt 6）与 Go 控制平面。复用独立安装的官方 ZeroTier One，不实现 VPN 核心、不转发游戏包、不修改 Planet/Moon、系统默认路由或 DNS。

**当前交付：Phase 0 / 1 文档与调查工具完成，Phase 2 服务和客户端源码完成；Go、C++ 核心与 Qt 6 客户端 mock 集成已测试，真实网络未验证。Direct Required 不可用并明确拒绝。**

## 桌面界面（0.3 新增）

采用参考 Windows 11 的浅色桌面界面：左侧五页导航、圆角卡片、蓝色主操作、统一输入框与本地绘制的矢量图标。概览显示节点、虚拟 IP、房间与独立环境检测；游戏、房间、诊断和设置按操作分组。底部同步显示最新事件，各页都能看到操作失败原因，完整日志保留在网络诊断页。

默认窗口 1180×840，最小 960×680；内容较长时支持纵向滚动。仍使用 Qt 6 Widgets 与原生窗口边框，不需要额外 UI 框架。`GameLink-Windows-UI-Previews` CI artifact 提供五页与小窗口的渲染截图；截图中的环境/房间是测试数据，不能作为真实联机证据。

## ZeroTier 安装与服务检测（0.2 新增）

Home 分开展示安装检测、Windows 服务、本地 API 和节点 ONLINE/OFFLINE。每 5 秒只读查询 `ZeroTierOneService`，从服务注册路径及常见 Program Files 目录寻找程序；不依赖 Token，不启停/修复服务，不自动提权。

服务已停止、启动中、未注册、查询权限不足会分别显示；仅发现遗留数据目录不会认定已安装。非 Windows 平台显示此项不支持，仍保留 API 检测。自定义/便携安装未出现在服务或常见目录时需要人工核对。

API 诊断区显示凭证文件缺失/不可读/无效、非法端口、连接被拒绝、401/403、404、超时和异常 JSON/schema 等原因。服务 Running 与节点 ONLINE、玩家 DIRECT 分开判断。读取 `%ProgramData%/ZeroTier/One/zerotier-one.port` 后只提示端口；点击“使用检测到的 API 端口”才更新 GameLink 设置，不自动覆盖用户输入。Token 内容不会展示或记录。

## 已实现与边界

| 功能 | 当前情况 |
|---|---|
| 只读环境调查 | 本机已执行；VPS 脚本已提供，未远程执行 |
| 架构、API、Profile、策略与状态机 | 已落地，见下方文档 |
| Go 房间 / 认证 / 授权 / ACL / TTL / 限流 | 已实现，单实例、私有网络池、持久化撤销意图、重启清理 |
| 安装 / Windows 服务 / API 诊断 | 独立只读检测与具体错误提示；Windows CI 已通过原生查询与回归测试 |
| Qt GUI 五个页面 | 已在 macOS / Qt 6.11.2 编译并通过离屏集成测试；Windows CI 见 Actions |
| 本地 ZT API / join / per-node 状态 | 源码已实现；不依赖文本列宽，未与真实 ZT 集成验证 |
| 虚拟 IPv4 UDP echo / RTT / jitter / loss | 源码已实现，双端手动启用，仅绑定选定虚拟 IP；未实机验证 |
| 游戏路径 / Windows 进程检测 / 启动 | 手动路径、tasklist、插件示例；尚未游戏实测 |
| Warcraft III / DST / Overcooked! 2 | 配置均 unverified；Overcooked 只观察，不能替换 Steam P2P/SDR |
| Strict Direct / WFP / broker | 未实现，GUI/API 拒绝严格模式，不能保证不经 relay |
| 自动游戏扫描 / LAN 广播验证 / 自启动 | 未实现 |
| Windows 安装包 | Windows CI 已成功生成 Qt 6.8.3 ZIP 并验证独立启动/TLS；EXE 安装器/签名未验证 |

## 快速验证：不需要 Qt 或 ZeroTier

从工程根目录执行，要求 CMake ≥ 3.21、C++20、Go ≥ 1.22、Python ≥ 3.9：

```sh
cmake -S . -B build-core -DGAMELINK_BUILD_GUI=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-core
ctest --test-dir build-core --output-on-failure
python3 tests/inspection_tests.py
cd server
go test -race -cover ./...
go vet ./...
go build -o ../build/server/gamelink-server ./cmd/gamelink-server
cd ..
python3 tests/server_smoke.py --binary build/server/gamelink-server
```

受限环境中可设置 `GOCACHE=/tmp/gamelink-go-cache`；HTTP 集成测试需要允许监听本机临时端口。mock 测试不证明实际 NAT 穿透。

本次生成的本机二进制位于 `build/server/gamelink-server`；Linux 交叉编译产物为 `build/server/gamelink-server-linux-amd64` 和 `build/server/gamelink-server-linux-arm64`，未在 Linux 实机运行。

## 启动本机 mock 服务

```sh
python3 scripts/make_demo_config.py --directory build/demo
./build/server/gamelink-server --mock --config build/demo/server.json
```

测试 Alice/Bob 凭证位于权限 0600 的 `build/demo/credentials.json`。仅用于本机 mock：不连接真实 Controller、不分配真实虚拟 IP、不宣称有任何 P2P。GUI Settings 中填入 GameLink token。mock 状态文件不能切换为真实运行，生产必须使用独立路径。

## Qt 6 客户端验证与 CI

保持 Qt 6，最低构建要求 6.4。当前本机使用 Qt 6.11.2；Windows CI 使用固定 Qt 6.8.3 / MSVC 2022。

```sh
cmake -S . -B build/qt6 -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/clang_64 -DCMAKE_BUILD_TYPE=Debug
cmake --build build/qt6 --parallel 4
ctest --test-dir build/qt6 --output-on-failure
QT_QPA_PLATFORM=offscreen ./build/qt6/GameLink --self-test
```

Homebrew Qt 路径可使用 `/opt/homebrew/opt/qtbase`。启用测试时需要 Qt Test 模块。CTest 包含核心、Qt 客户端 HTTP/房间集成和程序启动三组检查；mock 不操作真实 ZeroTier。`--self-test` 检查五个页面、三个 profile 和 TLS 后端，随后退出；不创建/授权/加入网络。

[GitHub Actions](https://github.com/ec000/GameLink/actions/workflows/build.yml) 编译并测试 Linux 服务和 Windows Qt 6 客户端。通过的 Windows run 中可下载 `GameLink-Windows-Qt6` artifact，包含 ZIP 测试包；不是已签名发行版。[0.2.0 全部通过的运行](https://github.com/ec000/GameLink/actions/runs/38910834948)。打包后移除 SDK 的 PATH/插件环境，再检查程序启动和 TLS，避免依赖开发机环境。

## Windows GUI 编译

安装 Visual Studio 2022 C++ 工作负载、CMake、匹配 MSVC x64 的 Qt 6.4 或更新版本，单独安装官方 ZeroTier One。Developer PowerShell：

```powershell
.\scripts\build-windows.ps1 -QtPrefix 'C:\Qt\6.8.3\msvc2022_64'
# 可选生成安装器，指定本机实际的 Inno Setup 编译器：
.\scripts\build-windows.ps1 -QtPrefix 'C:\Qt\6.8.3\msvc2022_64' -InnoCompiler 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
```

Qt 路径只是示例，应使用实际安装的 SDK。手动编译：

```powershell
cmake -S . -B build/windows -G 'Visual Studio 17 2022' -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64
cmake --build build/windows --config Release
ctest --test-dir build/windows -C Release --output-on-failure
cmake --install build/windows --config Release --prefix "$PWD/build/stage"
.\build\stage\GameLink.exe
```

GUI 以普通用户运行；需要可读取的受保护本地 Token 文件，见 SECURITY.md。没有可用 Token 时显示错误，不自动提权。server token 仅保存在进程内存，重启需重新填写。

## 真实双端试用

1. 在 VPS 手动运行只读检测脚本并审核角色/许可/路由/端口。预配置专用私有网络池，不使用既有生产网络。
2. 按 DEPLOYMENT.md 配置服务器，管理员将每个个人 token 绑定到真实客户端 Node ID。
3. GUI Settings 填入 HTTPS server、GameLink token、本地 ZeroTier API 端口与 Token 文件。
4. 选择游戏，创建房间，可信对端输入 `房间id:邀请密钥` 加入；双方点击加入 ZeroTier 网络。
5. 等到网络 OK 和虚拟 IP 分配，双方在 Diagnostics 绑定自己的虚拟 IP 启用 UDP echo，选择目标成员后探测。
6. Room 查看目标 Node ID 的 DIRECT / RELAY / UNKNOWN 及证据；若没有流量先做 echo 触发发现。有限时间检测失败显示 FAILED，服务恢复后继续重试。
7. Preferred Direct 允许 ZeroTier 自身 fallback，并明确显示；echo 成功不意味着游戏兼容。启动后在游戏内手动使用 LAN/虚拟 IP，并按 TEST_PLAN 抓包验收。
8. 显式离开房间，房主离开会关闭全房间；关闭 GUI 不会自动退出已有 ZeroTier 网络，孤立房间依靠 TTL。Controller 撤销存在传播延迟。

当前 UDP 探测只测选定目标，三人以上需逐个测量，启动检查不代表已完成全互联验证。当前虚拟 LAN 是网络级连通，**没有按游戏进程分流或隔离**；其他程序如果主动访问该虚拟网段也可通信。GUI 不管理全隧道、不拦截 Steam、不修改游戏文件。

## 文档

- [架构](ARCHITECTURE.md)、[API](docs/API.md)、[Profile](docs/PROFILE_FORMAT.md)
- [ZeroTier 安装/服务检测与错误处理](docs/ZEROTIER_DETECTION.md)
- [研究依据与许可](docs/RESEARCH.md)、[本机评估](docs/ENVIRONMENT_REPORT.md)
- [安全边界](SECURITY.md)、[部署与安装](DEPLOYMENT.md)
- [严格直连限制](docs/STRICT_DIRECT.md)、[游戏验证步骤](docs/GAME_PROFILES.md)
- [测试计划](TEST_PLAN.md)、[本次测试报告](docs/TEST_REPORT.md)
