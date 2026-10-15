# 部署与 Windows 安装

本次没有 VPS 地址/SSH 凭证，未访问或修改任何 VPS。以下命令供用户在审核后手动执行；服务文件不会被本项目自动安装，不会停止或重启现有 ZeroTier。

## Phase 0：VPS 只读评估

将 scripts/inspect_environment.py 和 scripts/inspect-vps.sh 放到 VPS 同一目录：

```sh
sh scripts/inspect-vps.sh gamelink-vps-report.md
```

脚本使用 timeout 子进程执行 uname、ZeroTier JSON CLI、systemctl status、ss、ip、sysctl、nft/iptables 的只读查询。普通权限失败记录 failed/unavailable，不解释为不存在。若需要完整 nft/服务信息，可自行以管理员权限运行同一只读脚本；无自动 sudo。只写输出报告，不输出 token/identity.secret，不运行 dump。

人工审核：版本、Controller API 可达性、许可、moons.d 与 /moon、TCP fallback 当前活动/配置、默认 route、IP forwarding、bridge、NAT/filter 规则、已监听端口。planet 的存在不能判断是否自建 Root，需与准确发行包 checksum/运维记录比对。Controller API 列表 200 说明接口可用；是否实际托管网络看列表和已配置网络详情。无法读取或未知字段保持 UNKNOWN。

## 编译 Linux 服务

```sh
cd server
go test -race ./...
go build -trimpath -o ../build/server/gamelink-server ./cmd/gamelink-server
# 在 macOS/Windows 交叉编译 Linux amd64：
GOOS=linux GOARCH=amd64 go build -trimpath -o ../build/server/gamelink-server-linux-amd64 ./cmd/gamelink-server
```

复制与 VPS 架构匹配的产物。服务没有游戏 UDP listener，不绑定 ZT 虚拟网卡，无 ip_forward/NAT/bridge 配置。

## 专用网络池与配置

请管理员手动准备**新的专用私有测试网络**并确认授权为空、开启需要的 LAN broadcast、仅虚拟子网直连 route（via=null）、v4 自动分配，网络之间子网不重叠。不使用承载 SSH/生产业务的现有网络。不自动创建/删除/重建 Controller。

复制 deploy/server.example.json 到 /etc/gamelink/server.json，替换 networks、users、controllerUrl/TokenFile 的占位符。启动会拒绝无效 ID、公有网络、default/gateway route、未纳管授权成员。pool 大小即并行房间上限，每房间最多 8 成员。

生成高熵个人 token 和 hash（只在管理员终端显示一次，安全交给对应用户，勿写日志）：

```sh
python3 -c 'import secrets,hashlib; t=secrets.token_hex(32); print("token:",t); print("tokenHash:",hashlib.sha256(t.encode()).hexdigest())'
```

每个 user.nodeId 由客户端官方 ZeroTier info 的真实 ID 核对，不能使用 mock 的 aaaaaaaaaa。controller-token.secret 使用现有本地管理 Token 的受保护副本，GameLink 用户需可读；不改变 ZeroTier 原始 Token 文件权限，不对公网暴露本地 API。

确认目录/用户不存在冲突后，可手动安装：

```sh
sudo useradd --system --home /var/lib/gamelink --shell /usr/sbin/nologin gamelink
sudo install -d -o root -g gamelink -m 0750 /etc/gamelink
sudo install -d -o gamelink -g gamelink -m 0700 /var/lib/gamelink
sudo install -d -o root -g root -m 0755 /opt/gamelink
sudo install -m 0755 build/server/gamelink-server-linux-amd64 /opt/gamelink/gamelink-server
# 先手动填好 /etc/gamelink/server.json 和 controller-token.secret，再设置权限：
sudo chown root:gamelink /etc/gamelink/server.json /etc/gamelink/controller-token.secret
sudo chmod 0640 /etc/gamelink/server.json /etc/gamelink/controller-token.secret
sudo install -m 0644 deploy/gamelink.service /etc/systemd/system/gamelink.service
sudo systemctl daemon-reload
sudo systemctl enable --now gamelink
sudo journalctl -u gamelink --no-pager -n 50
```

这些命令只安装新 GameLink 服务；不要照搬在已有同名部署上覆盖，先比对。不要对 zerotier-one 执行 restart。systemd 配置未在目标 Linux 实机验证。长期运行应安排安全更新、监控持久化失败和 revocation_pending，不删除状态文件。

在**现有 HTTPS 反向代理配置**中人工添加 GameLink 站点，参考 deploy/Caddyfile.example。公网仅开放业务所需的 HTTPS，8080 仅 loopback。不要覆盖已有代理配置；本项目没有自动装 Caddy/改防火墙。health 需认证，返回进程状态/模式，不证明 Controller 或全网 P2P 可用。

## Windows 客户端凭证

GUI manifest 为 asInvoker，不要求管理员。MVP 使用用户 ACL 限制的本地 Token 副本；由拥有本机管理权的用户手动准备 `$env:LOCALAPPDATA\GameLink\zerotier-token.secret`，不要把内容放在脚本参数、日志或截图中，确保仅对应用户和 SYSTEM 可读。原始 Token 一般在 `%ProgramData%\ZeroTier\One\authtoken.secret`，实际位置与本地 API port 应从安装版本确认。

复制本地 Token 等于授予该用户本地 API 管理能力，不能在不可信多用户电脑上当作受限 broker 使用。需要最小权限时，应暂缓部署 GUI 的写操作，等待独立 broker。不要把 GUI 以管理员运行来绕过权限问题。

## Windows 打包与卸载

README 的 build-windows.ps1 使用 Qt CMake deployment 安装依赖到 build/stage，生成 ZIP；安装 Inno Setup 时可生成 per-user Setup.exe。安装器不捆绑 ZeroTier、不加防火墙规则、不改变路由/DNS、不开机自启。分发前加入应用签名和实际 Qt 许可材料，并在干净 Windows VM 验证运行库、驱动依赖及卸载。

未在当前 macOS 生成 Windows 安装包。卸载仅移除应用；Qt 设置和用户本地 Token 副本需用户手动删除，官方 ZeroTier 网络也由用户管理。GUI 日志限 1000 条，可主动导出，不自动收集游戏流量。

## UDP 探测与防火墙

Diagnostics 默认 UDP 39755，双方仅绑定各自虚拟 IPv4。Windows 防火墙可能阻止 echo；如需要，由管理员为**具体 GameLink.exe、ZeroTier 接口、本机虚拟 IP 和目标虚拟子网**增加 UDP 入站规则，并用带唯一名称的规则手动移除。不要关闭防火墙或允许所有 Steam 进程。UDP echo 并非游戏端口；游戏端口必须逐版本实测后单独配置。本项目不自动修改规则，因此没有自动清理用户规则的动作。
