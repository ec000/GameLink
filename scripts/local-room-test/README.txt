GameLink Windows 本机房间测试

本包只验证创建、加入、成员列表和离开房间，不连接真实 Controller，不授权真实 ZeroTier 节点，也不分配游戏可用的虚拟 IP。无需安装 Python/Go、管理员权限或修改防火墙。

1. 将 ZIP 完整解压到一个文件夹，双击 Start local room test.cmd。
2. 保持控制台打开。程序会生成本机专用的随机测试凭证，并将 Alice 的凭证复制到剪贴板，屏幕上不会打印 Token。
3. 打开已安装的 GameLink 0.6.4，在 设置 → 房间服务 填写：
   服务地址：http://127.0.0.1:8080
   访问凭证：直接粘贴剪贴板里的 Alice 测试凭证。
4. 在 我的游戏 确认游戏已选中，策略使用优先直连；回到联机房间点击创建，应显示创建成功。再点击离开，可验证退出流程。

如需测试另一个成员：
5. 再打开一个 GameLink 窗口。先将第一个窗口的完整房间码复制到第二个窗口的房间码输入框。
6. 双击 Copy Bob credential.cmd，在第二个窗口的房间服务设置中粘贴 Bob 凭证，地址同上；返回加入房间，可看到两个测试成员。
7. 两个窗口的凭证不同；需要恢复 Alice 凭证时双击 Copy Alice credential.cmd。重启 GameLink 后需重新粘贴凭证。

测试期间不要点击“连接虚拟网络”或“加入网络”：本包的 Node ID 和 Network ID 是模拟数据，不能用于真实游戏联机。

停止：在服务控制台按 Ctrl+C，然后关闭窗口。再次启动会复用同一组本机测试凭证。8080 端口被其他程序占用时会拒绝启动，不会结束已有进程。
数据位置：%LOCALAPPDATA%\GameLink\LocalRoomTest，仅当前用户和 SYSTEM 可访问。真实 ZeroTier 数据、Moon/Planet 和其他 GameLink 配置均不修改。
这是独立测试服务器，不是 Windows 生产房间服务。真实联机后续仍需在 Linux 部署 GameLink Server，并配置实际用户/Node ID、专用网络池与 HTTPS。
