# ZeroTier 安装、服务与 API 检测

GameLink 0.2（2026-10-16）在 Home 中分开展示四类证据，不再把本地 API 请求失败统一解释为 ZeroTier 未安装/未运行。

| 显示项 | 来源 | 无 Token 时可用 |
|---|---|---|
| 安装检测 | 服务注册的程序路径、Program Files 常见目录中的程序文件 | 是 |
| Windows 服务 | 本机 SCM 的 ZeroTierOneService 状态 | 是 |
| 本地 API | 127.0.0.1 指定端口，凭证认证与 JSON/schema 检查 | 否；会提示凭证问题 |
| 节点 ONLINE/OFFLINE | /status 的 online 字段 | 否；API 失败/样本过期时显示未知 |

服务检测每 5 秒刷新，可点击“重新检测 ZeroTier”。只请求 SC_MANAGER_CONNECT、SERVICE_QUERY_STATUS 和独立的 SERVICE_QUERY_CONFIG；不调用 StartService、ControlService、ChangeServiceConfig，不自动提权。只读取程序路径，丢弃服务命令参数，不运行检测到的 EXE 或 CLI。

程序文件、服务注册和遗留数据目录分开处理：发现文件表示“已发现程序”；服务存在但无法读配置显示“已注册，程序路径未确认”；配置路径未发现文件显示修复提示；仅有数据目录不能确认安装；权限不足为未知。未发现标准安装不排除自定义/便携部署，检测不验证文件签名或安装完整性。非 Windows 平台明确标记安装/服务检测不支持。

端口从 `%ProgramData%/ZeroTier/One/zerotier-one.port` 独立读取，不读取 authtoken.secret。检测值不自动覆盖 Settings，只有点击“使用检测到的 API 端口”才修改 GameLink 自身设置；文件存在不代表 API 可访问。现有 Token 副本设置与 ACL 要求不变。

## API 诊断与处理

| 错误码 | 含义与处理 |
|---|---|
| INVALID_PORT | 端口不是 1–65535；检查 Settings 或应用检测到的端口 |
| TOKEN_PATH_EMPTY | 未指定本地凭证文件 |
| TOKEN_MISSING | 文件未找到或路径不可访问；核对路径与当前用户权限 |
| TOKEN_ACCESS_DENIED / TOKEN_READ_FAILED | 打开或读取失败；准备受保护且可读的本机凭证副本 |
| TOKEN_INVALID | 不是普通文件、空文件、过大或含无效字符 |
| AUTH_FAILED | HTTP 401/403；核对对应 Token 与访问权限，不回显服务端 body |
| CONNECTION_REFUSED | 目标拒绝连接；结合 Windows 服务状态核对是否未运行或端口不匹配 |
| REQUEST_TIMEOUT | 2 秒总请求时限到期；检查服务、端口和本机访问策略 |
| API_NOT_FOUND | HTTP 404；可能是端口错误或版本接口差异 |
| INVALID_JSON / API_SCHEMA_UNSUPPORTED | 响应不是预期 JSON 或缺少必需字段，不能继续当作有效路径证据 |
| RESPONSE_TOO_LARGE / REDIRECT_BLOCKED | 响应超过限制或发生重定向，主动拒绝 |

连接被拒绝或超时不能唯一定位根因；界面给出核对步骤，不声称已确定防火墙/NAT 故障。错误发生时清除路径/探测证据、关闭 echo socket，并隐藏过期 Node/IP；日志仅记录去重的阶段与错误说明，不输出 Token、服务参数或上游响应。

服务 Running 不等于节点 ONLINE；节点 ONLINE 也不等于目标玩家 DIRECT。服务已运行但缺少 Token 时，两条状态会同时显示，用户不必误以为服务需要重装。

依据：[ZeroTier 官方服务名称](https://docs.zerotier.com/faq/noservice/)、[QueryServiceStatusEx](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryservicestatusex)、[服务读取权限](https://learn.microsoft.com/en-us/windows/win32/services/service-security-and-access-rights)、[QueryServiceConfigW](https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-queryserviceconfigw)。
