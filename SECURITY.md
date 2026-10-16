# 安全边界

## 凭证与权限

GameLink API 个人凭证与 ZeroTier 本地 authtoken.secret 是不同凭证。GameLink token 使用 CSPRNG 至少 32 字节随机值（hex 编码），服务器仅存 SHA-256 hash；不是密码登录，不接受低熵密码。管理员预绑定真实 Node ID，一账号只允许一个房间；不存在客户端任意 nodeId 授权接口。

Controller 管理 Token 仅留在 VPS，服务通过 numeric loopback HTTP 使用，不允许代理或重定向；GameLink API 不返回它。GUI 的本地 ZT token 只用于 127.0.0.1 API，不发给 GameLink Server，不写日志/普通设置、不放在进程命令行。

MVP 尚无受限 broker。标准用户需要读取**本机用户自己控制的**受 ACL 保护 Token 副本；复制该 Token 会赋予用户对本地 ZeroTier API 的管理能力，不是按网络隔离的受限权限。多用户/不可信 Windows 电脑不宜部署该方案，后续应使用独立服务按 SID 和网络 allowlist 限制操作。GUI 不自动复制 Token，不以管理员身份运行。

VPS 上专用用户 gamelink，/etc/gamelink/server.json 和 controller-token.secret root:gamelink 0640，状态目录 0700。上游本地管理 Token 权限很大；应用的 network allowlist 不是 token 自身权限隔离。密钥轮换在服务端配置完成后重启；客户端内存凭证重新输入。报告含 IP/端口拓扑，分享前脱敏。

## 房间权限与隔离

只有个人凭证不能随意加入房间，还需 128-bit 邀请密钥；GET 仅成员访问。邀请只在创建时返回，其他 API 响应不含；GUI 复制是用户操作。HTTPS 必需，公网不得直接暴露 8080 或 Controller 管理端口。不要在反向代理启用记录 Authorization/body 的日志。

一房间一个专用私有网络，启动检查未纳管授权成员，拒绝默认/经网关的 route。GUI join 设置 allowDefault/allowGlobal/allowDNS=false，仅针对新加入的所选网络；已有网络保持原设置且风险状态拒绝 ready。运维应避免网络池子网重叠并关闭桥接。

此版本用于可信玩家试验，不是敌对租户环境。虚拟网段可承载其他主动访问它的应用，尚无进程/端口 ACL。Controller 撤销有证书缓存/传播延迟；恶意退出成员即时隔离、旧证书在网络复用时的效果尚未验证，不承诺即时断开或强多租户隔离。生产强隔离需要不跨房复用网络与独立验证的流规则/证书策略。

## 状态与流量约束

DIRECT 是某次目标 LEAF 路径样本的证据，6 秒 GUI TTL / 10 秒路径收包阈值，不是逐包保证，也不保证下一个包仍走直连。UDP echo nonce + 源地址校验防止误认旧包，echo 只接受房间授权 IP、限速、小包；不是对恶意已授权玩家的加密认证。IPv6 echo 暂未实现。

Direct Required 始终 unsupported，服务器 422，GUI 拒绝相关 join/launch/probe 操作；不保证已经另行运行的游戏被阻断。尚未使用 WFP 或 Windows 防火墙自动规则，因而退出/崩溃无需恢复 GameLink 网络过滤规则。仅房间授权意图持久化恢复，GUI 退出保留本地 ZT membership。

## 运行约束

API body 8 KiB、请求超时、客户端/Controller 响应大小限制、邀请常量时间比较、单 IP/用户限流、单实例文件锁与原子 fsync journal。后端操作串行，适用于小规模 MVP；恶意请求可占用有限 Controller 时隙，公网应在反向代理进一步限制连接。当前不信任 X-Forwarded-For，因此所有反代流量共享 IP 限额；扩展规模时仅从受信代理读取真实来源。

异常授权/撤销保留持久化意图，网络隔离等待重试。存储故障使 API 返回 503，需要修复并重启；不要删 journal“修好”服务。Controller 断网/停止不必然立即断开已授权玩家。关闭进程不能消除既有证书，数据面安全不能依赖控制平面 TTL。

不复制 ZeroTier 源码/二进制、不修改游戏/Steam DLL、不注入进程、不关闭防火墙。具体第三方版本与许可由部署前核对；参考 docs/RESEARCH.md。

## 安装与服务只读检测（0.2）

安装/服务检测与 Token/API 分离。SCM 仅使用连接和查询权限，状态查询和配置查询分别申请；配置读取被拒不会覆盖成功的运行状态。只展示可执行文件路径，丢弃参数，不执行发现的文件，不启停/重装服务。权限/文件证据不足显示未知，不据此自动提权。端口检测只读 zerotier-one.port，使用检测值需用户点击，不读取/复制原始 authtoken.secret。
