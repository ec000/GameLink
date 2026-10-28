# 测试记录（按版本追加）

下方首次交付与 0.2/0.3/0.4 的限制为历史记录；0.5 一键安装器结果见文末。

首次交付日期：2026-10-15（Asia/Shanghai）。首次交付主机 macOS arm64；Apple Clang 21.0.0、CMake 4.4.3、Go 1.26.3。最初无本地 ZeroTier、Qt 6、Windows SDK 或远端访问资料；后续已补齐 Qt 6 本机和 Windows CI 验证，见文末。

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

## 0.5 一键安装官方 ZeroTier（2026-10-18）

[最终 Windows/Linux CI](https://github.com/ec000/GameLink/actions/runs/39102452054) 全部通过，构建提交 `c0c9a15c4efdc5e34fbc3afe26116dd4957242d8`。本机 macOS arm64 / Qt 6.11.2 完整构建、CTest 3/3、Python 2/2 与更新后的启动自检通过；Windows 专属 MSI 测试在本机跳过，由 Windows CI 覆盖。

Windows Server 2022 / MSVC 2022 / Qt 6.8.3：CTest 3/3（Qt 客户端共 19 项）、固定官方 MSI 的签名/发布者/产品识别、Qt/CRT 部署及清除 SDK 环境后的启动/TLS 检查通过。Inno Setup 6.7.1 成功编译包含简体中文向导的一键 EXE。

以下在全新一次性 Windows runner 上使用真实安装器完成，不属于 mock：

| 检查 | 结果 |
|---|---|
| 取消 ZeroTier 任务 | 仅安装/卸载 GameLink，未创建 ZeroTier 服务 |
| 默认任务 | 从官方 HTTPS 下载 1.16.2，固定 SHA-256 校验后实际运行 MSI，安装成功 |
| 初始运行 | 服务 Running，生成身份，本地 API 返回正确版本和节点格式 |
| 安装后客户端 | GameLink 自检通过，识别并验证本次下载缓存的 MSI |
| 重复安装 | 日志为 ZT_PREREQ_SKIP；identity.secret 的 SHA-256 不变，原先停止的服务保持 Stopped |
| 卸载 GameLink | 等待 Inno 记录成功；GameLink.exe 消失，独立 ZeroTier 仍 Running，原身份不变 |
| 显式卸载 ZeroTier | 官方 MSI /x 成功，服务消失 |
| 默认分发内容 | stage 不含 ZeroTier MSI；EXE 按需从官方直接下载，ZIP 不附带该 MSI |

CI 中 sc stop 返回过 109（ERROR_BROKEN_PIPE）；测试仅在随后真实查询 SCM 为 Stopped 时接受，不把退出码直接视为成功。Inno 原始卸载器的自删除可能晚于启动进程退出，测试等待卸载成功日志，且不重复执行残留卸载器。Qt action 使用同一固定提交的 JS 入口，避免部分 runner 后置阶段不支持其复合入口的 $/action 语法。

官方样本：ZeroTier One 1.16.2，SHA-256 `42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670`。只读提取 MSI 与 Windows -q -h 帮助均确认其实际二进制声明 Source-Available；不能按默认源码构建说明推断为 MPL-only。默认交付不内置/再分发此 MSI，许可和源码通知已纠正。公开再分发的离线嵌入模式需相应权限；该公开交付没有离线版。随后按用户确认的个人用途生成的私用离线包见下节。

产物：`GameLink-0.5.0-Setup.exe`（一键在线安装）与 `GameLink-0.5.0-Windows.zip`，分别提供于 GameLink-Windows-Installer / GameLink-Windows-Qt6 artifact；日志位于 GameLink-Windows-Installer-Logs。源码归档及 SHA-256 校验清单位于本地 build 目录。

边界：CI 为管理员/自动化环境，未手动验收 Windows 10/11 标准用户 UAC 同意/取消、跨账号提权、旧版升级/更高版本保留、重启与驱动策略矩阵。服务/API 就绪不等于虚拟网卡加载、真实 join、P2P、NAT 或游戏验收；这些仍按既有测试计划执行。没有在用户本机安装服务，没有导入或修改 planet/moon。GameLink 程序与安装器尚未代码签名。

0.5.0 安装器 SHA-256：`1f12d10b10083e16f3f38c234de0be184e9723aa38cfce2f00e485a658f1d0d7`（41,480,637 字节）。ZIP SHA-256：`3aad8c0be9f2699dde5082df031a651ab2b07719423648c80f1bb9d5c7361c13`（49,610,513 字节）。本地已校验 ZIP 全量 CRC、Qt/CRT/许可依赖及无 MSI 条目，清单标记 download-on-install。

## 个人非商业自用离线包补充（2026-10-18）

用户明确选择内置原版官方 MSI，并确认仅个人非商业自用、不公开分发。本次提供独立的私用打包入口，不把个人用途声明伪装成商业/OEM 再分发授权。原始 MSI 仍受其原始条款约束，默认公开安装包继续采用官方下载模式。

[个人离线构建](https://github.com/ec000/GameLink/actions/runs/39103736272) 与 [默认 Windows/Linux 回归](https://github.com/ec000/GameLink/actions/runs/39103729234) 全部通过，打包源码提交 `1763d718157ec72bf615d61ec6654aac3e2b79bf`。个人安装器复用固定 SHA-256 的已验证 0.5.0 Qt 6.8.3 Windows ZIP，基线应用提交 c0c9a15；没有重编译或改动客户端/ZeroTier 二进制。

私用 Windows runner 上真实验证：取消 ZeroTier 任务仅安装 GameLink；默认任务使用内嵌 MSI（必须出现 ZT_PREREQ_BUNDLED 且没有 ZT_PREREQ_OFFICIAL_DOWNLOAD）；内置 MSI 与经签名/产品/哈希核对的原包一致；服务 Running、本地 API 版本/节点格式与 GUI 自检正常；重复安装跳过且保留原身份与 Stopped 状态；卸载 GameLink 保留独立服务和身份；最后单独卸载官方 MSI 后服务消失。未执行真实网络 join 或游戏联机，不把安装成功当作 P2P 验证。

原始离线 EXE/MSI/ZIP/stage 未上传为公开 artifact。传输使用一次性 256 位密钥、AES-GCM 与认证头；上传前完成 PowerShell 加密 → Node 认证解密 → 文件 SHA-256 一致检查。本机也完成认证解密，并确认哈希/大小与 Windows 测试文件一致。本地篡改测试证明认证失败不会写出明文。成功交付后已删除 GitHub 临时 Secret、临时密文 artifact 和本机密钥/密文/测试文件，复查远端 Secret 与 artifact 数量均为零。

本地私用文件：build/artifacts/windows-0.5.0-personal/GameLink-0.5.0-Offline-Personal-Setup.exe，49,521,076 字节，SHA-256 `5386787ffca96b40fd10096abd1529f9897b81bc504410338bbaa24cadf8bac6`；目录权限 0700、文件权限 0600。原始 ZeroTier MSI SHA-256 仍为 `42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670`。未创建 GitHub Release，未公开上传个人离线二进制。源码/流程说明见 PERSONAL_OFFLINE_PACKAGE.md。

边界与默认版相同：安装 MSI 无需重新下载，但真实联机仍需网络；Windows UAC、驱动/重启和标准用户跨账号矩阵未做交互式实机验收。GameLink 安装器未签名，不替换 planet/moon，不复制 Token。私用文件不能当作已经获准公开再分发的发行版。

## 自建根节点个人定制包补充（2026-10-18）

[定制离线包 Windows 验证](https://github.com/ec000/GameLink/actions/runs/39114565444) 与 [默认 Windows/Linux 回归](https://github.com/ec000/GameLink/actions/runs/39114563937) 全部通过。安装器/脚本源提交 bbbf4de；Qt 应用仍使用 personal-offline-base.json 固定的已验证 0.5.0 基线。

用户提供的两个文件按公开 World/Identity/InetAddress 格式检查通过，没有序列化身份私钥；Planet 与 Moon 指向同一个根节点。对截断、尾随数据、错误类型和含私钥的身份构造进行本机拒绝检查。格式校验不宣称验证了签名密钥归属或网络可达性。

真实一次性 Windows runner 验证：默认任务安装原版 MSI，并把内置文件配置到标准机器数据目录，文件哈希与输入一致；API `/moon` 确认已加载该 Moon；旧 Planet 有管理员/SYSTEM 备份。相同配置重复执行不增加备份；输入损坏被拒绝；人为用文件阻塞 moons.d 后，写 Planet 成功、写 Moon 失败，旧 Planet 按字节恢复，原先停止的服务仍停止。恢复目录后再次配置成功，身份哈希未变。后续安装器重复执行保留已停止的服务；卸载 GameLink 保留 ZeroTier 与身份；单独卸载官方 MSI 后服务移除。Qt GUI 自检通过。

修复了实际发现的两个环境问题：UAC 子进程继承的模块路径不一定适合 Windows PowerShell 5.1，现从受保护的系统目录显式加载对应模块；ZeroTier 停止时可能先关闭 SCM 管道，只有 SCM 在 30 秒内确认 Stopped 才允许继续写根配置。

边界：API 采样的 `online` 为 false；这次只证明本地安装和 Moon 加载，不证明自建服务器离线，也不证明它已可达。未提供 Network ID/GameLink 服务端登录配置，因此未 join、未授权真实成员、未验证虚拟 IP/P2P/NAT/游戏。Windows 10/11 标准用户、跨账号 UAC、策略/重启等仍需实机验收。安装器尚未签名。

交付文件：build/artifacts/windows-0.5.0-custom-roots/GameLink-0.5.0-CustomRoots-Offline-Personal-Setup.exe，49,530,398 字节，SHA-256 `1c24e0199b458a1cc548efa7bdbfb19e2a2dbb5867a17a9878cffe7917bc1332`。原版 ZeroTier MSI 与 Qt 运行库一同内嵌，新增根节点文件，无须手动复制。安装包认证解密后的本机哈希/大小与 Windows 测试文件一致。最终目录 0700、文件 0600；根节点输入经临时 Secret 传送，未提交二进制到公有仓库，EXE 仅加密上传并已删除远端密文及两项临时 Secret，本机临时密钥、输入捆绑、密文均已清理。原始用户文件保留在用户提供的目录。未创建公开 Release。


## ZeroTier 独立状态页 0.6.0（2026-10-19）

新增六页导航中的 ZeroTier 状态页：显式加入、选中网络退网、手动刷新、所有本机网络列表、节点/目标网络分开显示。安装检测、API 凭证、服务安装/卸载从设置和概览集中到此页，概览保留快捷入口。房间 ID 与 Network ID 分别显示。

本机 macOS / Qt 6.11.2 编译通过，CTest 3/3；Qt 19 项通过、1 项仅 Windows 的原版 MSI 检查跳过。[Windows/Linux 构建](https://github.com/ec000/GameLink/actions/runs/39251336067) 全部通过，Windows 原生 Qt 6.8.3 编译、CTest 3/3、六页大小窗口布局/截图、Qt/CRT 独立部署和真实在线安装器生命周期检查通过。应用源提交 732fd835d467b02a749ec69d3b53938bbeb24f39。

新增回环 API 集成测试覆盖：POST 新网络的路由/DNS 安全参数、等待授权 → 等待配置 → 等待 IP → 已连接；延迟响应时禁止重复点击及 MSI 操作；两个网络同时存在时只 DELETE 表格选中的网络；取消确认不发送 DELETE；HTTP 失败不移除成员行，成功后读取确认其他网络保留；认证失败时旧样本标为过期且禁用网络操作；凭据和上游错误内容不进入日志。原房间创建、入网、DIRECT/RELAY/UNKNOWN、离开房间场景回归通过。

[个人定制离线包](https://github.com/ec000/GameLink/actions/runs/39251944698) 使用 0eeaa2e 固定的 0.6.0 基线 ZIP；ZIP SHA-256 为 772fe4bc4dc72cf4b12937c3a448be23f463eacd99def7244cad148769f8ee03，本机全量 CRC 与不含 MSI 检查通过。个人包继续内置原版 ZeroTier 1.16.2 MSI、用户提供的 planet/moon，完成真实安装、根节点哈希、API Moon 加载、损坏拒绝、部分写入恢复、重复安装保留停止状态/身份、GUI 自检及独立卸载验证。

最终文件：build/artifacts/windows-0.6.0-custom-roots/GameLink-0.6.0-CustomRoots-Offline-Personal-Setup.exe，49,536,526 字节，SHA-256 09a61fcc4f26a262d435fcf6a1c0aed470ad360dcd77a1476316f89d86a4e92b。AES-GCM 认证解密、本机与 Windows 原始文件大小/哈希一致，目录 0700、文件 0600。两项临时 Secret 和密文 artifact 已删除并复查数量为零，本机临时密钥、输入捆绑和下载密文也已清理；未公开上传个人 EXE、原始 MSI 或根节点二进制。

边界：新加入/退网测试使用回环模拟服务，没有加入或离开用户真实网络。实际安装测试确认本地服务与 Moon 加载，但在线采样为 false，未验证自建根节点可达性、真实授权/IP、P2P、NAT 和游戏联机。API Token 仍需按用户账户权限配置；不能把网络 join 的 HTTP 接受、节点在线或测试截图视为游戏连通证明。标准用户/跨账号 UAC、策略/重启等矩阵仍需实机验收。个人安装器尚未签名。


## 客户端版本显示 0.6.1（2026-10-19）

窗口标题、侧栏和“设置 → 关于 GameLink”明确显示 GameLink 客户端 v0.6.1，统一来自 CMake 项目版本设置的 applicationVersion；ZeroTier 独立服务版本继续由状态 API 显示。现有启动自检增加标题/侧栏/关于页的版本一致性检查。本机 Qt 6.11.2 编译、启动自检与六页大小窗口布局检查通过，命令行 --version 输出 GameLink 0.6.1。

[Windows/Linux 构建](https://github.com/ec000/GameLink/actions/runs/39261823373) 全部通过，应用提交 edc2e6550a3a272714e1a6f867524af97f45a6b1；Windows 原生 Qt 6.8.3 编译、CTest 3/3、独立部署启动、真实安装器生命周期检查和截图检查通过。[个人离线打包](https://github.com/ec000/GameLink/actions/runs/39262489588) 使用 70be1c2 固定的已验证 0.6.1 ZIP，完成内置 MSI 安装、Moon 加载、根节点备份恢复、重复安装与卸载保留、安装后的版本显示自检。

个人包继续包含原版 ZeroTier 1.16.2 MSI 和原有 planet/moon。文件 build/artifacts/windows-0.6.1-custom-roots/GameLink-0.6.1-CustomRoots-Offline-Personal-Setup.exe，49,540,172 字节，SHA-256 ee29fba894bb04233cc9525b725d6f6f5ab88da4a9afd26385c44a06f2535896；本机认证解密后的大小/哈希与 Windows 原始文件一致。目录 0700、文件 0600。临时远端 Secret 与密文 artifact 已删除且复查为零，本机临时输入、密钥和密文已清理；未公开上传个人 EXE/MSI/根节点二进制。基线 ZIP CRC 校验通过，无 MSI，SHA-256 3684c38a11e0dcd1a68f7ecc33502c1bb390e15943665f4793a802e8c3f16eb6。

本次仅调整客户端版本显示，真实网络、授权和游戏验收边界沿用 0.6.0；没有操作用户电脑上的 ZeroTier 网络。


## 本机凭据与状态刷新修复 0.6.2（2026-10-19）

针对服务 Running、控制器显示节点在线，但 GameLink 报 TOKEN_MISSING 且网络列表为空的情况，修复只配置旧 GameLink 占位文件、没有发现官方用户副本的问题。兼容 Windows 新版和旧版 authtoken.secret 路径，当前配置优先；缺失默认占位路径自动迁移，其他自定义路径保留。显式检测通过回环 GET /status 验证，401/403 的旧副本会跳过，成功后只保存路径；新增文件选择和页面顶部解释，不复制凭据或自动提权。

本机 macOS / Qt 6.11.2 编译与 CTest 3/3 通过，Qt 21 项通过、1 项 Windows 专属检查跳过。新增回环集成测试覆盖新目录凭据失效而旧目录有效、恢复读取已有网络名称/状态/IP、自定义缺失路径不被启动覆盖、显式检测失败不保存、成功后恢复、节点 online=false 不误报在线，以及日志/设置不含凭据内容。检测只执行 GET，没有操作用户真实网络。

[Windows/Linux CI 39266729721](https://github.com/ec000/GameLink/actions/runs/39266729721) 全部通过，应用提交 358e12014d9e76e2050ff0eb4880857a03d91333。Windows MSVC 2022 / Qt 6.8.3 编译、CTest 3/3、六页导航/大小窗口布局、独立部署与实际在线安装器生命周期验证通过；人工检查 Windows 状态页预览。已下载 ZIP 全量 CRC 校验，未包含 MSI，SHA-256 为 61871335bbdff779e0cd8a412964fc8804223c585ac47c08833e7a6e04fefe96。

[个人离线包 CI 39267399164](https://github.com/ec000/GameLink/actions/runs/39267399164) 使用 110d925 固定的 0.6.2 基线，继续内置原版 ZeroTier 1.16.2 MSI 和用户原有 Moon/Planet。通过真实 MSI 安装、Moon API 加载、哈希/备份恢复、损坏/部分写入拒绝、重复安装/停止状态保留和独立卸载检查。本次节点 online=false 仅为 CI 样本；没有 join 用户真实网络，真实电脑授权/IP/P2P/游戏仍需实机验收。

最终文件 build/artifacts/windows-0.6.2-custom-roots/GameLink-0.6.2-CustomRoots-Offline-Personal-Setup.exe，49,540,361 字节，SHA-256 c391edd77446d20b1303dedc76419dd46d46e254712b47a8513739dca20a1793。本机 AES-GCM 认证解密后与 Windows 原始大小/哈希一致，目录 0700、文件 0600。两项临时 GitHub Secret、远端密文 artifact 均删除并复查为空；本机临时密钥、根输入捆绑和密文也已清理。未公开上传个人 EXE、MSI 或自建根二进制。


## 应用图标 0.6.3（2026-10-19）

新增原创、无字体依赖的蓝色圆角白色 G 矢量图稿，生成透明 PNG 和九尺寸 ICO（16/20/24/32/40/48/64/128/256）。Qt 内嵌 PNG，用于窗口/任务栏与侧栏；Windows RC 内嵌 ICO，供 EXE、开始菜单快捷方式和卸载列表使用；Inno Setup 使用同一 ICO。正常构建只消费已提交的图标文件，不需要 Sharp；可选脚本用于从 SVG 重新生成。

本机 Qt 6.11.2 编译、CTest 3/3（Qt 21 项通过、1 项 Windows 专属检查跳过）和启动图标资源自检通过。ICO 九个帧均可独立解码，尺寸与透明通道正确；人工检查图标与侧栏渲染。[Windows/Linux CI 39271596907](https://github.com/ec000/GameLink/actions/runs/39271596907) 全部通过，应用提交 cab3cdf19ee9641e6c4716666269c5c7fb5bdd54；Windows Qt 6.8.3 / MSVC 编译、3/3 CTest、Qt/CRT 部署、原生预览和实际安装器生命周期检查通过。解析最终 GameLink.exe 的 PE 资源，九个图标帧均与 ICO 源数据逐帧哈希一致。ZIP 全量 CRC 校验通过、无 MSI，SHA-256 23033d8c076b6eb518ab7e91b973338646201563c88b8c91b38952efd86af2aa。

[个人离线包 CI 39272358281](https://github.com/ec000/GameLink/actions/runs/39272358281) 使用 f5bdeb7 固定的 0.6.3 基线，继续内置原版 ZeroTier 1.16.2 MSI 和原有 Moon/Planet；真实安装、根文件备份恢复、Moon 加载、重复安装/独立卸载检查通过。最终安装器 PE 资源也确认包含全部九个原始图标帧。图标变更不调整网络或凭据配置；未进行用户 Windows 桌面图标缓存、真实网络和游戏验收。

交付 build/artifacts/windows-0.6.3-custom-roots/GameLink-0.6.3-CustomRoots-Offline-Personal-Setup.exe，49,673,584 字节，SHA-256 6bac02533407a8da6a54d674ba7b1803bccfe3025c983fbe3523752f65f04a33；本机认证解密后与 Windows 原始文件大小/哈希一致。目录 0700、文件 0600。两项临时 GitHub Secret 与远端密文已删除并复查为空，本机临时密钥、输入捆绑和密文已清理，个人 EXE/MSI/根文件二进制未公开上传。


## 房间操作反馈修复 0.6.4（2026-10-28）

用户报告点击创建房间无反应，并确认尚未部署房间服务或未填写个人访问凭证。原客户端将本地拒绝与请求错误仅写入日志；新版本在房间页顶部直接显示缺少凭证、非法地址/凭证、连接失败、401/403、异常响应及操作成功，当前页自动滚动到提示。新增设置与游戏选择入口、所选游戏摘要；请求期间显示进度、禁止重复提交并锁定请求使用的房间服务字段。创建房间仍需要单独部署 GameLink Server，不要求预先指定或启动游戏 EXE。

本机 macOS / Qt 6.11.2 编译、CTest 3/3 通过（Qt 22 项通过、1 项 Windows 专属检查跳过）。新增回环集成验证缺少凭证时不发请求、设置入口可达、非法地址/短凭证拒绝、延迟响应时禁止重复点击与修改配置、401 错误可见且恢复按钮、缺少房间/网络 ID 时拒绝成功、有效响应显示成功，以及凭证/邀请/上游内容不写日志。现有创建、入网、DIRECT/RELAY/UNKNOWN 和离开回归通过。缺凭证界面预览已保存并检查。

[Windows/Linux CI 40608023583](https://github.com/ec000/GameLink/actions/runs/40608023583) 全部通过，应用提交 92c775fab1d7d52c1ea4bf64fdf4431117211798。Windows MSVC / Qt 6.8.3 编译、3/3 CTest、六页大小窗口布局、独立部署、实际安装器生命周期及原生界面预览检查通过。已下载基线 ZIP 全量 CRC 校验、确认无 MSI，SHA-256 992d4cd1bc9cd8bc718aa23e1b5f3f5c83c345d898d1ab55dfbb7f2b756f72ca。

[个人离线包 CI 40608591410](https://github.com/ec000/GameLink/actions/runs/40608591410) 使用 2aad7cc 固定基线，保留官方 ZeroTier 1.16.2 MSI、原有 Moon/Planet 和应用图标，通过实际安装、自建根加载/备份恢复、重复安装与独立卸载检查。交付 build/artifacts/windows-0.6.4-custom-roots/GameLink-0.6.4-CustomRoots-Offline-Personal-Setup.exe，49,675,146 字节，SHA-256 ae69f9cb1ea884991b4fa2466f64c7034cb3fcc08df1fc2f27fb3f2bddd161b2。本机认证解密后与 Windows 原始大小/哈希一致，目录 0700、文件 0600。两项临时 GitHub Secret 和远端密文已删除并复查为空，本机临时密钥、输入捆绑和密文也已清理。

本次没有部署用户 VPS 上的 GameLink Server，没有创建用户真实房间或验证真实游戏联机。部署目标尚待用户选择，后续仍需实际服务器、HTTPS 和用户/网络池配置。客户端操作反馈修复不能替代房间服务部署。


## 本机房间模拟测试包（2026-10-28）

用户选择先在本机测试。Mac 实际构建并运行 Go 服务，独立生成 build/local-room-test-20261028 下的测试配置与凭证（目录 0700，配置/凭证 0600），在 127.0.0.1:8080 验证认证 health、创建房间、Bob 邀请加入、两名成员列表及离开，验证后房间已清理；不连接真实 Controller、不改动 ZeroTier。服务保留运行供当前 Mac 手动测试，PID 与校验进程身份的停止脚本同在该私有目录。另有临时真实进程 smoke 验证互斥写锁、退出后重启；Mac Go race 全量通过。

原服务端 Windows 入口会因不支持状态锁而拒绝启动。新增 Windows 专用独占文件句柄锁与明确的 mock-only CLI 限制，实际 Controller 模式在读取配置前拒绝；Windows mock journal 保留文件同步和重命名，跳过该平台不可用的目录 fsync，不宣称生产断电持久性。Linux/macOS 原有 fsync 路径和真实服务模式不变，Windows 不作为生产服务平台。

[本机测试包 CI 40614226391](https://github.com/ec000/GameLink/actions/runs/40614226391) 全部通过，源提交 dd5dca83644af56959f8e6f1222bc434346bf131。Linux 全量 Go race/vet 通过。Windows 原生验证 CLI 真实模式拒绝、同配置第二进程拒绝、退出后锁恢复、实际服务创建/ACL/邀请加入/成员/离开；Windows PowerShell 5.1 验证启动配置、两账号独立随机凭证、用户/SYSTEM 保护 ACL、重复启动复用、账号凭证复制及使用脚本生成凭证向实际服务认证、创建和离开。未使用真实 ZeroTier 网络或验证游戏。

首轮验证包 build/artifacts/local-room-test-20261028/GameLink-Local-Room-Test-Windows.zip，4,921,672 字节，SHA-256 52583a87a0202fe10db3e1e21b89b3fa0e490b806931d76552d275d4eb8452af。完整 CRC 通过；仅含测试服务 EXE、PS1/CMD 与 README，无预置凭证、配置 JSON、MSI 或个人根文件。凭证在目标 Windows 首次运行时生成，测试包配合已安装的 0.6.4 GUI 使用，未重新打包客户端。此包只验证房间管理 API，不能通过模拟 Node/Network ID 加入真实虚拟网络。

最终补充 [CI 40614834650](https://github.com/ec000/GameLink/actions/runs/40614834650)，源提交 e8850c59eb2274f8fe269790c75e1c336461b00c，全部成功：真实服务持有状态锁期间仍可复制 Bob 凭证，重复启动在端口检查处拒绝，不影响原服务。最终交付改为 build/artifacts/local-room-test-20261028-final/GameLink-Local-Room-Test-Windows.zip，4,921,670 字节，SHA-256 d22ad50cbaaaeb7dc9f7f14cad11b2bccb7826ae0100c8671cc91185aa0ec1c4；CRC/六个静态文件/无配置与凭证检查通过。
