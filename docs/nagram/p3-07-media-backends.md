# P3-07 外部媒体后端与平台能力

本文件是 [功能需求](requirements.md) 第 6 节 P3-07 的专项设计，覆盖功能族 [F06](requirements.md#f06) 与 [F16](requirements.md#f16) 中的外部贴纸缓存、音乐封面、录制与通话音频处理、解码器、网页应用兼容身份和地图预览。架构约束见 [设计与路线](design.md) 第 3 节，提交规则见 [分步实施计划](implementation-plan.md) 第 2、3、5 节。

文中的上游位置均为 2026-10-01 在当前工作副本中实际读到的代码，路径相对 `Telegram/SourceFiles/`，另有说明的除外。实现时以当时的上游代码为准重新确认。

## 1. 范围与不做的事

### 范围

| 编号 | 来源名称 | 功能族 |
| --- | --- | --- |
| A080–A082 | 外部贴纸缓存、自动同步、目录命名方式 | F06 |
| A044 | 自定义音乐封面 API | F06 |
| D115 | 音乐封面自适应颜色 | F06 |
| N121 | 音频码率（语音消息录制） | F06 |
| N122 | 关闭群语音处理 | F06 |
| A017 | 噪音抑制和语音增强 | F06 |
| A146 | 播放器解码器 | F06 |
| D047 | 网页应用兼容身份 | F16 |
| N019 | 地图预览服务 | F16 |

### 不做的事

- 不修改子模块。`Telegram/lib_webview`、`Telegram/lib_webrtc`、`Telegram/ThirdParty/tgcalls` 都是上游独立仓库的子模块（见 `.gitmodules`），需要改它们才能实现的能力一律写成“条件不满足”。
- 不迁移 Android 的 URI 授权、ExoPlayer 解码器列表、Android 系统音效（`NoiseSuppressor` 等）和地图 SDK。
- 不内置任何第三方服务的地址或密钥。封面与地图的外部地址只由用户填写，默认空，默认不发起任何外部请求。
- 不转码贴纸。导出的是 Telegram 下发的原始文件（`.webp`、`.tgs`、`.webm`）。
- 不改圆形视频录制的码率（`ui/controls/round_video_recorder.cpp` 的 `kAudioBitRate`、`kVideoBitRate`）；N121 只针对语音消息。
- 不改私聊通话的音频处理（原因见 2.6）。
- 外部请求失败后不自动改用其他服务。
- F16 中未归包的三项（禁用官方网页自动登录、点击标签的默认搜索页面、扩大网页应用宽高）不在本包。

## 2. 逐项结论

| 条目 | 结论 | 一句话原因 |
| --- | --- | --- |
| N121 | 可实现 | 桌面端语音录制码率是一个写死的常量，可按选项替换 |
| N122 | 可实现 | tgcalls 的群通话描述符已有 `disableOutgoingAudioProcessing` 字段，桌面端未设置 |
| A044 | 可实现（桌面自定义协议） | 上游已有按 URL 加载图片的 `PlainUrlLocation`，封面位置在一处生成 |
| A080–A082 | 可实现 | 贴纸集数据、文件下载、目录选择器都有现成上游接口，上游只需一行挂钩 |
| D047 | 部分可实现 | 传给服务端的平台标识可改；浏览器 User-Agent 无法改 |
| N019 | 内置服务商条件不满足；自定义地址模板可实现，待维护者决定 | 桌面端地图预览由 Telegram 服务端代取，仓库内没有任何第三方静态地图接入 |
| A017 | 条件不满足 | 群通话上游已有开关；私聊通话与语音录制没有可用的处理链 |
| A146 | 条件不满足 | 桌面端只有 FFmpeg 一套解码器，硬件加速开关上游已有 |
| D115 | 条件不满足 | 桌面端播放器界面不显示封面，没有可着色的界面 |

### 2.1 N121 语音消息码率

**现状**：`media/audio/media_audio_capture.cpp` 的 `Instance::Inner::initializeFFmpeg()` 选出 `opus` 容器后，以 `d->codecContext->bit_rate = 32000` 固定码率，单声道，采样率 `kCaptureFrequency`（48 kHz）。录制由 `Instance::start()` 在主线程发起，通过 `InvokeQueued` 转到采集线程执行 `Instance::Inner::start()`。

**挂钩**：

- `Instance::start()`：在主线程读取 `Nagram::Media::VoiceRecordBitrate(32000)`，随已有参数一起传入采集线程。
- `Instance::Inner::start()`：增加一个 `int bitrate` 参数并保存。
- `Instance::Inner::initializeFFmpeg()`：`bit_rate` 改用保存的值。

`Inner` 是该 `.cpp` 内部的类，改动约 6 行，超过单行挂钩，在提交正文说明原因：注册表只能在主线程读取，不能在采集线程里调用 `ForDevice()`。

**取值**：0 表示跟随 Telegram；其余只允许 16、24、48、64、96、128（kbps）。不提供自由输入，避免把编码器不接受的值传进 `avcodec_open2`。

**与上游一致**：选项为 0 时 `VoiceRecordBitrate(32000)` 原样返回传入的 32000。

**限度**：只影响之后开始的录制；波形、时长和发送流程不变。码率越高文件越大，说明文字写明。

### 2.2 N122 关闭群通话音频处理

**现状**：`calls/group/calls_group_call.cpp` 的 `GroupCall::tryCreateController()` 构造 `tgcalls::GroupInstanceDescriptor` 时没有设置 `disableOutgoingAudioProcessing`（`ThirdParty/tgcalls/tgcalls/group/GroupInstanceImpl.h`，默认 `false`）。该字段在 `GroupInstanceCustomImpl.cpp` 的 `createOutgoingAudioChannel()` 中决定是否关闭回声消除、噪声抑制、自动增益和高通滤波。

**挂钩**：在该描述符的 `.requestVideoBroadcastPart` 与 `.videoContentType` 之间加一行 `.disableOutgoingAudioProcessing = Nagram::Media::GroupCallRawAudio()`。位置必须符合结构体的声明顺序。

**与上游一致**：选项关闭时返回 `false`，等于字段默认值。

**限度**：

- 在创建通话控制器时读取，对已加入的通话不生效，说明文字写“下次加入通话时生效”。
- 关闭回声消除后，用扬声器外放会产生回声，说明文字提示配合耳机使用。
- 上游群通话设置里的“噪声抑制”（`calls/group/calls_group_settings.cpp`，`Core::Settings::groupCallNoiseSuppression`）走 tgcalls 的另一条后处理路径（`initialEnableNoiseSuppression`、`setIsNoiseSuppressionEnabled`），本选项不改它。两者同时开启时后处理是否仍执行，须在现场验证后写进说明。

### 2.3 A044 自定义音乐封面

**现状**：`data/data_document.cpp` 的 `DocumentData::refreshPossibleCoverThumbnail()` 在歌曲没有自带缩略图、且表演者与标题都不为空时，把缩略图位置设为 `AudioAlbumThumbLocation{ id }`。该位置在 `storage/download_manager_mtproto.cpp` 中通过 `upload.getWebFile` + `inputWebFileAudioAlbumThumbLocation` 向 Telegram 服务端取图，也就是桌面端的封面已经由 Telegram 代取。加载失败时 `DocumentData::hasThumbnail()` 返回假，界面画默认图标，播放不受影响。

上游另有 `PlainUrlLocation`（`ui/image/image_location.h`），在 `storage/file_download.cpp` 中由 `webFileLoader` 按 URL 下载，经过上游的重定向限制与缓存。

**挂钩**：`refreshPossibleCoverThumbnail()` 中的 `{ AudioAlbumThumbLocation{ id } }` 改为 `{ Nagram::Media::CoverLocation(this, AudioAlbumThumbLocation{ id }) }`，一行。函数在 `nagram/media/` 中：模板为空时原样返回传入的位置；非空时用表演者与标题展开模板，返回 `PlainUrlLocation`。

**桌面端协议**：模板是一个 URL，必须直接返回图片。占位符 `{artist}`、`{title}`，展开时做百分号编码。校验规则：`https`，或回环地址的 `http`；长度不超过 512；至少包含 `{title}`；不含换行。校验不通过时设置页拒绝保存。Android 端 `CustomArtworkApi` 的响应格式本次没有核对源码，两端字段不互通。

**与上游一致**：模板为空（默认）时位置对象与上游完全相同，不发起任何外部请求。

**限度**：

- 只作用于没有自带缩略图的歌曲；已经算出位置的文档不会回头更换，改动对之后加载的音频生效，说明文字写明。
- 表演者和标题会发给用户填写的服务，说明文字写明。
- 外部服务失败时不显示封面，不回退到 Telegram 的封面服务（沿用需求“不能失败后自动切换服务”的约定）。
- `webFileLoader` 的体积上限是上游常量 `kMaxWebFile`（4000 MB），对封面来说过大，见第 8 节问题 3。
- 模板可能带令牌，注册为不可导出。

### 2.4 A080–A082 外部贴纸缓存

**现状**（均为上游已有接口，不需要改动）：

- 贴纸集与顺序：`Data::Stickers::sets()`、`setsOrder()`；`Data::StickersSet` 含 `id`、`hash`、`title`、`shortName`、`stickers`、`flags`（`NotLoaded` 表示内容未加载）。
- 变化通知：`Data::Stickers::updated(StickersType)`。
- 补全未加载的贴纸集：`ApiWrap::scheduleStickerSetRequest()`、`requestStickerSets()`。
- 取文件：`DocumentData::save(origin, QString())` 配合 `Data::FileOriginStickerSet`、`DocumentData::stickerSetOrigin()`，完成后从 `Data::DocumentMedia::bytes()` 取字节；进度通过 `Main::Session::downloaderTaskFinished()` 得知。
- 目录选择：`FileDialog::GetFolder()`（`core/file_utilities.h`）。

**挂钩**：`main/main_session.cpp` 的 `Main::Session` 构造函数中，在已有的 `Nagram::ViewRefresher::Attach(this)` 之后加一行 `Nagram::Media::StickerExport::Attach(this)`。其余逻辑都在 `nagram/media/`。`Attach` 只订阅选项和 `updated()`，不在构造期间做同步，同步推迟到事件循环。

**模块划分**：

- `nagram/media/sticker_export_model.{h,cpp}`：纯逻辑。目录名生成与清洗、清单（manifest）解析与序列化、同步计划（比较上次清单与当前贴纸集，得出要写、要删、要改名的项）、同步代次的状态机。只依赖 Qt Core，可进 `test_nagram`。
- `nagram/media/sticker_export.{h,cpp}`：随会话存在的执行者。负责下载、写盘、错误状态和通知设置页。

**目录结构**：

```
<导出目录>/
  nagram-stickers.json          根清单 {"version":1,"sets":[{"id","dir","hash","count"}]}
  <贴纸集目录>/
    set.json                    {"version":1,"id","shortName","title","stickers":[{"id","file","emoji"}]}
    <documentId>.webp|.tgs|.webm
```

文件以文档 ID 命名，顺序记录在 `set.json`，贴纸集内重排时不需要改名。写入一律用 `QSaveFile`。

**目录命名（A082）**：0 短名称（`shortName`），1 标题，2 贴纸集 ID。枚举值是桌面端自己的定义，不沿用 Android 整数。清洗规则：去掉路径分隔符、控制字符和 Windows 保留字符与保留名，去掉结尾的点和空格，限制长度；清洗后为空或与其他集重名时追加 `_<id>`。改变命名方式后，下一次同步把根清单中记录的旧目录改名；改名失败则按新名重新导出，旧目录保留。

**同步流程**：

1. 读根清单，按当前贴纸集（只含普通贴纸，不含面具与自定义表情）生成计划；`hash` 与数量都没变的集跳过。
2. 未加载的贴纸集先请求补全，等 `updated()` 后重新生成计划。
3. 逐个文档取字节并写盘，同时进行的下载数设上限。
4. 一个集全部写完后写 `set.json`，再更新根清单。
5. 贴纸集内已移除的贴纸：只删除上一版 `set.json` 中记录过的文件，目录里其他文件不动。
6. 已卸载的贴纸集：目录保留，不自动删除，只从根清单移除。

**触发**：设置页的“立即同步”动作；A081 开启时，会话启动后和 `updated(StickersType::Stickers)` 之后去抖触发。

**竞态与失败处理**：

- 每次同步有一个代次。路径、命名方式变化，或新的同步开始时代次加一；下载和写盘回调先比对代次，过期的结果直接丢弃。
- 多账号：路径是本机选项，各会话的执行者写同一个根目录。同一贴纸集内容相同，写入幂等；根清单的读改写放在主线程的同一事件内完成，不跨异步边界持有旧清单。
- 目录不存在、不可写或磁盘已满（例如外接盘被拔出）：停止本次同步，保留已写完的集，在设置页显示失败原因；路径不清空；自动同步在本次运行期间暂停，直到用户手动同步成功或重启。
- 单个文件下载失败：该集不写 `set.json`、不进根清单，下次同步重试；其他集继续。
- 会话销毁：执行者随会话析构，未完成的下载由上游加载器自行取消。

**与上游一致**：路径为空（默认）时 `Attach` 不订阅 `updated()`，不发起任何下载，不写任何文件。

**作用域**：路径与两个子选项都是本机选项；路径是本机文件系统位置，注册为不可导出。

### 2.5 D047 网页应用兼容身份

**现状**：

- `inline_bots/bot_attach_web_view.cpp` 的 `WebViewInstance::requestButton()`、`requestSimple()`、`requestMain()`、`requestApp()`、`requestChatJoin()` 五处请求都以 `MTP_string("tdesktop")` 作为平台参数。服务端据此生成网页应用地址，网页里的 `Telegram.WebApp.platform` 由它决定。
- `Telegram/lib_webview` 中没有任何设置 User-Agent 的接口（在 `lib_webview` 下搜索 `userAgent`、`user_agent` 无结果）。
- 桌面端在 `ui/chat/attach/attach_bot_webview.cpp` 的 `Panel::createWebview()` 注入 `window.TelegramWebviewProxy`，事件通道与平台参数无关。

**结论**：平台参数可改，User-Agent 不可改（需要给三个平台的 `lib_webview` 后端各加接口，属于改子模块）。只改平台参数就能让多数按 `Telegram.WebApp.platform` 判断的网页应用把客户端识别为 Android；按 User-Agent 判断的网页应用不受影响，说明文字写明。

**挂钩**：五处 `MTP_string("tdesktop")` 改为 `MTP_string(Nagram::Links::WebAppPlatform(_bot))`，各一行。函数放在 `nagram/links/`，返回 `"tdesktop"` 或 `"android"`。传入 `_bot`（`WebViewInstance` 的成员）是为了以后按机器人区分时不必再改上游。

**与上游一致**：选项关闭时返回 `"tdesktop"`。

**限度**：

- 只对之后打开的网页应用生效，已打开的面板不变。
- 网页应用可能因此调用桌面端没有实现的 Android 专属事件，桌面端按上游逻辑忽略未知事件；说明文字提示“部分功能可能不可用”。
- 支付、Instant View、游戏分享等其他网页视图不经过这五处请求，不受影响。
- 需求写的是逐站点可选，本设计先做全局开关，见第 8 节问题 1。

### 2.6 A017 噪音抑制和语音增强（条件不满足）

按三个可能的落点分别核对：

- **群通话**：上游已有“噪声抑制”开关（`calls/group/calls_group_settings.cpp`，写入 `Core::Settings::groupCallNoiseSuppression`，经 `GroupCall::setNoiseSuppression()` 生效），不重复提供。
- **私聊通话**：`calls/calls_call.cpp` 的 `Call::createAndStartController()` 在 `tgcalls::Config` 中写了 `enableAEC = false`、`enableNS = true`、`enableAGC = true`，但在 `ThirdParty/tgcalls/tgcalls` 中这三个字段只有 `Instance.h` 里的声明，没有任何实现读取它们；`v2/InstanceV2Impl.cpp` 把 `_disableOutgoingAudioProcessing` 写死为 `false`。桌面端 `calls/calls_controller_webrtc.cpp` 的 `setEchoCancellationStrength()` 等也是空实现。没有可接的开关。
- **语音消息录制**：`media/audio/media_audio_capture.cpp` 用 OpenAL 采集 PCM 后只做跳过开头和淡入，再直接编码，没有处理链。

**缺什么**：私聊通话需要 tgcalls 提供开关（改子模块）；语音录制需要先做一个把音频处理模块接进采集线程的原型，并确认三个平台的 WebRTC 构建都能从 `Telegram` 目标直接使用该模块。两者具备之前不注册选项。

### 2.7 A146 播放器解码器（条件不满足）

桌面端的音视频解码只有 FFmpeg 一条路径：`ffmpeg/ffmpeg_utility.cpp` 用 `avcodec_find_decoder` 选解码器，`hwAllowed` 为真时挂上 `GetHwFormat` 尝试硬件解码，否则软件解码。`hwAllowed` 来自上游设置“Hardware accelerated video decoding”（`settings/sections/settings_advanced.cpp`，`Core::Settings::hardwareAcceleratedVideo`），由 `media/view/media_view_overlay_widget.cpp` 和 `media/view/media_view_pip.cpp` 传入。

Android 的选项是在多套解码器实现之间选择，桌面端没有第二套实现；硬件与软件之间的选择上游已有入口。不注册选项，继续使用上游入口。

**缺什么**：除 FFmpeg 之外的可维护解码后端。

### 2.8 D115 音乐封面自适应颜色（条件不满足）

桌面端的播放器界面（`media/player/` 下的播放条与面板）不取用文档缩略图，在该目录搜索 `thumbnail`、`goodThumb` 无结果。封面只出现在消息气泡（`history/view/media/history_view_document.cpp`）、共享媒体列表（`overview/overview_layout.cpp`）和系统媒体控件（`media/system_media_controls_manager.cpp`）里，作为小图叠加播放按钮，没有以封面为背景、需要随封面取色的界面。

**缺什么**：一个显示封面的播放界面。上游以后增加这类界面时再评估。

### 2.9 N019 地图预览服务

**现状**：`data/data_session.cpp` 的 `Data::Session::location()` 用 `Data::ComputeLocation()`（`data/data_location.cpp`）生成 `GeoPointLocation`，在 `storage/download_manager_mtproto.cpp` 中通过 `upload.getWebFile` + `inputWebFileGeoPointLocation` 由 Telegram 服务端返回图片。用哪家地图由服务端决定，客户端没有选择。仓库里与第三方地图有关的代码只有两类：位置选择器（`ui/controls/location_picker.cpp`）用服务端下发的令牌加载 Mapbox 交互地图；点击位置时打开 `maps.google.com` 链接或系统地图。没有任何静态地图图片服务的接入或密钥。

**结论**：

- “在若干内置服务商之间选择”条件不满足。**缺什么**：不需要密钥、可长期使用的静态地图服务，或者由维护者提供并承担配额的密钥。
- 可行的受限形式是用户自填地址模板，机制与 A044 相同：`Data::Session::location()` 中的 `{ location }` 改为 `{ Nagram::Media::MapPreviewLocation(location) }`，一行；模板为空时原样返回 `GeoPointLocation`。占位符 `{lat}`、`{lon}`、`{zoom}`、`{width}`、`{height}`、`{scale}`，校验规则同 A044，另要求同时包含 `{lat}` 和 `{lon}`。
- 受限形式只覆盖消息气泡与投票中的位置预览。内联结果（`inline_bots/inline_bot_result.cpp`）和 Instant View（`iv/iv_cached_media.cpp`、`iv/editor/iv_editor_session.cpp`）各自构造 `GeoPointLocation`，保持走 Telegram。
- `Data::Session::location()` 按坐标缓存 `CloudImage`，已创建的预览不会更换，选项标为重启后生效。
- 经纬度会发给用户填写的服务，说明文字写明。

是否做受限形式由维护者决定，见第 8 节问题 2。决定不做时，N019 整项按“条件不满足”处理，下面各节标“（待定）”的内容全部不提交。

## 3. 注册表条目

类型沿用 `nagram/core/options.h` 的 `Option<bool>`、`Option<int>`、`Option<QString>`。本机作用域的条目默认可导出；标 `Flag::Hidden` 的不进入配置交换。

| 条目 | 键 | 类型 | 默认值 | 作用域 | 分栏（`Category`） | 可导出 | 需重启 | 校验 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| N121 | `nagram.voiceRecordBitrate` | `int` | `0`（跟随 Telegram） | D | 媒体与贴纸（`Media`） | 是 | 否 | 0、16、24、48、64、96、128 |
| N122 | `nagram.groupCallRawAudio` | `bool` | `false` | D | 媒体与贴纸（`Media`） | 是 | 否，下次加入通话时生效 | — |
| A044 | `nagram.musicCoverUrl` | `QString` | `""` | D | 媒体与贴纸（`Media`） | 否（`Hidden`） | 否，对之后加载的音频生效 | 空，或通过模板校验 |
| A080 | `nagram.stickerExportPath` | `QString` | `""` | D | 媒体与贴纸（`Media`） | 否（`Hidden`） | 否 | 空，或绝对路径且长度不超过 1024 |
| A081 | `nagram.stickerExportAutoSync` | `bool` | `false` | D | 媒体与贴纸（`Media`） | 是 | 否 | — |
| A082 | `nagram.stickerExportDirNaming` | `int` | `0`（短名称） | D | 媒体与贴纸（`Media`） | 是 | 否，下次同步时生效 | 0–2 |
| D047 | `nagram.webAppAndroidPlatform` | `bool` | `false` | D | 规则（`Rules`） | 是 | 否，下次打开网页应用时生效 | — |
| N019（待定） | `nagram.mapPreviewUrl` | `QString` | `""` | D | 规则（`Rules`） | 否（`Hidden`） | 是（`RequiresRestart`） | 空，或通过模板校验 |

A017、A146、D115 不注册。在 `main` 分支没有找到旧实现的 `nagram_settings.h`，这些键没有可沿用的旧名。

## 4. 设置页行

编号接在 [设置页设计](settings-page.md) 各分栏已有编号之后（媒体与贴纸到 F18，规则到 I03）。其他 P3 专项先占用了这些编号时，合并时顺延。

### 媒体与贴纸

**录制与通话**（新分组）

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| F19 | 语音消息码率 | 选项：跟随 Telegram / 16 / 24 / 48 / 64 / 96 / 128 kbps | D | 对之后录制的语音消息生效；码率越高文件越大。 |
| F20 | 关闭群组通话的音频处理 | 开关 | D | 不做回声消除、降噪和自动增益，下次加入通话时生效。外放时可能产生回声，建议使用耳机。 |

**音乐封面**（新分组）

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| F21 | 自定义封面地址 | 文本；右侧显示“默认”或主机名 | D | 留空时由 Telegram 提供封面。填写后，没有自带封面的音乐会把表演者和标题发送到该地址；地址须直接返回图片，可用 `{artist}` 和 `{title}`。获取失败时不显示封面。对之后加载的音频生效；不会导出。 |

**贴纸导出**（新分组，位于“贴纸目录”之后）

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| F22 | 导出目录 | 动作：选择文件夹；右侧显示“未设置”或目录名；已设置时点击可更换或清除 | D | 把已安装贴纸集的原始文件保存到该目录，供其他程序使用。清除目录不会删除已导出的文件；不会导出到配置文件。 |
| F23 | 自动同步 | 从属开关（F22 已设置时可用） | D | 贴纸集变化后自动更新导出目录。 |
| F24 | 目录命名方式 | 从属选项：短名称 / 标题 / 贴纸集 ID | D | 下次同步时生效。 |
| F25 | 立即同步 | 动作（F22 已设置时可用）；右侧显示“同步中 n/m”、上次结果或失败原因 | D | 已卸载的贴纸集目录不会自动删除。 |

### 规则

**网页应用与地图**（新分组）

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| I09 | 网页应用使用 Android 平台标识 | 开关 | D | 下次打开网页应用时生效。只改变 Telegram 告知网页应用的平台，浏览器标识不变；部分功能可能不可用。 |
| I10（待定） | 自定义地图预览地址 | 文本；右侧显示“默认”或主机名 | D | 留空时由 Telegram 提供预览。填写后，位置消息的经纬度会发送到该地址；地址须直接返回图片，可用 `{lat}`、`{lon}`、`{zoom}`、`{width}`、`{height}`、`{scale}`。重启后生效；不会导出。 |

搜索 ID 沿用现有格式：`nagram/media/voice-bitrate`、`nagram/media/group-call-raw-audio`、`nagram/media/cover-url`、`nagram/media/sticker-export`、`nagram/rules/web-app-platform`、`nagram/rules/map-preview-url`。

## 5. 三语文案键

三个文件（`Telegram/Resources/langs/nagram/nagram.strings`、`zh-hans.strings`、`zh-hant.strings`）同时提交。

| 键 | 用途 |
| --- | --- |
| `lng_nagram_recording_calls` | 分组标题“录制与通话” |
| `lng_nagram_voice_record_bitrate` | F19 标题 |
| `lng_nagram_voice_record_bitrate_about` | F19 说明 |
| `lng_nagram_voice_record_bitrate_value` | 选项文字，含占位符 `{value}`（kbps） |
| `lng_nagram_group_call_raw_audio` | F20 标题 |
| `lng_nagram_group_call_raw_audio_about` | F20 说明 |
| `lng_nagram_music_cover` | 分组标题“音乐封面” |
| `lng_nagram_music_cover_url` | F21 标题 |
| `lng_nagram_music_cover_url_about` | F21 说明 |
| `lng_nagram_external_url_default` | F21、I10 右侧“默认” |
| `lng_nagram_external_url_invalid` | 模板校验失败提示 |
| `lng_nagram_sticker_export` | 分组标题“贴纸导出” |
| `lng_nagram_sticker_export_path` | F22 标题 |
| `lng_nagram_sticker_export_path_about` | F22 说明 |
| `lng_nagram_sticker_export_path_unset` | F22 右侧“未设置” |
| `lng_nagram_sticker_export_path_choose` | 目录选择框标题 |
| `lng_nagram_sticker_export_path_clear` | 清除目录 |
| `lng_nagram_sticker_export_auto_sync` | F23 标题 |
| `lng_nagram_sticker_export_auto_sync_about` | F23 说明 |
| `lng_nagram_sticker_export_dir_naming` | F24 标题 |
| `lng_nagram_sticker_export_dir_naming_about` | F24 说明 |
| `lng_nagram_sticker_export_dir_short_name` | F24 选项“短名称” |
| `lng_nagram_sticker_export_dir_title` | F24 选项“标题” |
| `lng_nagram_sticker_export_dir_id` | F24 选项“贴纸集 ID” |
| `lng_nagram_sticker_export_sync_now` | F25 标题 |
| `lng_nagram_sticker_export_sync_now_about` | F25 说明 |
| `lng_nagram_sticker_export_progress` | 进度，占位符 `{done}`、`{total}` |
| `lng_nagram_sticker_export_done` | 上次同步结果，占位符 `{count}` |
| `lng_nagram_sticker_export_error_path` | 目录不存在或不可写 |
| `lng_nagram_sticker_export_error_write` | 写入失败（磁盘已满等） |
| `lng_nagram_sticker_export_error_download` | 部分贴纸集下载失败，占位符 `{count}` |
| `lng_nagram_web_and_maps` | 分组标题“网页应用与地图” |
| `lng_nagram_web_app_android_platform` | I09 标题 |
| `lng_nagram_web_app_android_platform_about` | I09 说明 |
| `lng_nagram_map_preview_url`（待定） | I10 标题 |
| `lng_nagram_map_preview_url_about`（待定） | I10 说明 |

注册表的 `titleKey` 使用各条目的标题键。“跟随 Telegram”沿用已有的 `lng_nagram_preview_follow`。

## 6. 测试

### 6.1 单元测试（`test_nagram`）

新增 `nagram/tests/test_media.cpp`，加入 `Telegram/cmake/nagram.cmake` 的 `test_nagram` 源文件列表。被测代码只依赖 Qt Core 与 `nagram/core`。

| 对象 | 用例 |
| --- | --- |
| 注册表 | 第 3 节各键的默认值、类型、作用域；三个 `Hidden` 键不可导出，其余可导出；N121 拒绝 32（与默认重复的显式值）、100、负数；A082 拒绝 3 |
| `VoiceRecordBitrate` | 选项为 0 时返回传入的回退值；非 0 时返回 kbps × 1000；存储中是非法值时返回回退值并报告读取错误 |
| URL 模板校验 | 接受 `https`；接受回环地址的 `http`；拒绝其他主机的 `http`、`file:`、无主机、超长、含换行、缺少必需占位符、未知占位符 |
| URL 模板展开 | 空格、`&`、`#`、`/`、中文和表情做百分号编码；占位符重复出现；值里含 `{title}` 字样时不二次展开 |
| 回退 | 模板为空时位置选择函数返回“使用上游位置”；模板在存储中损坏时同样回退并报告读取错误，不发起请求 |
| 目录命名 | 三种方式；路径分隔符、控制字符、Windows 保留名（`CON`、`NUL` 等）、结尾的点和空格、超长标题；清洗后为空；两个集清洗后同名时追加 ID 且结果稳定 |
| 清单 | 根清单与 `set.json` 往返；版本不符、未知字段、类型错误、重复 ID、`dir` 含 `..` 或绝对路径时拒绝，且不改动原文件 |
| 同步计划 | 新增、`hash` 变化、集内移除贴纸、卸载、命名方式变化、无变化（空计划）；删除项只来自上一版清单；清单损坏时计划为“全部重新导出、不删除任何文件” |
| 同步代次（异步竞态） | 开始新一轮后，旧代次的完成、失败回调被拒绝；路径变化使进行中的一轮失效；同一代次重复完成只计一次；失败后状态为“暂停自动同步”，手动同步成功后恢复 |
| `WebAppPlatform` | 关闭时为 `tdesktop`，开启时为 `android` |
| 文案 | 现有三语键集合与占位符一致性检查覆盖第 5 节新增键 |

### 6.2 手动检查

外部服务一律用 localhost 桩，不向真实聊天发送消息；录制的语音只发到收藏夹。

**N121**

- 默认值下录一段语音，文件码率与上游构建一致。
- 依次选 16、64、128 kbps 各录一段，用 E20“媒体信息”或 `ffprobe` 核对码率；波形和时长正常，能播放。
- 录制过程中改选项，本段不受影响，下一段生效。
- 暂停后继续录制、试听后发送（D24）两条路径正常。

**N122**

- 关闭时加入群通话，行为与上游一致。
- 开启后重新加入：说话可被听到；外放时对方能听到回声（预期）。
- 通话中切换开关，当前通话不变；退出重进后生效。
- 与上游“噪声抑制”同时开启时的实际效果，记录到说明文字。
- 屏幕共享、直播（RTMP）观看不受影响。

**A044**

- 模板为空：抓包确认没有外部请求，封面由 Telegram 提供。
- 桩返回图片：没有自带缩略图的歌曲显示桩的封面；自带缩略图的歌曲不请求。
- 外部能力不可用：桩返回 404、500、非图片内容、超时、连接被拒绝；封面不显示，播放、下载、列表滚动正常，不出现重复请求风暴。
- 表演者或标题含特殊字符时，桩收到的请求编码正确。
- 加载过程中修改模板：进行中的请求按旧地址完成，新地址只用于之后加载的音频，不出现错配的封面。
- 导出配置，文件中不含该地址。

**A080–A082**

- 未设置目录时没有任何下载和写盘。
- 选目录后手动同步：目录结构、文件数、`set.json` 顺序与贴纸面板一致；静态、动画（`.tgs`）、视频（`.webm`）贴纸各有样本。
- 再次同步：没有变化时不重写文件（修改时间不变）。
- 安装、卸载贴纸集，集内增删贴纸（用自己的测试贴纸集）：自动同步开启时目录随之更新；卸载的集目录保留。
- 切换命名方式：旧目录被改名；标题含特殊字符或两个集同名时目录名正确。
- 异步竞态：同步进行中更换目录、清除目录、切换命名方式、连续点击“立即同步”、切换账号、退出账号、关闭应用；不崩溃，旧目录不再被写入，新目录内容完整。
- 外部能力不可用：目录被删除、设为只读、所在卷被弹出、磁盘写满；设置页显示失败原因，路径保留，自动同步暂停，恢复目录后手动同步成功。
- 网络断开时同步：未完成的集不进清单，恢复网络后补齐。
- 目录里放入无关文件，同步后仍在。
- 双账号各自同步到同一目录：根清单完整，没有互相覆盖出的损坏。
- Windows 上的长路径和保留名、Linux 上区分大小写的文件系统各检查一次。

**D047**

- 关闭时五种入口（消息按钮、键盘按钮、主应用、直链应用、加群验证）打开的网页应用平台为 `tdesktop`。
- 开启后重新打开，用一个显示 `Telegram.WebApp.platform` 的测试网页确认值为 `android`；`navigator.userAgent` 不变。
- 已打开的面板不变，重新打开后生效。
- 主题、主按钮、返回按钮、关闭确认、全屏等桌面已实现的事件仍然可用；调用桌面未实现的事件时不崩溃。

**N019（待定）**

- 模板为空时没有外部请求。
- 桩返回图片：位置消息与投票里的位置预览来自桩；内联结果和 Instant View 仍走 Telegram。
- 桩不可用：预览为空，点击位置仍能打开地图，消息列表正常。
- 修改模板后提示重启，重启前已显示的预览不变。

## 7. 提交拆分

按 [分步实施计划](implementation-plan.md) 第 2 节：每个提交包含注册表条目、设置页行、三语文案、上游挂钩和单元测试，并同步更新 `settings-page.md`、`upstream-hooks.md` 和实施计划的步骤表。步骤号是本文件内的标识，并入实施计划时换成当时可用的 S 编号。每步通过 V1（macOS Debug 增量构建、`test_nagram`、所列手动场景）。

| 步骤 | 提交信息 | 条目 | 上游改动文件 | 验证 |
| --- | --- | --- | --- | --- |
| S170 | `feat(media): voice message bitrate` | N121（F19） | `media/audio/media_audio_capture.cpp` | `test_nagram`（注册表、`VoiceRecordBitrate`）；N121 手动场景 |
| S171 | `feat(media): turn off audio processing in group calls` | N122（F20） | `calls/group/calls_group_call.cpp` | `test_nagram`（注册表）；N122 手动场景 |
| S172 | `feat(media): custom music cover source` | A044（F21） | `data/data_document.cpp` | `test_nagram`（模板校验、展开、回退）；A044 手动场景，localhost 桩 |
| S173 | `feat(media): export sticker sets to a folder` | A080–A082（F22–F25） | `main/main_session.cpp` | `test_nagram`（目录命名、清单、同步计划、同步代次）；A080–A082 手动场景 |
| S174 | `feat(rules): Android platform identity for web apps` | D047（I09） | `inline_bots/bot_attach_web_view.cpp` | `test_nagram`（`WebAppPlatform`）；D047 手动场景 |
| S175（待定） | `feat(rules): custom map preview source` | N019（I10） | `data/data_session.cpp` | `test_nagram`（模板必需占位符）；N019 手动场景，localhost 桩 |

说明：

- 外部 URL 模板的校验与展开（`nagram/media/url_template.{h,cpp}`）随第一个使用者 S172 提交；S175 只增加占位符集合。
- S170 至 S174 互不依赖，顺序可调；S175 依赖 S172。
- 新增源文件加入 `Telegram/cmake/nagram.cmake`，不改 `Telegram/CMakeLists.txt`。
- A017、A146、D115 不产生提交。本文件随文档提交进入仓库后，在 `feature-catalog.md` 对应行注明“条件不满足，见 P3-07 设计”，与 N019 的最终结论一起更新。
- 上游改动合计 5 个文件（含待定项 6 个），其中 `main/main_session.cpp` 已有 Nagram 挂钩，其余为首次改动。除 S170 约 6 行外，各处都是单行调用或单个表达式替换。

## 8. 需要维护者决定的问题

1. **D047 的粒度**。需求 F16 写“网页兼容身份为逐站点可选项”，来源 D047 是全局开关。本设计先做全局开关，挂钩函数已带 `_bot` 参数。逐机器人列表需要一个入口，最自然的位置是网页应用面板的菜单（`ui/chat/attach/attach_bot_webview.cpp`），要改上游菜单构造与 `MenuButton` 枚举，并新增一个账号作用域的结构化选项。请决定：只做全局开关，还是同时做逐机器人列表。
2. **N019 是否做“自定义地址模板”**。内置服务商条件不满足。受限形式只多改一行上游，但用户需要自己找到能直接返回图片的服务，并且经纬度会发给第三方。请决定：做受限形式（S175），还是整项记为条件不满足。
3. **A044 的协议与体积上限**。桌面端定义为“直接返回图片的 URL 模板”，与 Android 字段不互通；Android 端的响应格式没有核对。另外复用上游 `webFileLoader` 时体积上限是 4000 MB。请决定：接受现状（实现最小），还是改为由 `nagram/services/` 的请求层下载并限制体积（例如 2 MB）后再调用 `Data::DocumentMedia::setThumbnail()`，代价是多一段异步逻辑和一处上游挂钩。
4. **A017 语音录制降噪是否单独立项做原型**。需要把音频处理模块接入 `media_audio_capture.cpp` 的采集线程，并在三个平台确认可用。本包不做；如需要，另开专项。
5. **贴纸导出范围**。本设计只导出普通贴纸集，不含面具和自定义表情包，卸载的集不自动删除目录。如需调整请指出。
