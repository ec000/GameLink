# GameLink MVP 架构

状态：2026-10-15，Phase 0/1 设计基线；Phase 2 实现范围见 README。

## 网络边界

GUI → GameLink HTTPS API → localhost ZeroTier Controller API，是控制平面。
游戏 → ZeroTier 虚拟网卡 → 对端物理 UDP 路径，是期望的数据平面。
不能保证所有 NAT 组合都能直连。GameLink 没有转发游戏数据的服务器端组件。

Controller 在 VL2 管理网络配置、成员证书和授权；Root 在 VL1 帮助发现与 rendezvous，也可能转发。Planet 是默认根集合，Moon 是附加根集合；Relay 是实际转发行为，TCP fallback 是另一种回退。自建 Controller 不等于自建 Root，更不等于禁止官方中继。本项目不修改 Planet、Moon、Root、local.conf、系统默认路由或 DNS。

## 模块

- client/core：无 Qt 的状态机、探测统计、策略能力判断，可在当前 macOS 编译测试。
- client/gui：Qt 6 Widgets，Home、Games、Room、Diagnostics、Settings；QNetworkAccessManager 访问本地 API 与房间 API。
- client/platform：Windows 原生 SCM 只读服务查询与独立安装/端口文件检测；新增独立 MSI 安装管理器，通过签名/产品验证后单次启动系统 msiexec，使用 Windows UAC，详见 docs/ZEROTIER_INSTALLATION.md；client/platform/windows 含标准用户 manifest、只读网卡/进程诊断脚本。没有提权 GUI、WFP 驱动或防火墙自动修改。
- server：Go 标准库 HTTP 服务；认证、固定 Node ID 绑定、房间、TTL、限流、日志、持久化撤销意图、Controller adapter。
- profiles：版本化 JSON。候选进程名、端口与实测验证分开；没有验证记录不得标为已验证支持。

## 房间隔离与授权

MVP 使用运维**事先单独准备并明确交给 GameLink 的私有网络池**，一房间独占一 network ID，不复用生产网络。
服务只 GET 网络配置、GET 成员、POST 成员 authorized，不创建/删除网络。启动检查 private=true、禁止 routed via/default route、禁止已有未纳管的 authorized 成员；失败拒绝启动。池耗尽返回 503。
每个 API 凭证由管理员绑定 user ID 和固定 ZeroTier Node ID，客户端不能自行认领别人的 Node ID。房主生成随机房间 ID 和 128-bit 邀请密钥；读操作仅成员可用；join 必须持有邀请密钥。
成员意图在授权之前原子写盘。撤销失败时保留房间和网络占用，定时重试，不把网络交给新房间。重启先撤销 journal 中所有旧房间成员，再开放服务；旧房间不恢复。journal 丢失/损坏不得跳过校验。
Controller 撤销不是即时数据面断开保证：已发证书/对端缓存有传播时延，不能提供对恶意客户端的即时踢出隔离。高安全隔离应使用永不跨房复用的网络和经验证的证书 TTL/流规则；MVP 仅适合可信玩家测试，不宣称强多租户隔离。

## P2P 状态机

DISCONNECTED → CONNECTING → NAT_TRAVERSAL → DIRECT / RELAY / UNKNOWN / FAILED。
- 服务离线或不可访问：DISCONNECTED；旧 direct 证据立即失效。
- 加入/授权中：CONNECTING；目标尚未出现在 peers：NAT_TRAVERSAL，最多 30 秒，再 FAILED。
- 按目标 Node ID 找 LEAF；只对了解的 JSON schema 判断。snapshot 超过 6 秒、字段缺失/矛盾、版本未知路径语义：UNKNOWN。
- tunneled=true：RELAY（服务层 TCP fallback 证据，不能定位中继地址）。
- preferred + active + !expired 的物理路径且最近收到数据 ≤ 10 秒：DIRECT。
- 符合支持 schema 的 LEAF 没有 preferred 路径：RELAY，含义与官方 CLI 一致，是路径回退推断，不证明游戏正在发包。
- 其他情况：UNKNOWN，不以 ONLINE、低 RTT、VPS 低流量推断 direct。

每 2 秒轮询；串行采样 /peer → /status → /network，让 clock 不早于 path.lastReceive，只发布完整样本；单次 HTTP 最多 2 秒，旧样本有独立单调计时到期。timestamp 使用 /status.clock 与 path.lastReceive 比较，不与 GUI 墙钟混用。网络变化/服务重启清除证据。状态事件仅记录 Node ID、状态、物理路径及理由，不记录 token、邀请密钥或完整 API body。
虚拟 IP UDP echo 使用随机 nonce、预期源地址/端口和超时计算 RTT、相邻 RTT 差均值 jitter、loss；只监听用户选择的本机虚拟 IPv4，无默认 0.0.0.0 绑定。不等同于游戏协议的连通性证明。

## 三种策略

Preferred Direct 为默认：有限时间继续探测，明确显示中继，允许官方正常 fallback。
Relay Allowed：显式选择，显示性能/带宽风险；仍不将 VPS 配成 relay。
Direct Required：MVP 能力返回 unsupported，服务器拒绝此房间策略，GUI 禁止相应 join/启动操作。没有声称阻断已经在系统运行的游戏。详见 docs/STRICT_DIRECT.md。
profile.allow_relay=false 表示不希望回退的产品偏好，**不是**已执行的网络 ACL；必须结合策略能力解释，不能据此声称阻断。

## 控制平面失联

VPS 失联时禁用房间变更并显示错误；既有 ZeroTier 网络可能继续传输。不会自动退出已有网络，避免破坏用户配置。
GUI 退出不触碰系统路由和既有 ZT memberships。房间显式 leave 或 TTL 触发 Controller 撤销；GUI 未正常退出依赖 TTL。客户端网络 join 是用户明确操作，会保留，由官方客户端管理；不自动 leave 用户已有网络。

## 后续

独立低权限 broker（命名管道 ACL、调用者 SID、仅允许管理池网络）、安装与签名、WFP 动态过滤器/watchdog、LAN 广播实测、游戏自动扫描、双端路径交叉核验。没有把这些未完成项计作 MVP 能力。
