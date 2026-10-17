# ZeroTier 调研记录

查阅日期 2026-10-15。浏览器读取官方文档与 upstream dev 源码；未 clone、未编译 ZeroTier，网络受限，无法通过 GitHub API 固定 commit，因此这些引用是移动分支，必须在目标安装版本重测。

- [Protocol](https://docs.zerotier.com/protocol/)：VL1 roots 与 VL2 controller 职责不同，首次 rendezvous 可能经过 root；CGNAT 不保证可打洞。
- [Service 源码 _peerToJson / statusGet](https://github.com/zerotier/ZeroTierOne/blob/dev/service/OneService.cpp)：/status 提供 clock、tcpFallbackActive；/peer 提供 address、role、tunneled、paths；path 包括 preferred、active、expired、lastReceive。tunneled 是服务全局 TCP fallback 标志，不能称为精确逐玩家路径遥测。
- [CLI 源码 peers 分支](https://github.com/zerotier/ZeroTierOne/blob/dev/one.cpp)：官方可读输出基于 preferred 路径以及 tunneled 生成 DIRECT/RELAY，没有 preferred 时显示 RELAY。JSON 本身没有必须存在的 DIRECT 字符串。GameLink 添加 TTL、active 和最近接收校验以保守判断；缺字段 UNKNOWN。
- [Controller 文档](https://docs.zerotier.com/controller/)：GET /controller/network/{nwid}、成员列表/明细；POST /controller/network/{nwid}/member/{nodeId} 使用 authorized。删除成员不会阻止重新出现，MVP 不删成员/网络。
- [Service API](https://docs.zerotier.com/api/service/v1/)：本次页面抓取失败，接口交叉核对上面的官方源码；需对安装版本验证。
- [Relaying](https://docs.zerotier.com/faq/relaying/) 与 [Roots](https://docs.zerotier.com/roots/)：root/moon 可能转发，本项目不改变配置。

## 许可检查

[upstream README](https://github.com/zerotier/ZeroTierOne) 将 agent/service 与 nonfree 目录区分；[MPL](https://github.com/zerotier/ZeroTierOne/blob/dev/LICENSE-MPL.txt) 覆盖相应 agent 源码；[nonfree/LICENSE.md](https://github.com/zerotier/ZeroTierOne/blob/dev/nonfree/LICENSE.md) 对 Controller 等组件有独立条款。不能笼统宣称整包都是 MPL。本项目不复制/打包 ZeroTier 二进制，Windows 用户单独安装官方发行版。VPS 具体版本、构建选项及许可未知，上线/商业分发前针对实际发行包核对。Qt 动态库分发也需要满足实际 Qt 许可，安装方案保留该责任。

## Windows 服务安装管理核对（2026-10-17）

- [官方下载页](https://www.zerotier.com/download/) 指向官方 Windows MSI；本轮实际下载并只读检查固定 [1.16.2 MSI](https://download.zerotier.com/RELEASES/1.16.2/dist/ZeroTier%20One.msi)，SHA-256 为 42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670。
- [upstream Windows MSI 工程](https://github.com/zerotier/ZeroTierOne/blob/dev/ext/installfiles/windows/ZeroTier%20One.aip) 当前声明 ProductName=ZeroTier One、Manufacturer=ZeroTier, Inc.、ProductVersion=1.16.2，并要求管理权限。移动分支引用仅作交叉核对，固定测试样本由上面的版本与哈希确定。
- Microsoft [MsiEnumProductsExW](https://learn.microsoft.com/en-us/windows/win32/api/msi/nf-msi-msienumproductsexw)：使用 MACHINE 上下文、NULL 用户 SID 枚举计算机级安装，产品通过同线程递增索引读取。卸载按产品 GUID 调用 Windows Installer，未执行注册表中的任意卸载命令。
- Microsoft [ShellExecuteExW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw) 与 [WinVerifyTrust](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)：主 GUI 不提权，明确操作后由系统安装器请求 UAC；安装前验证签名与发布者，随后跟踪安装器进程结果。

Windows CI 已只读验证该官方 MSI 的正向识别；真实安装/驱动/UAC 流程尚未验收。GameLink 不分发下载的 ZeroTier MSI，不自动接受其安装许可，也不更改其 planet/moon 配置。
