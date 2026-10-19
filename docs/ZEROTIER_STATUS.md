# ZeroTier 状态页（0.6）

左侧新增独立“ZeroTier 状态”页面，集中显示本机 Node ID、在线/离线/TCP 回退、版本、目标 Network ID、入网阶段和所有本机网络的名称、状态、虚拟 IP。安装检测、Windows 服务、本地 API、凭证/端口设置、MSI 安装修复与卸载也集中到此页；概览保留摘要与快捷入口。

## 加入与离开

- 无房间时输入 16 位 Network ID 后点“加入网络”；有房间时目标固定为房间的 Network ID，界面分别显示房间 ID 和 Network ID。房间页原有“连接虚拟网络”保留。
- 加入通过本地 API `POST /network/<id>`；新网络允许 Managed IP，禁用 default/global/DNS。已有网络保留原配置，不重复更改，启用这些路由/DNS 设置时显示待审核状态。
- 表格选中本机网络后点“离开选中网络”，确认框明确显示 Network ID；通过本地 API `DELETE /network/<id>` 请求退出。只退出选中项，其他网络保留。
- 退网与“离开房间”分开。退网保留 GameLink 房间、控制器授权、Moon/Planet；离开房间会请求服务端撤销授权，但保留本机网络配置。重新加入同一网络时仍以控制器实际授权为准。
- 请求成功后刷新实际网络列表，不能把 HTTP 请求接受当作已获得授权或 IP。失败显示明确错误，等待下一次读取确认实际状态，不伪造成功或删除表格项。

## 状态含义

| 界面状态 | 本地 API 依据 |
|---|---|
| 未加入 | 新鲜网络列表中没有目标 ID |
| 等待授权 | ACCESS_DENIED |
| 等待配置 | REQUESTING_CONFIGURATION |
| 等待分配 IP | OK 且节点在线，但没有 assignedAddresses |
| 已连接 | OK、节点在线、有虚拟 IP，且未启用 default/global/DNS |
| 已加入 · 节点离线 | OK，但节点 online=false |
| 网络不存在 / 网卡异常 / 需要升级 / 需要额外身份验证 | 对应 ZeroTier 状态码 |
| 状态未知 / 状态过期 | API 不可用、凭据/端口已变，或样本超过 6 秒 |

节点在线不证明已入网，入网成功也不证明玩家直连或游戏连通。状态每约 2 秒轮询，可手动刷新；底部显示最近事件，完整事件在“网络诊断”。API 端口与 Token 文件仍需正确配置，程序不会把服务器控制器密钥写入客户端。

加入/离开与 Windows Installer 操作互斥，进行中禁止重复请求和切换本地凭证/端口，旧请求回包不能恢复过期状态。退网确认后重新核对身份、目标存在状态及凭证上下文。

本地与 Windows 回归使用临时回环 HTTP 模拟服务验证状态迁移和退网语义；这些测试不会加入用户的真实网络。真实授权/P2P/游戏需单独验收。

## 本地 API 凭据与刷新排查（0.6.2）

服务 Running、服务端显示节点 ONLINE，而 GameLink 报 TOKEN_MISSING 时，是 GUI 缺少本机 API 凭据，不能据此认定节点离线。无需重新 join、授权或更换 planet/moon。

Windows 自动检测以下已有文件（不生成、复制或修改它们）：

1. `%LOCALAPPDATA%/ZeroTier/One/authtoken.secret`（新版官方用户副本）。
2. `%LOCALAPPDATA%/ZeroTier/authtoken.secret`（旧版官方用户副本）。
3. `%PROGRAMDATA%/ZeroTier/One/authtoken.secret`（仅当前用户已有读取权限时）。

启动时，仅在路径为空，或旧默认 `%LOCALAPPDATA%/GameLink/zerotier-token.secret` 缺失时自动检测；用户显式配置的其他路径不会被自动覆盖。点击页面顶部“检测本机凭据”可主动重新检测，先尝试当前路径，再尝试官方路径。每个候选都通过当前配置端口的回环 `/status` 验证，旧副本 401/403 时继续尝试其他位置，成功才保存路径。检测本身仅 GET，不 join/leave 或启停服务。

若没有可读凭据，先从开始菜单启动官方 ZeroTier One 托盘程序，完成它自己的权限提示，再回 GameLink 检测。也可在“本地 ZeroTier”选择已有受保护的本机 Token 文件。手动输入请使用完整路径（例如 `C:/Users/用户名/AppData/Local/ZeroTier/authtoken.secret`），不直接输入 `%LOCALAPPDATA%` 环境变量表达式。保存并刷新后，已加入的所有网络会直接列出名称、状态和虚拟 IP，无需重复加入。

凭据检测不会更改 API 端口；连接被拒绝或超时时应核对服务及检测到的端口。凭据内容只发送给 `127.0.0.1`，不写日志或普通设置。只需配置每台电脑自己的本地 Token，不能使用 VPS Controller 管理密钥。

路径依据：官方 [DesktopUI serviceclient.rs](https://github.com/zerotier/DesktopUI/blob/main/src/serviceclient.rs) 兼容新版与旧版用户副本；旧副本失效的情况也见官方 [issue 44](https://github.com/zerotier/DesktopUI/issues/44)。

### 文件存在，但 API 返回 401/403

这说明 GameLink 已读取文件并向本机接口发出请求，但凭据未被接受；不等同于网络未授权或节点离线。用户目录中的副本可能与当前服务 Token 不一致，仅找到文件不能修复这一情况。

先点页面顶部的“检测本机凭据”，尝试其他可读的官方位置。环境卡片的“重新检测”只查询安装/服务/端口并刷新当前配置，不会切换 Token 路径。

若仍失败，可按官方用户凭据恢复流程处理：

1. 退出官方 ZeroTier 托盘程序（只退出界面，不停止系统服务）。
2. 将 `%LOCALAPPDATA%/ZeroTier/authtoken.secret` 改名为 `authtoken.secret.bak`；若 `ZeroTier/One/authtoken.secret` 也存在，同样改名，已有备份则使用其他备份名。只处理当前用户目录中的副本。
3. 从开始菜单重新打开官方 ZeroTier One，完成它的权限提示，使其重新取得本机服务凭据。
4. 回到 GameLink 点“检测本机凭据”，然后检查网络列表和 Node ID。

不要改动 `%PROGRAMDATA%/ZeroTier/One/authtoken.secret`、identity 文件、planet/moon 或 networks.d，也无需为此在服务器添加 authTokens。恢复步骤以官方 [用户 Token 清理说明](https://docs.zerotier.com/faq/appcrashes/) 和 DesktopUI 的重新取证流程为依据；此处用改名保留用户旧副本。服务 Token 的位置与用途见 [官方 Token 文档](https://docs.zerotier.com/tokens/#zerotierone-local-service-token)。

若当前服务 Token 仍无法认证，需要进一步核对本机实际服务数据目录、端口和服务实例。不要上传 Token 内容；只有 Windows 本机验证结果才能确认具体原因。
