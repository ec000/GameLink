# GameLink 部署只读评估

公有仓库版本：已隐去本机主机名，原始报告仅保留在本地 build 目录。

采集时间：2026-10-15T14:20:49.726741+00:00
系统：macOS-26.6.2-arm64-arm-64bit-Mach-O
ZeroTier 版本：UNKNOWN
Controller API：UNKNOWN
TCP fallback 当前活动：UNKNOWN

未执行任何网络配置变更；无法读取的检查是 UNKNOWN，不能推断不存在。
planet 文件存在无法判断是否自定义；需要管理员比对已安装发行版 hash。
moons.d 与 /moon 只描述本机，不代表所有玩家的 roots。
端口、转发、NAT、网桥与权限失败详情见下面 JSON；上线前人工审核。
没有 SSH 信息，未连接任何远端 VPS；本地检测结果不能代替 VPS 评估。

```json
{
  "generatedAt": "2026-10-15T14:20:49.726741+00:00",
  "system": "macOS-26.6.2-arm64-arm-64bit-Mach-O",
  "mutations": [],
  "zerotier": {
    "info": {
      "status": "unavailable"
    },
    "listnetworks": {
      "status": "unavailable"
    },
    "peers": {
      "status": "unavailable"
    },
    "homeExists": false,
    "planet": {
      "exists": false
    },
    "moons.d": {
      "exists": false
    },
    "controller.d": {
      "exists": false
    },
    "local.conf": {
      "exists": false
    },
    "localAPI": {
      "status": "unavailable_token_or_port"
    }
  },
  "checks": {
    "uname": {
      "status": "ok",
      "exit": 0,
      "stdout": "Darwin <redacted-host> 25.6.0 Darwin Kernel Version 25.6.0: Fri Jul 31 19:11:03 PDT 2026; root:xnu-12377.161.14~5/RELEASE_ARM64_T8132 arm64\n",
      "stderr": ""
    }
  }
}
```
