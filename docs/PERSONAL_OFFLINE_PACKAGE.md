# 个人非商业自用离线包

用户已确认：仅供个人非商业自用，不公开分发。本模式不宣称取得商业、OEM 或公开再分发授权，原版 ZeroTier Source-Available 条款仍适用。公开发布的默认 EXE/ZIP 保持一键在线安装模式。

## 内容与安装

个人包在 GameLink EXE 安装器内嵌原版官方 ZeroTier One 1.16.2 MSI；不修改 MSI、服务、驱动或桌面 UI。嵌入前验证 SHA-256、签名发布者及 MSI 产品属性，运行时提取并再次验证 SHA-256。相同或更高的标准版本跳过，不强行启动已有停止服务；按需通过 Windows 请求管理员授权，禁止自动重启。卸载 GameLink 保留独立 ZeroTier 和身份；普通个人包不改 planet/moon，CustomRoots 定制版通过额外任务配置它们。

内嵌 MSI SHA-256：`42514072b0fe44b8f66e0395bcd23a0b1d1642c28ed00831f1527b2f41b14670`。

客户端复用 deploy/personal-offline-base.json 中固定版本、来源运行编号及 SHA-256 的已验证 Windows Qt ZIP。当前基线为 0.6.0 / Qt 6.8.3，应用提交 732fd835d467b02a749ec69d3b53938bbeb24f39，包含独立 ZeroTier 状态页和加入/退网功能。个人打包仅复用该已验证应用，不重新混入其他客户端代码。

个人包文件名为 `GameLink-0.6.0-Offline-Personal-Setup.exe`；带自建根节点的定制版为 `GameLink-0.6.0-CustomRoots-Offline-Personal-Setup.exe`，额外包含用户提供的 planet/moon，并通过独立安装任务校验、备份后部署，详见 [自建根节点说明](CUSTOM_ROOTS.md)。Windows 程序与安装器尚未签名。

## 私有产物传输

仓库是公有的，因此仅手动触发 personal-offline.yml，不上传原始 EXE、MSI、ZIP 或 stage。工作流验证用户声明和一次性私密密钥，完成真实安装生命周期检查后，使用 AES-256-GCM 加密 EXE，仅上传密文（最长保留一天）。上传前用 Node 验证跨运行时解密和原始 SHA-256 一致。

本机随机生成 32 字节密钥，存入权限 0600、被 Git 忽略的 build/private-offline/transport.secret；GitHub Actions Secret 仅用于本次构建。成功下载、认证解密和核对文件后，删除临时远端 Secret 和密文 artifact，并清理本机临时密钥。不要把密钥放入日志、命令参数值、源码归档或产物中。

密文格式：8 字节 ASCII GLPKG001（同时作为 AAD），12 字节 nonce，16 字节认证 tag，后接密文。scripts/decrypt-personal-package.mjs 在认证通过之前不写任何明文，并拒绝覆盖既有文件。

## 构建入口

个人模式使用 scripts/package-personal-offline.ps1，要求显式 PersonalUseOnly；这是用户用途声明，不代表获得公开再分发权。商业/公开离线构建仍需现有构建入口的相应授权确认。原版许可、原始来源和源码链接均随个人包提供。

最终构建验证与交付校验值见 TEST_REPORT.md。
