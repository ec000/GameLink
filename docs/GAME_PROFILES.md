# 游戏适配与验证

当前三个 profile 都是 unverified。未经用户机器/版本/抓包验证的进程名放在 candidate_executables，正式 executables 与 ports 为空。客户端只能手动路径启动与按 basename 检测进程，尚未自动扫描安装目录。不存在游戏 EXE/DLL 修改或 Steam 注入。

## Warcraft III（优先传统 LAN）

记录版本、发行渠道、补丁、进程名/hash、实际游戏 TCP/UDP 端口；两个 Windows 客户端使用同一兼容版本。
先验证虚拟 IPv4 echo，再观察游戏进程是否在 ZeroTier 适配器发送 LAN UDP broadcast/multicast；客户端接收、创建/显示 LAN 房间、加入、对战、退出重开均需测试。用 Wireshark 选择 ZT 虚拟网卡，分别观察广播目的地址、TCP 建连和实际游戏数据。

若找不到房间，先运行 client/platform/windows/diagnose.ps1 查看网卡 metric、路由和端口归属，不自动修改网卡优先级。必要调整由用户手动执行并记录/恢复原 metric。不要把只成功虚拟 IP echo 算作 LAN 房间发现成功；Reforged、经典版和不同地区构建分别列测试结果。

## Don't Starve Together

记录客户端/专服版本与 server.ini 的实际 UDP 端口，正式 profile 中暂不预填端口。分别测试 listen server LAN、专服 LAN、手动虚拟 IP 连接、分片/洞穴场景；使用游戏该版本支持的连接入口/控制台命令，避免未经核对的自动命令。

手动复制 Room 显示的虚拟 IP，确认专服监听该地址或可到达接口。将实际进程端口与 Wireshark 目标虚拟 IP/UDP 会话对应，记录 10 分钟游戏和重连、丢包情况。Steam 登录成功与 ZeroTier 数据面直连分别验收。

## Overcooked! 2（Observe Only）

仅手动启动、进程检测与只读诊断。运行：

```powershell
.\client\platform\windows\diagnose.ps1 -ProcessName 'Overcooked2' -Output '.\overcooked-observation.json'
```

进程名只是候选，按实际安装版本调整。结合 Steam 连接界面、进程 PID、Get-NetUDPEndpoint 与 Wireshark（物理与虚拟网卡）观察会话。Windows UDP endpoint 表不包含完整远端会话，必须抓包关联。没有自动 Steam 网络状态 API 集成；诊断是人工观测流程，不能说已识别 SDR。

只有实际游戏包出现在虚拟网卡并指向另一玩家的虚拟 IP，且双端 ZeroTier peer/path 证据对应，才可记录该联机模式兼容性。否则保持 observe_only。不替换 Steam DLL、不拦截 Steam API，不把 UI 中的 ZeroTier DIRECT 计作该游戏正在走 ZeroTier。

## 验证后升级 profile

复制具体版本的独立 profile，填正式 executable basename、协议/端口、LAN 行为，validation 记录 OS/日期/版本/抓包 SHA256 与结果。不能用一个版本的证据泛化所有版本。
