# ZeroTier 调研记录

查阅日期 2026-10-15。浏览器读取官方文档与 upstream dev 源码；未 clone、未编译 ZeroTier，网络受限，无法通过 GitHub API 固定 commit，因此这些引用是移动分支，必须在目标安装版本重测。

- [Protocol](https://docs.zerotier.com/protocol/)：VL1 roots 与 VL2 controller 职责不同，首次 rendezvous 可能经过 root；CGNAT 不保证可打洞。
- [Service 源码 _peerToJson / statusGet](https://github.com/zerotier/ZeroTierOne/blob/dev/service/OneService.cpp)：/status 提供 clock、tcpFallbackActive；/peer 提供 address、role、tunneled、paths；path 包括 preferred、active、expired、lastReceive。tunneled 是服务全局 TCP fallback 标志，不能称为精确逐玩家路径遥测。
- [CLI 源码 peers 分支](https://github.com/zerotier/ZeroTierOne/blob/dev/one.cpp)：官方可读输出基于 preferred 路径以及 tunneled 生成 DIRECT/RELAY，没有 preferred 时显示 RELAY。JSON 本身没有必须存在的 DIRECT 字符串。GameLink 添加 TTL、active 和最近接收校验以保守判断；缺字段 UNKNOWN。
- [Controller 文档](https://docs.zerotier.com/controller/)：GET /controller/network/{nwid}、成员列表/明细；POST /controller/network/{nwid}/member/{nodeId} 使用 authorized。删除成员不会阻止重新出现，MVP 不删成员/网络。
- [Service API](https://docs.zerotier.com/api/service/v1/)：本次页面抓取失败，接口交叉核对上面的官方源码；需对安装版本验证。
- [Relaying](https://docs.zerotier.com/faq/relaying/) 与 [Roots](https://docs.zerotier.com/roots/)：root/moon 可能转发，本项目不改变配置。

## 许可检查

[upstream README](https://github.com/zerotier/ZeroTierOne) 将 agent/service 与 nonfree 目录区分；[MPL](https://github.com/zerotier/ZeroTierOne/blob/dev/LICENSE-MPL.txt) 覆盖相应 agent 源码；[nonfree/LICENSE.md](https://github.com/zerotier/ZeroTierOne/blob/dev/nonfree/LICENSE.md) 对 Controller 等组件有独立条款。不能笼统宣称整包都是 MPL。0.4 及此前不打包 ZeroTier；0.5 核验实际 Windows 二进制后，默认改为一键官方下载，不把 MSI 放入分发包。VPS 具体版本、构建选项及许可未知，上线/商业分发前针对实际发行包核对。Qt 动态库分发也需要满足实际 Qt 许可，安装方案保留该责任。

## Windows 服务安装管理核对（2026-10-17）

- [官方下载页](https://www.zerotier.com/download/) 指向官方 Windows MSI；本轮实际下载并只读检查固定 [1.16.2 MSI](https://download.zerotier.com/RELEASES/1.16.2/dist/ZeroTier%20One.msi)，SHA-256 为 42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670。
- [upstream Windows MSI 工程](https://github.com/zerotier/ZeroTierOne/blob/dev/ext/installfiles/windows/ZeroTier%20One.aip) 当前声明 ProductName=ZeroTier One、Manufacturer=ZeroTier, Inc.、ProductVersion=1.16.2，并要求管理权限。移动分支引用仅作交叉核对，固定测试样本由上面的版本与哈希确定。
- Microsoft [MsiEnumProductsExW](https://learn.microsoft.com/en-us/windows/win32/api/msi/nf-msi-msienumproductsexw)：使用 MACHINE 上下文、NULL 用户 SID 枚举计算机级安装，产品通过同线程递增索引读取。卸载按产品 GUID 调用 Windows Installer，未执行注册表中的任意卸载命令。
- Microsoft [ShellExecuteExW](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecuteexw) 与 [WinVerifyTrust](https://learn.microsoft.com/en-us/windows/win32/api/wintrust/nf-wintrust-winverifytrust)：主 GUI 不提权，明确操作后由系统安装器请求 UAC；安装前验证签名与发布者，随后跟踪安装器进程结果。

Windows CI 已只读验证该官方 MSI 的正向识别；真实安装/驱动/UAC 流程尚未验收。这是 0.4 的验证范围；0.5 已新增一键官方下载与安装，见下文。

## Windows 实际二进制许可核对（2026-10-18）

- [官方下载页](https://www.zerotier.com/download/) 与 [changelog](https://docs.zerotier.com/changelog/) 的 Windows 稳定版为 1.16.2。固定发布路径和 SHA-256 见 deploy/zerotier-package.json。
- 最初依据 [1.16.2 Release Notes](https://github.com/zerotier/ZeroTierOne/blob/1.16.2/RELEASE-NOTES.md) 中默认构建不含 Controller 的说明，推断官方 MSI 为 MPL-only；**该推断不适用于实际 Windows MSI，已纠正**。
- [Windows Release x64 工程](https://github.com/zerotier/ZeroTierOne/blob/1.16.2/windows/ZeroTierOne/ZeroTierOne.vcxproj) 明确设置 ZT_NONFREE_CONTROLLER=1 / ZT_NONFREE=1。[one.cpp LICENSE_GRANT](https://github.com/zerotier/ZeroTierOne/blob/1.16.2/one.cpp) 据此选择 Source-Available 声明。
- 本机只读提取固定 SHA-256 官方 MSI 的内嵌 CAB，实际 zerotierone_x64.exe 包含“Licensed under a Source-Available License for Non-Commercial”声明。Windows 测试另读取客户端 -q -h 帮助核对。不能笼统宣称此官方二进制仅适用 MPL。
- [nonfree/LICENSE.md](https://github.com/zerotier/ZeroTierOne/blob/1.16.2/nonfree/LICENSE.md) 对产品整合/再分发与商业用途设有独立授权要求；用户尚未提供再分发授权。默认交付因此改为直接从官方源下载并一键安装，不在 EXE/ZIP 内再分发该 MSI。离线构建开关要求明确确认已有相应授权；使用原版客户端仍受其条款约束。
- MPL 覆盖对应 agent 源码部分；DesktopUI 与原始依赖也有各自许可。third_party/zerotier 保留 MPL、实际 Windows 二进制声明的 Source-Available 许可、原始源码地址和校验值，不声称官方背书。
- 使用 [Inno Setup PrepareToInstall](https://jrsoftware.org/ishelp/topic_scriptevents.htm) 在应用文件复制前完成先决组件；[CreateDownloadPage](https://jrsoftware.org/ishelp/topic_isxfunc_createdownloadpage.htm) 从官方 HTTPS 下载并验证固定 SHA-256。持有文件锁并再次校验后由系统 msiexec/runas 请求 UAC，独立 MSI 不加入 GameLink 卸载链。
- Windows Qt 6.8.3 动态库保留独立 DLL，可替换兼容版本；随包提供 LGPL/GPL 文本、源码地址和 Qt 原始依赖许可索引。
