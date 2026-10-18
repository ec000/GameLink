# ZeroTier 一键安装、修复与卸载（0.5）

## 一次安装 GameLink 与 ZeroTier

运行 `GameLink-0.5.0-Setup.exe`，保留默认勾选的“安装/升级官方 ZeroTier One 1.16.2”，点击安装。安装器从官方 HTTPS 地址下载固定版本原版 MSI，目标电脑需要联网；无需用户手动下载。

- GameLink 以普通用户身份安装到 `%LocalAppData%/Programs/GameLink`；ZeroTier 服务与虚拟网卡安装由 Windows 单独请求管理员授权。
- 相同/更高的标准 MSI 版本且服务已注册：跳过；已停止的已有服务不被强行启动。
- 较旧的标准 MSI：调用官方 MSI 升级；相同版本缺少服务：尝试修复。较新版本缺少服务时拒绝降级，提示使用原版本修复。
- 非标准服务、未知/无法可靠读取的版本：停止自动安装，可返回取消 ZeroTier 任务，仅装 GameLink。
- UAC/安装取消、安装冲突或其他错误：明确显示原因并阻止继续安装 GameLink。3010/1641 提示手动重启，不主动重启电脑。
- 取消勾选 ZeroTier 可仅安装 GameLink。卸载 GameLink 只移除自身文件和本次下载缓存的 MSI，保留独立 ZeroTier 与其身份/网络数据。

构建脚本从 `deploy/zerotier-package.json` 读取固定版本/官方来源/校验值，检查 SHA-256、Authenticode 及 MSI 产品属性后才构建。默认 EXE/ZIP 不附带 MSI，安装时直接从官方源下载，固定哈希验证成功后才执行；本次下载的 MSI 缓存到应用目录供修复。随包通知和源码链接位于 `licenses/zerotier`，Qt 通知位于 `licenses/qt`。安装器默认简体中文，也可选择英文。GameLink EXE 尚未签名，官方 MSI 保留签名。

安装完成后仍需配置 GameLink 服务器及受保护的本地 API Token；安装器不会获取或复制 API 凭证。默认包不替换 planet/moon；个人定制包可额外勾选自建根节点任务。

官方 1.16.2 Windows 实际二进制声明 Source-Available 许可，并非纯 MPL。默认不随 GameLink 再分发该 MSI；用户使用仍需遵守其条款。取得相应再分发授权后，构建者才可显式使用 `-BundleZeroTier -RedistributionAuthorized` 生成离线嵌入版；该开关是构建者的授权声明，不会赋予许可权利。公开交付为一键在线安装版。针对已确认的个人非商业自用，可通过独立的 package-personal-offline.ps1 与手动私用工作流生成内置原版 MSI 的个人离线包，不声明商业/公开再分发授权；详见 [个人离线包](PERSONAL_OFFLINE_PACKAGE.md)。

## 设置页手动管理

在“设置 → ZeroTier 服务管理”操作。适用于计算机级官方 ZeroTier One MSI 安装，主界面继续以普通用户身份运行。

1. 设置页自动验证并选中本次下载缓存的官方 MSI；也可从官方渠道获取新版完整 `.msi`。
2. 点击“选择 MSI”。验证成功后显示产品、版本和签名发布者。
3. 点击“安装 / 修复服务”，检查确认框中的文件、版本和影响范围；Windows 将单独弹出管理员授权，并运行官方 MSI 向导。
4. 卸载时先刷新安装列表，选择已安装版本，再点击“卸载 ZeroTier”。不需要保留原安装包。
5. 完成或取消向导后，GameLink 更新安装、服务与 API 检测；安装器要求重启时会明确提示。GameLink 不主动重启电脑。

**此处管理整个官方 ZeroTier 产品（服务与虚拟网卡驱动），不是仅删除 SCM 服务注册。** 安装/升级可能中断已有连接，卸载影响本机所有 ZeroTier 网络及使用它的软件。操作前提供具体产品/版本确认，默认不执行。便携版、单独注册服务、非 MSI 或用户级安装不通过这里卸载，请使用它们自身的官方管理方式。

安装包/产品列表不保存到普通设置。GameLink 0.5 默认由安装器从官方下载固定版本原版 MSI；GUI 本身不自动联网下载或自动更新。GUI 不复制本地 Token，也不会在安装完成后自动取得 API 凭证；仍需按现有方式配置受保护的 Token 文件。

## 执行边界

- 安装读取 Windows Authenticode 信任结果，验证证书组织为 ZeroTier, Inc.，并检查 MSI ProductName、Manufacturer、ProductCode 和版本。当前支持产品名 ZeroTier One 或 ZeroTier。
- 验证前锁定本机 MSI 只读文件；拒绝 UNC、网络映射盘、链接、替代数据流、错误扩展名与非法路径。执行前再次验证，安装器运行期间继续持有文件锁。
- 信任检查只使用 Windows 已缓存的证书/吊销信息，避免 GUI 因联网证书查询长时间阻塞；缓存或链不完整时明确拒绝。需要更新系统证书/吊销缓存后重试，不提供跳过签名的开关。
- 卸载仅枚举计算机级 Windows Installer 产品，按名称/发布者筛选，确认与执行前重新核对。仅将对应 GUID 交给系统安装器，不执行注册表 UninstallString，也不运行发现的服务 exe。
- 使用 Windows 系统目录的 msiexec.exe，通过 ShellExecuteEx/runas 请求 UAC；没有 cmd、PowerShell 或 PATH 程序查找。主 GUI manifest 保持 asInvoker。
- 设置页手动管理使用 MSI 完整交互界面；一体化安装器展示附带许可并运行 MSI 基本进度界面（静默模式为 /qn）。禁止自动重启，跟踪进程真实退出结果，不将“进程启动成功”当作安装成功。
- 一个任务运行时禁止重复操作、清除旧链路证据、暂停本地 API 轮询/网络加入/游戏启动。进行中的旧 API 回包也不能恢复旧状态。任务结束重新查询实际状态。
- 运行中不能正常关闭 GameLink，请先在 Windows Installer 中完成或取消；不强杀安装器。不承诺强制结束进程、系统崩溃等情况的事务恢复，实际安装事务由 Windows Installer 管理。

## 结果与错误

| 情况 | 显示/行为 |
|---|---|
| 0 | 安装器已完成，刷新独立检测；不直接声称节点 ONLINE |
| 3010 / 1641 | 完成但需要重启，提醒用户，GameLink 不主动重启 |
| 1223 / 1602 | UAC 或安装向导取消，恢复按钮 |
| 1605 | 对应产品未安装或已移除，要求刷新 |
| 1618 | 另一安装任务正在运行 |
| 1619 / 1620 | 包无法打开/无效 |
| 1625 | 受系统安装策略阻止 |
| 1638 | 已存在其他版本 |
| 1603 / 其他 | 显示具体错误码，检查安装向导和 Windows 事件日志 |
| MSI_SIGNATURE / MSI_PUBLISHER / MSI_PRODUCT | 签名、发布者或产品校验未通过，不执行 |

## planet / moon

已支持个人定制安装器内嵌用户提供的 planet/moon，在显式安装任务中校验、备份后部署并按需重启服务，失败尝试恢复原配置。默认公开包和 GUI 不导入或替换根节点。详见 [自建根节点配置](CUSTOM_ROOTS.md)。

GameLink 不主动删除 identity、网络或根配置。**官方卸载器可能调整其自身数据，不能承诺保留这些文件。** 如需保留，请在卸载前按官方方式备份。配置根节点不等于加入网络；仍需 Network ID 和控制器授权。

## 0.5 验证状态

[Windows/Linux CI](https://github.com/ec000/GameLink/actions/runs/39102452054) 全部通过：Windows 原生编译、CTest 3/3、官方签名/产品识别、Qt/CRT 独立部署、Inno EXE 编译，以及全新一次性 runner 上的真实官方下载/安装、服务/API、自检、重复安装跳过与身份/Stopped 状态保留。卸载 GameLink 后 ZeroTier 仍运行、身份不变；最后单独用官方 MSI 卸载 ZeroTier，服务消失。

默认包不含官方 MSI；安装需要联网，并可在失败时返回取消该任务。手动 UAC、跨账号标准用户、旧版升级/更高版本、驱动策略/重启与 Windows 10/11 桌面矩阵仍需实机验收。GameLink EXE 尚未签名，planet/moon 不变。详见 [测试报告](TEST_REPORT.md)。

## 0.4 基线验证（历史）

本机 macOS / Qt 6.11.2 已编译 GUI，使用注入的 mock 安装平台验证产品/错误识别、无效包拒绝、重复任务阻止、取消与重启结果、对象销毁后回调、卸载确认、任务期间不能关闭以及完成后刷新。非 Windows 显示不支持并禁用安装/卸载。

测试不会安装/卸载真实 ZeroTier。Windows 原生 API 已在 MSVC 2022 / Qt 6.8.3 编译并通过完整回归；[CI 39026632529](https://github.com/ec000/GameLink/actions/runs/39026632529) 额外下载固定 SHA-256 的官方 1.16.2 MSI，通过只读签名/发布者/产品识别，未安装或执行它，亦未把它随包分发。0.4.0 Windows ZIP 已生成，并在清除 Qt SDK 环境后通过程序启动/TLS 检查。

本机完整 CTest 3/3 通过，客户端 18 项通过、1 项仅 Windows 的官方 MSI 检查跳过；Windows 客户端共 19 项通过。实际 UAC、服务/驱动安装、升级、卸载、数据保留及重启矩阵仍需在隔离 Windows 实机/虚拟机确认。最初网络/审批超时已恢复，不能把源码或 mock 通过解释为真实安装流程已经验收。
