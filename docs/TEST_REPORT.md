# 本次测试报告

日期：2026-10-15（Asia/Shanghai）。首次交付主机 macOS arm64；Apple Clang 21.0.0、CMake 4.4.3、Go 1.26.3。最初无本地 ZeroTier、Qt 6、Windows SDK 或远端访问资料；后续已补齐 Qt 6 本机和 Windows CI 验证，见文末。

## 已验证

- CMake configure/build（GAMELINK_BUILD_GUI=OFF）：成功；C++20 core 静态库与测试可执行文件已产生。
- CTest：1/1 passed。包含 offline、join pending、bounded NAT traversal、DIRECT、TCP fallback RELAY、无 preferred RELAY 推断、UNKNOWN schema/role/过期/clock、网络切换、strict unsupported 与 RTT/jitter/loss。
- Go `test -race -cover ./...`：通过；API、Controller fixture、文件锁测试。最终覆盖率见下面最终复验记录。
- Go `vet ./...`：通过。
- 本机 Go server 编译成功，真实进程 + loopback HTTP mock 冒烟通过：health、strict 422、创建、访问控制、邀请加入、成员读取、离开。
- Linux amd64 / arm64 服务交叉编译成功，未在 Linux 上运行。
- Python 2 项测试通过：调查白名单隐藏秘密字段，游戏 profile 维持 unverified/无预填已验证端口。
- 本地只读调查已执行，结果在 ENVIRONMENT_REPORT.md。

初次 Go 测试受 sandbox 的默认缓存目录和本机监听限制；缓存改到 /tmp，获准在本机临时端口运行后完整测试通过。没有跳过 HTTP 测试来报告成功。

## 未验证/未完成

- `cmake -S . -B build-gui -DGAMELINK_BUILD_GUI=ON` 在 find_package(Qt6) 失败：Qt6Config.cmake 不存在。这是最初缺少 SDK 的结果；后续 Qt 6 验证已完成，见下方记录。
- 没有用户 Windows 桌面实机的手动验收、Inno Setup 安装器运行、ZeroTier join 实机、真实成员证书时序/撤销、UDP echo 实机或游戏路径验证。Windows CI 的构建与测试记录另见文末。
- 六种 NAT 网络矩阵全部未测，不能保证中国大陆 CGNAT 直连成功率。
- WFP/受限 broker/严格直连逐包限制未实现；严格模式能力恒 false，不是已实现后“测试失败”。
- 自动游戏扫描、网卡优先级修改、LAN 广播/组播验证、游戏实测端口、Steam 自动会话识别、自启动未实现。
- VPS 只读评估未执行；角色/许可/服务端口均未知，未修改远端配置。

源代码与 mock 测试可运行不等于完整 Windows 产品或真 P2P 验收完成。

## 最终复验记录

最终 15 项 Go 测试通过，race detector 无报告；包覆盖率 API 79.7%、Controller 78.9%、main/文件锁 8.8%。main 启动与退出另由真实进程冒烟覆盖，该流程不计入 go test 的行覆盖率。
最终服务重新编译后，真实进程 HTTP 冒烟再次通过；go vet 无报告。Python 语法检查与 shell `sh -n` 通过。这是首次交付状态；后续已通过 Windows CI 执行 PowerShell、Qt 和 Windows 资源编译，Inno Setup 安装器仍未验证。

交付归档：build/GameLink-0.1.0-source.zip（源码、profiles、文档与测试，不含凭证或构建缓存）；build/SHA256SUMS 包含源码归档与三个服务端二进制的 SHA-256。

## Qt 6 后续验证

继续开发时安装 Homebrew qtbase 6.11.2。首次实际编译发现禁用 ASCII 隐式转换下的 QChar(char) 不合法，以及 QHash 使用不完整 QJsonObject 类型；已改为 QLatin1Char 并补充头文件。

macOS arm64 / Qt 6.11.2 全 GUI 已编译。新增 Qt Test 本机 HTTP fixtures，覆盖本地 token 与 GameLink token 隔离、新网络 join 的安全配置、非 HTTPS/非 loopback origin 拒绝、重定向不转发凭证、持续慢响应的总时限、异常/过大响应脱敏、GUI 创建房间/严格模式拒绝/join/DIRECT→RELAY→UNKNOWN/leave，以及 token 不写设置和日志。

新增 2 秒总请求 deadline，弥补 Qt transfer timeout 只限制无数据期间的空闲时间。增加 GUI 启动自检，检查 profiles、页面与 TLS 可用性。所有本机 Qt 测试通过；Windows 另由 `.github/workflows/build.yml` 编译和打包验证；最终成功 run 为 38761176168，记录如下。

fixture 截图位于 build/qt6/qt-room-fixture.png，标题明确标为 MOCK，不代表真实 P2P 连接。真实 ZeroTier、游戏、NAT 场景及 Windows 交互式桌面仍未验收。

Windows 首次 run 38759602128 的 MSVC 编译和 3/3 CTest 均通过；安装阶段暴露 relative --prefix 使 Qt deploy_qt_conf 拒绝路径的问题。已将 PowerShell stage 改为绝对路径，并将 CMAKE_INSTALL_BINDIR 设为与 flat ZIP/安装器一致的目录，让 Qt DLL 和 qt.conf 位于 GameLink.exe 旁；修正后重新运行 CI。

第二次 run 38760257190 仍在打包后自检失败：Qt 6.8 的部署支持在加载时捕获了 bin 目录；已在 install 脚本显式设 QT_DEPLOY_BIN_DIR="."，并提前设置安装目录。另根据 windeployqt 的 VCINSTALLDIR 提示，通过 vswhere 查找 MSVC 并复制 x64 CRT DLL，打包前逐项验证 Qt、平台插件、qt.conf 和 CRT 文件。

### 最终 Qt 6 验证结果

- 本机 macOS arm64 / Qt 6.11.2：GUI 编译成功，CTest 3/3 passed（core、qt_client、gui_startup）。
- [Windows/Linux CI run 38761176168](https://github.com/ec000/GameLink/actions/runs/38761176168)：两个 job 全部成功，源码提交 dfc0fdcf83f727bb08bda7234995adfee720b353。
- Windows Server 2022 runner / MSVC 2022 / Qt 6.8.3：编译、3/3 CTest、Qt DLL/插件部署、app-local CRT 检查均通过；清除 Qt SDK PATH/插件环境后，stage/GameLink.exe --self-test 成功，验证默认 Windows 平台插件、三个 profile 与 TLS 后端。
- Linux Ubuntu 24.04：核心测试、Go race/coverage/vet、服务编译、Python 检查和真实进程 mock HTTP 冒烟均通过。
- Windows ZIP 测试包由成功 run 的 GameLink-Windows-Qt6 artifact 提供，保留 14 天；没有创建正式 Release 或已签名安装器。

这些检查不需要/不修改真实 ZeroTier；实际联机、六组 NAT、Warcraft III/DST/Overcooked 兼容性和严格直连仍未验收。用户 Windows 10/11 桌面与 Inno Setup 安装/卸载也需单独验收。

## 0.2 独立安装/Windows 服务/API 诊断（2026-10-16）

新增 client/platform/zerotier_environment：SCM 查询 ZeroTierOneService，服务注册路径/常见目录检测程序，独立只读端口文件。Home 分开显示安装、服务、API、节点状态，并提供明确错误码与人工核对提示。凭证错误不覆盖服务状态；API 失败清除过期路径证据。

本机 Qt 6.11.2 初轮 CTest 3/3 通过，新增 5 个功能测试方法覆盖证据/错误/UI 分离；Windows 原生查询与 0.2.0 ZIP 在 CI 复验后记录。自定义安装、真实 ZeroTier 用户环境/权限矩阵仍需实机确认。

Windows 首轮 38909273554 与补充逐项日志的 38910392945 已完成编译与启动自检；原生服务查询（不存在服务、EventLog Running）及安装/UI 分离测试通过。唯一失败为关闭端口测试过度限定 CONNECTION_REFUSED，Windows Qt 的连接重试也可能先触发总请求超时；测试改为接受实际 CONNECTION_REFUSED 或 REQUEST_TIMEOUT，保留 2 秒硬时限与其独立测试，其他错误仍会失败。

### 0.2 最终结果

[CI run 38910834948](https://github.com/ec000/GameLink/actions/runs/38910834948) 全部通过，代码提交 bc6a4889a787e5c6c00c6e47d368ec610496e668。Windows Qt 6.8.3 / MSVC 2022 的 3/3 CTest、原生服务查询和新增 Home/错误诊断回归测试通过；Qt 客户端含 11 个功能测试方法（加 init/cleanup 为 13 项）。Linux job 同样通过。0.2.0 ZIP 已生成，含 SDK 环境隔离后的程序/TLS 启动检查。

本机 macOS Qt 6.11.2 测试同样通过；原生 Windows 服务查询由 Windows CI 执行，macOS 只测平台无关模型和 API/UI fixtures。用户电脑上的实际 ZeroTier 安装、企业 ACL、自定义/便携目录与不同安装版本仍需实机核对；没有自动启停、修复或修改任何 ZeroTier 服务。

## 0.3 桌面 UI 更新（2026-10-17）

Qt 6 Widgets 改为五页侧边导航与卡片布局，UI 构建独立于网络/房间逻辑。新增窗口导航回归：点击五个导航、检查当前页与选中状态，在 1180×840 和 960×680 检查字段布局无横向溢出并保存十张 UI 预览。现有安装/服务/API 分离和房间 DIRECT→RELAY→UNKNOWN/leave 集成测试继续执行。

本机 macOS / Qt 6.11.2 编译通过，初轮 CTest 3/3 通过。Windows Qt 6.8.3 由 CI 构建、测试、打包并上传渲染预览，完成后的结果另行记录。预览使用 fixtures 或未配置状态；未进行用户 Windows 桌面、真实 ZeroTier 或游戏联机验收。

首轮 38934418435 编译、3/3 CTest 和打包成功，但人工检查 Windows 预览发现通用 offscreen 插件缺少字体数据库，英文和中文均显示方框。Windows 测试改用原生 windows 平台插件，增加拉丁/中文字体可用性断言；macOS 继续使用 offscreen。修正后重新运行并检查截图，不将首轮方框截图视为界面验收结果。

### 0.3 最终结果

- [CI run 38935484115](https://github.com/ec000/GameLink/actions/runs/38935484115) 全部成功，代码提交 f4190334252f35b2e777b6551c20a4e6aebe89ca。
- Windows Server 2022 / MSVC 2022 / Qt 6.8.3：3/3 CTest、原生平台的中英文字体与导航/小窗口布局检查、现有 ZeroTier/房间回归和隔离 SDK 环境的打包启动/TLS 检查全部通过。生成 GameLink-0.3.0-Windows.zip。
- macOS / Qt 6.11.2：3/3 CTest 通过，Qt 客户端包含 12 个功能测试方法（加 init/cleanup 共 14 项）。另以 QT_SCALE_FACTOR=1.5 重跑导航/布局检查通过，人工查看五页及小窗口预览。
- Linux job 的核心、Go race/coverage/vet、Python 和真实进程 mock HTTP 冒烟通过。
- 已检查 Windows 原生预览中的中英文、卡片、导航、表单、房间和诊断；中文可正常显示。字体优先级包含 Segoe UI Variable/Segoe UI/微软雅黑 UI/微软雅黑/黑体，CI 服务器仍可能使用其已安装的其他中文回退字体；未随包分发字体。首轮 offscreen 方框截图已单独保留，交付使用最后成功 run 的包和预览。

Windows 测试桌面会按可用屏幕尺寸限制初始窗口，长页面通过纵向滚动访问；没有隐藏底部操作。用户电脑上的 Windows 11 外观、不同 DPI/字体配置与真实游戏联机仍需实机验收。此版本没有更改 ZeroTier 网络控制、安全策略或游戏支持边界。

## 0.4 ZeroTier 服务安装/卸载（2026-10-17）

新增 Windows Installer 产品枚举、官方 MSI 信任/发布者/产品校验、独立 UAC 系统安装器调用及异步任务状态；设置页增加安装/修复、版本选择、卸载、结果提示。测试通过注入平台，不执行真实安装/卸载。

本机 macOS / Qt 6.11.2 编译通过。新增 4 个测试方法，覆盖身份与退出码、无效/并发请求和生命周期、原生平台无效包拒绝/非 Windows 限制、GUI 确认/任务关闭保护/重启提示与刷新；连同导航布局检查共 5 个方法均通过（含 init/cleanup 为 7 项）。核心与 GUI 启动检查通过。服务管理预览保存为 build/qt6/zerotier-installer-fixture.png，明确标记 MOCK。

受限环境下直接执行全量 CTest 时，HTTP fixtures 无法监听本机端口，已有 7 个 HTTP 测试因此未通过；它们不是安装功能测试失败。GUI 模态框测试首轮使用 done() 没有触发标准按钮选择，修正为点击真实按钮后通过。已请求在允许本机测试端口的环境中复验，结果待补充。

外网/DNS 与连接器不可用，读取官方资料的权限自动审批两次超时，不能执行远端资料核对或 Windows CI。尚未编译验证本轮 Windows 原生分支，也未验证真实官方 MSI 签名/发布者的正向路径、UAC、服务/驱动安装卸载和数据保留。没有生成或声称已验证 0.4 Windows 包。

补充：本机无网络权限依赖的 9 个客户端测试方法全部通过（含 init/cleanup 共 11 项），core/gui_startup 的 CTest 2/2 通过。请求允许监听本机端口的完整 CTest 执行时，自动权限审批超时，命令未执行，未将其算作测试通过。

### 0.4 最终复验

网络/自动审批恢复后，完整本机 CTest 3/3 通过，源码和文档已提交并推送。首轮 [CI 39026431440](https://github.com/ec000/GameLink/actions/runs/39026431440) 完成 Windows 原生编译、完整回归与 0.4.0 ZIP。

补充固定官方 MSI 正向检查后的 [CI 39026632529](https://github.com/ec000/GameLink/actions/runs/39026632529) 全部成功，代码提交 1eda6302363c54717bbf3f4039ec9dbef158db2b。Windows Server 2022 / MSVC 2022 / Qt 6.8.3 的 3/3 CTest、原生产品枚举/无效包拒绝、服务管理 mock/确认/关闭保护、原有 HTTP/房间回归、Qt/CRT 部署及隔离 SDK 环境的启动/TLS 检查通过。Windows 客户端含 17 个功能方法，加 init/cleanup 共 19 项。Linux 核心、Go race/coverage/vet、Python 和真实进程 mock 冒烟也通过。

CI 只读检查官方 ZeroTier One 1.16.2 MSI，固定 URL 为 https://download.zerotier.com/RELEASES/1.16.2/dist/ZeroTier%20One.msi，SHA-256 42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670。验证有效签名、发布者、产品名、版本与 GUID；不启动或安装第三方 MSI，不将它加入 GameLink artifact。包来源同时与官方下载页和 upstream Windows .aip 产品定义交叉核对。

本机最终 CTest 3/3 通过，Qt 客户端 18 passed / 1 skipped（仅 Windows 的官方 MSI 只读检查）。Windows 原生服务管理卡片截图已检查；build/artifacts/windows-0.4.0-ui/zerotier-installer-fixture.png 明确标记 MOCK。0.4.0 ZIP 已下载到 build/artifacts/windows-0.4.0/，当前源码归档为 build/GameLink-0.4.0-source.zip。

实际 UAC 同意/取消、服务/虚拟网卡驱动安装升级卸载、原配置保留、不同系统策略/证书缓存与重启场景仍需实机验证；本轮没有在用户电脑或 CI 上安装/卸载真实 ZeroTier，也没有修改 planet/moon。
