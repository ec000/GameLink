# GameLink 验收计划

本次结果见 docs/TEST_REPORT.md。mock、核心单测、真实 ZeroTier 与游戏实测分开记录。

## 自动化

README 提供完整命令。C++ 无 Qt 核心验证状态、时效、schema、role、TCP fallback 与策略；Go 运行 `go test -race -cover ./...`，包含真实 loopback HTTP adapter，controller 是 fixture/mock；Python 验证调查输出不含状态配置中的秘密和 profile 的未验证标识。
实际进程冒烟 `python3 tests/server_smoke.py --binary build/server/gamelink-server` 测 health、严格模式拒绝、创建、邀请、ACL、peers、离开。无外部依赖，不会操作真实网络。

## 双端网络矩阵（全部待实测）

| 场景 | A | B | 需要记录 |
|---|---|---|---|
| 1 | 同一 LAN | 同一 LAN | 物理直连、ZT direct、广播、游戏 |
| 2 | 电信 CGNAT | 电信 CGNAT | 端口映射/peer路径、是否 fallback |
| 3 | 电信 CGNAT | 联通 CGNAT | 跨运营商 RTT/jitter/loss、路径 |
| 4 | 家庭宽带 | 手机热点 | 网络切换前后、过期失效 |
| 5 | 公网 IPv4 | CGNAT | 双向路径与防火墙 |
| 6 | 严格 NAT | 严格 NAT | 实际 relay/unknown/失败，不强求直连 |

每组至少双端持续采样 10 分钟，游戏开始前/中/后各 60 秒。记录 OS、ZeroTier version、游戏 version、ISP/NAT 条件（无法确认记 unknown）、时间同步、Node IDs、虚拟地址、物理路径、网络 ID、策略、截图与脱敏抓包。

## 集成用例

| 用例 | 验收标准 | 当前自动化范围 |
|---|---|---|
| ZeroTier 在线 | /status online；不推导任意 peer direct | 状态模型；真实服务待测 |
| Join / 授权 | 本机 /network OK、有 IP；Controller authorized | 授权 adapter fixture；真实 join 待测 |
| DIRECT | 目标 LEAF 活动首选路径、最近接收、双端包证据 | C++ 分类已测；真实路径待测 |
| RELAY | tunneled 或无 preferred 推断，显示证据范围 | C++ 已测；真实 relay 待测 |
| UNKNOWN | 缺字段、未知角色、过期/矛盾路径 | C++ 已测 |
| 断线与重试 | 清 direct、有限时间 traversal、服务恢复继续轮询 | 核心已测；Qt mock 时序已测；真实环境待测 |
| 网络切换 | 原 echo socket 关闭、探测证据失效、新 IP 重新绑定 | 源码；实机待测 |
| ZeroTier 重启 | 立即或 TTL 内失效，不沿用旧 direct | 核心；实机待测 |
| GUI 退出/重启 | 不改变既有网络、清内存 token、TTL 清房间 | Qt mock 启动/离开已测；server journal 重启已测 |
| VPS 不可达 | 房间更新显示错误，既有 ZT 可能继续通信 | Qt HTTP 错误处理已测；真实端到端待测 |
| Strict Direct | API 422、GUI 拒绝；不能标为内核阻断已实现 | API/核心已测，严格流量保证不支持 |
| Controller 掉线 | 失败保留撤销意图，不分配受影响网络 | 故障注入已测 |
| 存储失败/磁盘满 | 503 unhealthy，重启先恢复 journal | 待故障注入实机验证 |
| 同时 join / 限流 | 幂等、无重复授权成员、429 | Go 并发/限流已测 |
| 独立网络池 | 不允许其他授权成员/default/gateway route | adapter fixture 已测，证书缓存隔离待测 |

## VPS 流量与实际路径联合验证

不要以低 VPS 带宽单独证明 P2P。先只读记录接口和 socket：

```sh
ip -s link
ss -u -a -n -p
```

三个时间窗口各保存 ip -s link 的 counters。按 VPS **实际物理接口与实际 ZeroTier 端口**选择过滤；以下占位符需替换，不默认所有流量都在 UDP 9993。可在获得本机抓包权限后使用：

```sh
sudo timeout 60 tcpdump -i <vps-interface> -nn -s 96 -w before.pcap 'udp or tcp port 443'
sudo timeout 60 tcpdump -i <vps-interface> -nn -s 96 -w during.pcap 'udp or tcp port 443'
sudo timeout 60 tcpdump -i <vps-interface> -nn -s 96 -w after.pcap 'udp or tcp port 443'
```

只采集必要范围；上例 broad UDP 可能包含其他业务元数据，实际应缩小到测试客户端公网地址及已观测端口，不把完整 pcap 发布。VPS 如果是 Root/Moon，本来可能转发，要单独识别；如果只有 Controller，不应因此推断没有官方 Root relay。

双端同时抓物理网卡与 ZeroTier 虚拟网卡：

1. 通过虚拟网卡确认游戏进程目标为对端虚拟 IP，记录实际 TCP/UDP 5 元组；Overcooked 不能跳过这一项。
2. /peer 按对端 Node ID 记录 preferred/active/expired/lastReceive/tunneled，区分 root 角色。
3. 对照物理 UDP 目标与对端公网端点，核对双端时间和流量方向；加密 outer 无法直接读游戏 payload，需时间/速率/对端捕获交叉验证。
4. 排除 TCP fallback/官方根转发、物理 LAN 本地直连游戏流、Steam SDR 或其他业务。
5. 结合 VPS counters 和抓包说明它是否转发了游戏数据，无法关联时写 UNKNOWN。

最严苛验收需在游戏运行中人为使直连失败，观察路径切换、6 秒样本 TTL 和 strict unsupported 提示。MVP 没有逐包阻断能力，不能将 Preferred Direct 的 UI 拒绝启动解释成 Direct Required 验收通过。

## Windows 打包验收

干净 Windows 10/11 VM：标准用户安装/启动、Qt DLL/运行库、ZeroTier 单独安装、token 权限错误提示、网络加入/原配置保留、卸载、重启与异常结束。防火墙保持开启，浏览器/Steam 下载继续正常。Qt GUI 在本机和 Windows CI 已通过编译、mock 集成和启动检查；安装器、用户桌面交互与真实网络仍未验收，不能当作已发布 Windows 成品。
