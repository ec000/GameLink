# 本次测试报告

日期：2026-10-15（Asia/Shanghai）。执行主机 macOS arm64；Apple Clang 21.0.0、CMake 4.4.3、Go 1.26.3。空工作区起步，无本地 ZeroTier、Qt 6、Windows SDK 或远端访问资料。

## 已验证

- CMake configure/build（GAMELINK_BUILD_GUI=OFF）：成功；C++20 core 静态库与测试可执行文件已产生。
- CTest：1/1 passed。包含 offline、join pending、bounded NAT traversal、DIRECT、TCP fallback RELAY、无 preferred RELAY 推断、UNKNOWN schema/role/过期/clock、网络切换、strict unsupported 与 RTT/jitter/loss。
- Go `test -race -cover ./...`：通过；API、Controller fixture、文件锁测试。最终覆盖率见下面最终复验记录。
- Go `vet ./...`：通过。
- 本机 Go server 编译成功，真实进程 + loopback HTTP mock 冒烟通过：health、strict 422、创建、访问控制、邀请加入、成员读取、离开。
- Linux amd64 / arm64 服务交叉编译成功，未在 Linux 上运行。
- Python 2 项测试通过：调查白名单隐藏秘密字段，游戏 profile 维持 unverified/无预填已验证端口。
- 本地只读调查已执行，结果在 ENVIRONMENT_REPORT.md。

初次 Go 测试受 sandbox 的默认缓存目录和本机监听限制；缓存改到 /tmp，获准在本机临时端口运行后完整测试通过。没有跳过 HTTP 测试来报告成功。

## 未验证/未完成

- `cmake -S . -B build-gui -DGAMELINK_BUILD_GUI=ON` 在 find_package(Qt6) 失败：Qt6Config.cmake 不存在。这是最初缺少 SDK 的结果；后续 Qt 6 验证已完成，见下方记录。
- 无 Windows 实机、Qt 部署/安装器运行、ZeroTier join 实机、真实成员证书时序/撤销、UDP echo 实机或游戏路径验证。
- 六种 NAT 网络矩阵全部未测，不能保证中国大陆 CGNAT 直连成功率。
- WFP/受限 broker/严格直连逐包限制未实现；严格模式能力恒 false，不是已实现后“测试失败”。
- 自动游戏扫描、网卡优先级修改、LAN 广播/组播验证、游戏实测端口、Steam 自动会话识别、自启动未实现。
- VPS 只读评估未执行；角色/许可/服务端口均未知，未修改远端配置。

源代码与 mock 测试可运行不等于完整 Windows 产品或真 P2P 验收完成。

## 最终复验记录

最终 15 项 Go 测试通过，race detector 无报告；包覆盖率 API 79.7%、Controller 78.9%、main/文件锁 8.8%。main 启动与退出另由真实进程冒烟覆盖，该流程不计入 go test 的行覆盖率。
最终服务重新编译后，真实进程 HTTP 冒烟再次通过；go vet 无报告。Python 语法检查与 shell `sh -n` 通过。PowerShell、Inno Setup 与 Qt/Windows 资源编译未验证。

交付归档：build/GameLink-0.1.0-source.zip（源码、profiles、文档与测试，不含凭证或构建缓存）；build/SHA256SUMS 包含源码归档与三个服务端二进制的 SHA-256。

## Qt 6 后续验证

继续开发时安装 Homebrew qtbase 6.11.2。首次实际编译发现禁用 ASCII 隐式转换下的 QChar(char) 不合法，以及 QHash 使用不完整 QJsonObject 类型；已改为 QLatin1Char 并补充头文件。

macOS arm64 / Qt 6.11.2 全 GUI 已编译。新增 Qt Test 本机 HTTP fixtures，覆盖本地 token 与 GameLink token 隔离、新网络 join 的安全配置、非 HTTPS/非 loopback origin 拒绝、重定向不转发凭证、持续慢响应的总时限、异常/过大响应脱敏、GUI 创建房间/严格模式拒绝/join/DIRECT→RELAY→UNKNOWN/leave，以及 token 不写设置和日志。

新增 2 秒总请求 deadline，弥补 Qt transfer timeout 只限制无数据期间的空闲时间。增加 GUI 启动自检，检查 profiles、页面与 TLS 可用性。所有本机 Qt 测试通过；Windows 另由 `.github/workflows/build.yml` 编译和打包验证，首次运行结果待记录。

fixture 截图位于 build/qt6/qt-room-fixture.png，标题明确标为 MOCK，不代表真实 P2P 连接。真实 ZeroTier、游戏、NAT 场景及 Windows 交互式桌面仍未验收。
