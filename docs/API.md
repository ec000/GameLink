# API v1

所有请求必须 `Authorization: Bearer <个人凭证>`；HTTPS 部署，仅 localhost 开发允许 HTTP。
JSON body ≤ 8 KiB，拒绝未知字段及尾随 JSON；单用户 120 请求/分钟，单源 IP 300 请求/分钟。

| 方法与路径 | 请求 | 返回 |
|---|---|---|
| GET /api/v1/health | 无 | 200 状态、mode、strictDirectSupported=false |
| POST /api/v1/rooms | {"gameId":"warcraft3","policy":"preferred_direct"} | 201 房间，含 joinKey，仅房主创建时返回 |
| POST /api/v1/rooms/{id}/join | {"joinKey":"…"} | 200 房间 |
| POST /api/v1/rooms/{id}/leave | {} | 204；房主离开关闭全房间 |
| GET /api/v1/rooms/{id} | 无 | 200 房间，不含邀请密钥 |
| GET /api/v1/rooms/{id}/peers | 无 | 200 {"peers":[…]} |

房间字段：id、networkId、gameId、policy、owner、expiresAt、closing、members（userId、nodeId、authorized）。
peers 增加 virtualIps，由 Controller 读取；没有 DIRECT 字段，因为 VPS 不知道两客户端之间的路径。
GUI 对每个 Node ID 查询本地 /peer 做状态判定，不能把 Controller lastSeen 当 direct。

错误：400 输入不合法；401 凭证错误；403 邀请错误/不是成员；404 房间不存在；409 已在房间/房间清理中；422 strict 不支持；429 限流；503 Controller/存储不可用/池耗尽。错误 body 为 {"error":"稳定错误码"}，不包含上游响应或令牌。
同一身份最多一个房间；join 已是成员时幂等；leave 不是成员返回 403。并发房间操作由进程内互斥锁串行化，单实例运行。TTL 默认一小时，清理每 5 秒；Controller 不可达时隔离并重试，TTL 不构成即时数据面断开。
配置 tokenHash 是个人高熵 token 的 SHA-256 十六进制，服务器不保存明文；并非用户密码哈希方案。客户端 GameLink token 和本地 ZT token 是两个不同的凭证。
