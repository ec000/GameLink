# 个人安装包中的自建根节点

个人离线构建可同时内嵌用户提供的 `planet` 和一个 `.moon`。文件通过临时 Actions Secret 输入，根节点地址和二进制不写入公有仓库；生成的安装包仍只以认证加密形式传回本机。

## 安装行为

安装界面的“配置内置自建 Planet 和 Moon”默认勾选，可以取消。安装完官方 ZeroTier MSI 后，单独请求 Windows 管理员授权，向标准 `ProgramData\ZeroTier\One\planet` 和 `moons.d\<world-id>.moon` 写入文件。安装器和提权脚本都检查构建时固定的 SHA-256；文件在安装器调用期间保持禁止写入的锁。

更新前停止服务，将被替换的文件及存在状态保存在 `ProgramData\ZeroTier\One\gamelink-root-backups\<时间-随机ID>`，仅管理员/SYSTEM 可访问。发生部分写入失败时恢复原文件；原先运行的服务会重启，原先停止的服务保持停止。文件相同的重复安装不重启、不增加备份。不会复制或删除 identity、Token、networks.d、其他 Moon。GameLink 卸载保留独立 ZeroTier 和已应用的根节点配置；恢复官方根节点需管理员从备份恢复 planet，并移除本次增加的 Moon，再重启服务。

本版本仅支持标准 x64 官方 Windows MSI 安装，不接受重定向目录或自定义服务命令。替换机器级根节点会影响该电脑上所有使用 ZeroTier 的程序，安装任务中有明确提示。如果 MSI 要求重启，先手动重启，再重新运行配置。

## 文件检查与局限

构建时按 ZeroTier 1.16.2 的 World/Identity/InetAddress 序列化格式检查类型、长度、World ID、公开身份、端点和 Moon 文件名；拒绝含私钥的身份和未知扩展。这是格式与完整性检查，不是对签名密钥归属或服务器可达性的认证。

用户本次提供的 Planet 与 Moon 指向相同的一个根节点，因此不能视为两台冗余服务器。版本时间戳不是失效日期；实际服务健康、UDP 可达性、双方是否使用相容根节点，需要在真实网络验证。Moon 已被官方标为弃用；保留此次既有部署的兼容配置，未宣称新增官方支持。

参考：[官方根节点说明](https://docs.zerotier.com/roots/)、[1.16.2 World 格式](https://github.com/zerotier/ZeroTierOne/blob/1.16.2/node/World.hpp)。

## 新用户更快加入网络

这次解决安装服务和手动复制根节点文件两步。根节点配置不会携带 Network ID、创建控制器用户或授权成员，安装器不会猜测 Network ID，也不会自动加入未知网络。

当前可用流程：安装并配置根节点 → 配置受保护的本地 API Token → 在 GameLink 填 Network ID 并连接，或配置 GameLink 服务端/用户凭据后使用房间。私有网络仍需控制器授权。

后续减少步骤的方向：提供 Network ID、GameLink HTTPS 服务地址和用户注册/登录方案；通过有效邀请绑定本机 Node ID，在控制器授权成功后由明确的“加入房间并连接”操作发起入网，并显示“等待授权/等待 IP/已连接”。服务端控制器密钥不能下发到安装包；本地 Token 的普通用户访问需要专门的受限提权设计，不能将机器级凭据开放给 Everyone。当前 GUI 基线未改变，以上自动入网流程尚未实现。

## 构建

`scripts/package-personal-offline.ps1` 增加 `-CustomRootsDirectory`，目录内必须包含 `planet` 和以 World ID 命名的一个 `.moon`。输出 `GameLink-0.5.0-CustomRoots-Offline-Personal-Setup.exe`。普通个人包和默认公有在线包不携带这些根节点。

私有工作流以 `custom_roots=true` 启动，临时 `GAMELINK_CUSTOM_ROOTS` Secret 为仅含 `planet`、`moon` 两个 Base64 字段的 JSON；构建后删除该 Secret 和一次性传输密钥、密文产物。
