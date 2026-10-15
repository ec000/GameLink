# Game Profile v1

`schema_version=1`；id 唯一，name 是展示名；mode 为 virtual_lan / virtual_ip / observe_only。
executables、ports 是经过验证的规则，默认空。candidate_executables 仅提示验证对象，不用于自动匹配或强制分流。
p2p_policy、allow_relay、lan_discovery 表示意图，不表示已安装流量过滤器。
steam_api_interception 必须 false；validation.status 必须 unverified 或 verified；verified 需要具体 version、OS、协议、日期和抓包证据文件。
用户手动路径保存在 GUI 配置中（不改变 profile 的验证状态）；进程检测使用所选文件 basename。
Warcraft III 传统 LAN 版本与 Reforged 不混为一谈；DST dedicated 与 listen server 分别验证；Overcooked! 2 只能观察，禁止把启动/UDP 出现当作 ZeroTier 替代 Steam 的证据。
