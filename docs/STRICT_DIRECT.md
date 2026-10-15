# Direct Required 技术边界

MVP 不支持严格直连：服务拒绝 policy=direct_required（422），GUI 显示 unsupported。没有 WFP/broker 的标准用户 GUI 不能阻断另行启动的游戏，按钮禁用不是流量阻断。

Windows 防火墙的 block 优先级会使简单“block 全部 + allow direct peers”设计失效。WFP 需要按 ALE_APP_ID、接口 LUID、远端虚拟 IP、协议/端口及入站/出站层精确定义过滤，并处理既有 flow、广播/组播、UDP fast path、进程重用、接口重建。只拦截游戏进程的内层包也无法识别外层 ZeroTier 此刻选择的 relay。

轮询 /peer 存在检测间隙：检查 direct 后发包前可能切换 relay；WFP 动态 allow 仍有 TOCTOU。因此没有基于内核/ZeroTier 路径的逐包约束时，不能承诺“绝不经过任何 relay”。关闭 TCP fallback 也不等于关闭 UDP root 转发；不修改用户 global local.conf。

后续验收：预置 fail-closed、watchdog 失联撤销、过期撤销、网络切换撤销、崩溃及重启恢复、已经建立的 TCP/UDP/广播流、游戏外浏览器/Steam 下载不受影响；双端和 root 抓包验证。未找到可靠逐包约束前，继续保持 unsupported，不能提供静默降级开关。

依据：[Microsoft Filter Arbitration](https://learn.microsoft.com/en-us/windows/win32/fwp/filter-arbitration)、[Filtering conditions](https://learn.microsoft.com/en-us/windows/win32/fwp/filtering-condition-identifiers-)。
