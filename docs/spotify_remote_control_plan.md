# ESP32 Turntable — Spotify 远程遥控方案

> 目标：通过 ESP32-C3 物理按键 + OLED 显示屏，远程控制 iPhone 上 Spotify App 的音乐播放（基础控制 + 播放指定内容 + 状态显示）。
>
> 本文档长期跟踪实施进度，每个任务完成后勾选 `[x]`。

---

## 一、可行性结论

**可以实现。** 依托 Spotify Web API 的 Player 端点 + Spotify Connect 机制：

- iPhone 上的 Spotify App 登录后会作为 Spotify Connect 设备出现在账户设备列表中（联网在线即可，App 在后台也能被发现）。
- ESP32 通过 `GET /me/player/devices` 找到 iPhone 的 `device_id`。
- 调用 `PUT /me/player`（Transfer Playback）将播放目标转移到 iPhone。
- 之后即可用全部 Player 控制端点操作 iPhone 播放。

**前提条件**：
- [x] Spotify Premium 账户（Player 端点硬性要求）— **已确认**
- [ ] iPhone Spotify App 保持联网在线（不要被手动强制退出）

---

## 二、Spotify Web API 能力概览

| 分类 | 主要功能 |
|------|---------|
| Player（播放控制） | 播放状态、设备列表、转移播放、播放/暂停、上下首、定位、音量、循环、随机、队列 |
| Search | 搜索曲目/专辑/艺术家/歌单 |
| Library | 管理我的音乐库 |
| Playlists | 创建/修改/查询歌单 |
| Albums/Artists/Tracks | 元数据、推荐、音频特征 |
| Users | 用户画像、关注、Top 内容 |
| Audiobooks/Shows/Episodes | 播客与有声书 |

### 本项目使用的端点与 Scope

| 操作 | 端点 | Scope |
|------|------|-------|
| 列出可用设备（找 iPhone） | `GET /me/player/devices` | `user-read-playback-state` |
| 转移播放到 iPhone | `PUT /me/player` | `user-modify-playback-state` |
| 播放/恢复（可指定 context_uri） | `PUT /me/player/play` | `user-modify-playback-state` |
| 暂停 | `PUT /me/player/pause` | `user-modify-playback-state` |
| 下一首 | `POST /me/player/next` | `user-modify-playback-state` |
| 上一首 | `POST /me/player/previous` | `user-modify-playback-state` |
| 定位 | `PUT /me/player/seek` | `user-modify-playback-state` |
| 音量 | `PUT /me/player/volume` | `user-modify-playback-state` |
| 循环模式 | `PUT /me/player/repeat` | `user-modify-playback-state` |
| 随机开关 | `PUT /me/player/shuffle` | `user-modify-playback-state` |
| 加入队列 | `POST /me/player/queue` | `user-modify-playback-state` |
| 读取当前播放/队列 | `GET /me/player`、`GET /me/player/queue` | `user-read-playback-state` / `user-read-currently-playing` |
| 读取歌单（用于预设播放） | `GET /me/playlists` | `playlist-read-private` |

**所需 Scope 汇总**：
```
user-read-playback-state
user-modify-playback-state
user-read-currently-playing
playlist-read-private
```

---

## 三、系统架构

```
[按键] ┐                                        ┌─> iPhone Spotify App
       ├─> ESP32-C3 ──HTTPS──> api.spotify.com ─┤   (Spotify Connect 设备)
[OLED] ┘        │                               └─> 实际音频输出
                │
           accounts.spotify.com
           (token 刷新)
```

### 任务编排（app_main）
```
1. NVS init
2. WiFi 连接
3. SNTP 时间同步（证书校验依赖）
4. spotify_auth 初始化（读 refresh_token）
5. 创建 task:
   - input_task:   读按键 → 事件 queue
   - control_task: 消费事件 → 调 spotify_client（必要时先找设备）
   - state_task:   每 3-5s 拉 playback state → 推给 ui_task
   - ui_task:      渲染 OLED
```

---

## 四、目录结构

```
components/                        # (未使用，采用 PlatformIO lib/ 约定)
lib/
  nvs_manager/      # 通用 NVS 读写封装（namespace: app_config）
  wifi/             # WiFi 连接管理（STA/AP 模式 + NVS 凭证）
  http_server/      # HTTP 配网页面（WiFi + Spotify 凭证，SPIFFS 静态文件）
  sntp_sync/        # SNTP 时间同步（HTTPS 证书校验依赖）
  spotify_auth/     # OAuth token 管理（NVS 存储 + 自动刷新）
  spotify_client/   # Player 端点 HTTPS 封装（esp_http_client + crt_bundle）
  spotify_device/   # 设备发现（搜索 Smartphone 类型，缓存 device_id）
src/
  main.cpp           # 启动流程 + 串口交互命令
  Kconfig            # menuconfig 配置（AP SSID/密码/固件版本）
tools/
  get_refresh_token.py   # 电脑辅助 PKCE 授权脚本
  verify_playback.py     # 交互式播放控制验证脚本
data/
  www/                  # SPIFFS 网页资源（index/status/success/reset.html + styles.css）
partitions.csv          # nvs(24KB) + phy(4KB) + factory(2MB) + storage(512KB)
sdkconfig.defaults      # WiFi/SPIFFS/SNTP/mbedTLS 配置
build_spiffs.py         # PlatformIO 预构建脚本（打包 data/www/ → storage.bin）
```

---

## 五、实施进度

### 阶段 0：Spotify 开发者准备（手动）
- [x] 在 https://developer.spotify.com/dashboard 创建 App，勾选 Web API
- [x] 记录 Client ID： 229aabd5c2a244d0be3df795c36ca8fd
- [x] 配置 Redirect URI：`http://127.0.0.1:8888/callback`
- [x] 确认所需 scopes 已在 App 设置中启用
- [x] **注意**：2026 年 2 月起 Dev Mode 应用 refresh_token 7 天过期；长期使用需申请 Extended Quota 或上架 App。实施时再确认当前策略

### 阶段 1：项目骨架与依赖
- [x] 在 `lib/` 下创建各组件目录（PlatformIO 约定，`src/CMakeLists.txt` GLOB_RECURSE）
- [x] 配置 SNTP 同步（`lib/sntp_sync/`，`esp_sntp` + pool.ntp.org）
- [x] WiFi 连接逻辑（`lib/wifi/` + `lib/nvs_manager/`，AP 配网 + NVS 存储，参考 esp32-rss-display）
- [x] HTTP 配网页面（`lib/http_server/` + `data/www/`，WiFi + Spotify 凭证配置）
- [ ] OLED 驱动集成（确认型号：SSD1306 / SH1106 / 其他，I2C 地址与引脚）
- [ ] 按键 GPIO 引脚确认与驱动框架

### 阶段 2：`spotify_auth` 组件（核心）
- [x] NVS 读写 `refresh_token` / `client_id`（NVS key: `sp_client_id` / `sp_refresh_tok`，≤15 字符限制）
- [x] `spotify_auth_refresh()`：`POST https://accounts.spotify.com/api/token`，解析 access_token + expires_in
- [x] `spotify_auth_get_token()`：过期自动刷新，返回有效 token
- [x] 启动时 SNTP 同步完成后再初始化 auth
- [x] 单元验证：串口打印 `Token refreshed: len=264, expires_in=3600s`

### 阶段 3：`spotify_client` 组件
- [x] 统一 HTTPS 客户端封装（`esp_http_client` + `esp_crt_bundle_attach` 证书 bundle）
- [x] 统一鉴权头 `Authorization: Bearer <token>`
- [ ] 429 限流处理（读 `Retry-After`，延迟重试）— 暂未实现
- [x] `spotify_client_get_devices(buf, size, &status)`
- [x] `spotify_client_transfer_playback(device_id, play, &status)`
- [x] `spotify_client_play(device_id, context_uri, &status)` / `spotify_client_play_track(device_id, track_uri, &status)`
- [x] `spotify_client_pause(device_id, &status)`
- [x] `spotify_client_next(device_id, &status)` / `spotify_client_previous(device_id, &status)`
- [x] `spotify_client_get_state(buf, size, &status)`
- [x] 单元验证：串口打印设备列表与当前播放状态

### 阶段 4：`spotify_device` 组件
- [x] `spotify_device_find()`：搜索 `"Smartphone"` 并向前提取 `"id"`（兼容 JSON 空格）
- [x] device_id 缓存 + `spotify_device_invalidate()` 失效重查
- [ ] `is_restricted==true` 兜底处理 — 暂未实现
- [x] 控制前自动发现设备（`spotify_device_get_id()` 自动调用 find）

### 阶段 5：`ui` + `input` 组件
- [ ] **input**：GPIO + 消抖 + 事件 queue
- [ ] **input**：按键映射
  - 短按 KEY1 → 播放/暂停
  - 短按 KEY2 → 下一首
  - 短按 KEY3 → 上一首
  - 长按 KEY1 → 切换预设歌单（循环列表）
  - ~~KEY4/旋钮 → 音量 +/-~~ **不可用**：iPhone App `supports_volume=false`，API 无法控制音量（见第十节）
- [ ] **ui**：OLED 渲染当前曲目
  ```
  ♪ Bohemian Rhapsody
    Queen
    [====>    ] 2:31/5:55
    iPhone  (音量由设备端控制)
  ```
- [ ] **ui**：设备离线/错误状态提示

### 阶段 6：`main.cpp` 任务编排
- [x] 启动序列：NVS → WiFi → SNTP → auth init
- [x] 串口交互命令（`d/s/f/t/p/SPC/n/b/h`，非阻塞 `getchar` + `vTaskDelay`）
- [ ] 创建 input_task / control_task / state_task / ui_task（待阶段 5 硬件就绪）
- [ ] 事件路由：input 事件 → control 动作
- [ ] state_task 定时拉状态推 UI

### 阶段 7：电脑辅助授权脚本
- [x] `tools/get_refresh_token.py`
  - 本地起 `http.server` 监听 8888
  - 生成 `code_verifier` / `code_challenge`（PKCE）
  - 打开浏览器到 `https://accounts.spotify.com/authorize?...`
  - 回调拿 code → `POST /api/token` 换 token
  - 打印 refresh_token 供写入 ESP32
- [x] `tools/verify_playback.py`（交互式验证脚本）
- [x] ESP32 Web 配置页面写入 NVS（`POST /api/spotify` → NVS → 重启）

### 阶段 8：增量联调（建议顺序）
- [x] 8.1 用 Python + refresh_token 验证 API 能控制 iPhone（脱离 ESP32）— **见第十节验证结果**
- [x] 8.2 ESP32 串口打印 `get_devices` 结果 — **HTTP 200，iPhone 可见**
- [x] 8.3 ESP32 `transfer_playback` + `play/pause`，串口命令触发验证 — **全部 HTTP 200/204**
- [ ] 8.4 OLED 状态显示
- [ ] 8.5 预设歌单播放（长按切换）
- [ ] 8.6 长时间稳定性测试（token 自动刷新、设备掉线恢复）

---

## 六、关键技术要点

### OAuth 授权（电脑辅助方案）
1. 电脑跑 `get_refresh_token.py`，浏览器登录 Spotify 授权。
2. 脚本拿到 `refresh_token`（长期有效）。
3. 写入 ESP32 NVS 或 menuconfig。
4. ESP32 运行时用 refresh_token 每小时刷新 access_token。
5. access_token 1 小时过期，refresh_token 长期有效（受 Dev Mode 策略限制）。

### HTTPS 与证书
- 所有请求必须 HTTPS。
- 固定 DigiCert Global Root CA（Spotify 用）。
- 必须先 SNTP 同步系统时间，否则证书时间校验失败。
- 开发期可临时跳过校验（`skip_cert_common_name_check`），生产环境必须启用。

### device_id 稳定性
- 文档明确：device_id "unique and persistent to some extent, but not guaranteed"。
- 不长期缓存，每次启动或失效即重新拉取设备列表。

### 限流
- 状态轮询间隔 3-5s，不要太频繁。
- 收到 429 时读 `Retry-After` header 延迟重试。

### iPhone App 后台保活
- iOS 后台音频有系统限制，App 被彻底杀掉后会从设备列表消失。
- 保持 App 在后台运行（不手动强制退出）即可被 API 唤醒控制。
- UI 需提示"设备离线"状态。

### 实现中发现的 bug 与修复（2026-08-15）
1. **NVS key 长度限制**：`"sp_refresh_token"`（16 字符）超过 NVS 15 字符上限，写入静默失败。改为 `"sp_refresh_tok"`（14 字符）。
2. **JSON 空格兼容**：Spotify API 返回的 JSON 在冒号两侧有空格（`"type" : "Smartphone"`），设备发现搜索 `"type":"Smartphone"` 匹配不到。改为直接搜索 `"Smartphone"` 并向前提取 `"id"` 值，跳过空格/冒号。
3. **看门狗超时**：`getchar()` 阻塞导致 IDLE 任务饿死。用 `fcntl(O_NONBLOCK)` 设非阻塞 + `vTaskDelay(50ms)` 让出 CPU。
4. **esp_http_client content_type**：ESP-IDF 6.0 的 `esp_http_client_config_t` 无 `content_type` 字段，需在 init 后用 `esp_http_client_set_header()` 设置。

---

## 七、风险与对策

| 风险 | 对策 |
|------|------|
| **iPhone 不支持 API 音量控制**（`supports_volume=false`，403 `VOLUME_CONTROL_DISALLOW`） | 音量由 iPhone 物理按键 / 系统音量控制；ESP32 不做音量功能 |
| iPhone App 被杀进程后从设备列表消失 | 控制前先 `get_devices` 校验；UI 提示设备离线 |
| Dev Mode refresh_token 7 天过期 | 长期用需申请 Extended Quota / 上架 App；或定期重跑授权脚本 |
| `device_id` 不稳定 | 不长期缓存，每次启动/失效即重查 |
| HTTPS 证书校验失败 | 固定根证书 + SNTP 时间同步；开发期可临时跳过 |
| 429 限流 | 状态轮询 3-5s，遵守 Retry-After |
| token 刷新失败（网络/服务异常） | 重试 + 指数退避；UI 提示连接异常 |

---

## 八、待确认信息

- [ ] OLED 具体型号（SSD1306 / SH1106 / 其他）与 I2C 引脚/地址
- [ ] 按键 GPIO 引脚分配
- [x] ~~WiFi 连接是否已有现成代码~~ — 已实现：AP 配网 + NVS 存储（参考 esp32-rss-display）
- [x] 预设歌单列表（URI 或名称）— 已在第十节列出
- [x] ~~是否需要旋钮（旋转编码器）用于音量~~ — 不需要，iPhone 不支持 API 音量控制

---

## 九、参考文档

- Web API 总览：https://developer.spotify.com/documentation/web-api
- Player 端点参考：https://developer.spotify.com/documentation/web-api/reference/#player
- Scopes 说明：https://developer.spotify.com/documentation/web-api/concepts/scopes
- Authorization Code + PKCE：https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow
- Refreshing tokens：https://developer.spotify.com/documentation/web-api/tutorials/refreshing-tokens
- 2026 年 2 月 Dev Mode 变更迁移：https://developer.spotify.com/documentation/web-api/tutorials/february-2026-migration-guide

---

## 十、API 验证结果（2026-08-15）

使用 `tools/get_refresh_token.py` + `tools/verify_playback.py` 完成脱离 ESP32 的 API 验证。

### 环境
- Client ID：`229aabd5c2a244d0be3df795c36ca8fd`
- Redirect URI：`http://127.0.0.1:8888/callback`
- 授权流程：PKCE（Authorization Code + S256）
- Token 文件：`tools/.spotify_token.json`、`tools/.spotify_client.json`（已 gitignore）

### 验证明细

| # | 操作 | 端点 | HTTP | 结果 |
|---|------|------|------|------|
| 1 | 列出设备 | `GET /me/player/devices` | 200 | ✅ iPhone (Smartphone) 可见 |
| 2 | 转移播放到 iPhone | `PUT /me/player` | 204 | ✅ 成功 |
| 3 | 播放指定歌单 | `PUT /me/player/play` (context_uri) | 204 | ✅ 成功播放 Calypso 歌单 |
| 4 | 查看播放状态 | `GET /me/player` | 200 | ✅ 返回曲目/艺术家/专辑/进度/设备 |
| 5 | 下一首 | `POST /me/player/next` | 200 | ✅ 切到下一曲 |
| 6 | 暂停 | `PUT /me/player/pause` | 200 | ✅ 暂停成功 |
| 7 | 设置音量 | `PUT /me/player/volume` | **403** | ❌ `VOLUME_CONTROL_DISALLOW` |

### 关键发现：iPhone 不支持远程音量控制

iPhone Spotify App 的设备属性返回 `supports_volume: false`，调用 `PUT /me/player/volume` 返回 403：

```json
{
  "error": {
    "status": 403,
    "message": "Player command failed: Cannot control device volume",
    "reason": "VOLUME_CONTROL_DISALLOW"
  }
}
```

**结论**：ESP32 遥控器**无法通过 API 控制 iPhone 播放音量**。音量只能由 iPhone 物理按键或 iOS 系统音量调节。方案中已移除音量控制功能。

### 设备发现要点
- iPhone Spotify App 需在前台打开（或后台保活）才会出现在设备列表。
- 测试时 App 未打开则设备列表为空；打开后约 50s 内出现。
- 设备属性：`is_restricted=false`（可控）、`is_active=true`（转移后）。

### 已验证可用的歌单 URI（供 ESP32 预设）
| 名称 | URI |
|------|-----|
| KTV | `spotify:playlist:5GoVISbddYB77NjjJNNy8z` |
| babysleep | `spotify:playlist:4IJVe4fmIxNEbwQssfSrEH` |
| Calypso | `spotify:playlist:1hlxc7aK2GwPFJ3HUVnESB` |
| Arabica | `spotify:playlist:21pjxqSjQ8U8Fwois3OiEP` |
| Standard JAZZ | `spotify:playlist:3J2thyZnVmHGN1RklSX0pX` |
| house & disco | `spotify:playlist:48oZ7053Am7oXr7ugfENrs` |
| Latin | `spotify:playlist:6UNkFDYZGAibJDG3BpxFiZ` |

---

## 修订记录

| 日期 | 内容 |
|------|------|
| 2026-08-15 | 初版方案，确认 Premium + 基础控制 + 播放指定内容 + 状态显示 + 电脑辅助授权 + 按键+OLED |
| 2026-08-15 | 完成 API 验证：转移/播放/暂停/上下首/状态均通过；发现 iPhone 不支持 API 音量控制（supports_volume=false），方案移除音量功能 |
| 2026-08-15 | 完成 ESP32 固件阶段 1-4/6/7/8.1-8.3：WiFi 配网(NVS+AP)、SNTP 同步、Spotify auth(client_id+refresh_token→NVS)、HTTPS Player 端点封装、设备发现、串口交互命令。全链路验证通过（发现 iPhone→转移→播放→暂停） |
