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
