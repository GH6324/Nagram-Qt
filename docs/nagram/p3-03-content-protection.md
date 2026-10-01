# P3-03 内容保护与敏感内容 专项设计

本文件是 [功能需求](requirements.md#priority) 中 P3-03 包的专项设计，对应功能族 [F10](requirements.md#f10)。架构约束见 [设计与路线](design.md) 第 3 节，提交规则见 [分步实施计划](implementation-plan.md) 第 2、3、5 节，设置页惯例见 [设置页设计](settings-page.md)。文中上游路径相对 `Telegram/SourceFiles/`，位置均按 2026-10-01 的工作副本（`@-` 为 `docs(nagram): mark deferred P3 features`）实际读到的代码填写；实现时以当时的上游代码重新确认。

## 1. 范围与不做的事

### 范围

| 来源编号 | 功能 | 本文条目 |
| --- | --- | --- |
| I001、A001 | 解除内容保护 / 强制复制：`noforwards` 会话与消息的复制、选择、保存 | G13 |
| N095 | 忽略内容限制（restriction reason） | G14 |
| I002 | 自动显示受限（敏感）媒体，跳过敏感内容警告 | G15 |
| A063 | 添加联系人时默认不分享我的手机号码 | G16 |

四项都只改变本机行为：读取客户端已经收到并能解码的数据，或改变一个复选框的初始值。不新增网络请求，不修改服务端设置，不改上游持久化格式。

### 不做的事

- 已搁置的细项不设计：本地高级外观能力（N126、D053）、本地名称与引用颜色（A074、A075）、账号锁 / 隐藏、设置锁 / 隐藏、系统解锁界面、应急操作（A025、S21）。见[暂不实现的 P3 细项](requirements.md#p3-deferred)。
- 不绕过服务端校验：受保护消息的**转发**由服务端拒绝（`ApiWrap::sendMessageFail` 处理 `CHAT_FORWARDS_RESTRICTED`），本设计不改 `HistoryItem::allowsForward()`，也不提供“以副本重发”的替代动作。
- 不改变对外可见的保护状态：`PeerData::allowsForwarding()`、`HistoryItem::forbidsForward()` 本身不动。群组 / 频道设置里的“限制保存内容”开关（`boxes/peers/edit_peer_info_box.cpp`）和私聊的分享开关（`window/window_peer_menu.cpp`）仍显示服务端真实状态。
- 不处理限时媒体（`ttlSeconds`）、付费媒体（`HasExtendedMedia`）、付款页面的保护；这些分支保持上游行为。
- 不处理动态（story）的保护：`Data::Story::forbidsForward()`、`canDownloadChecked()` 与 Premium 权益耦合，按“不伪造服务端权益”保持上游。
- 不改聊天记录导出：`PeerData::canExportChatHistory()` 保持上游。
- 不绕过年龄验证：`AppConfig::ageVerifyNeeded()` 为真时 G15 不生效。
- 不恢复服务端未下发的数据：被限制的会话若服务端拒绝返回历史，G14 开启后仍然是空的。

## 2. 逐项结论

Nagram 侧新增 `nagram/privacy/protection.{h,cpp}`（读取选项、组合上游状态、响应式值）和 `nagram/privacy/protection_model.cpp`（不依赖上游类型的纯判断，供 `test_nagram` 链接，做法同 `alias_model.cpp`）。下文的 `Nagram::Privacy::*` 函数都在这两个文件中。

### 2.1 G13 允许复制和保存受保护的内容（I001、A001）

**结论：可实现**（复制文字、选择消息、保存与拖出媒体、媒体查看器内保存 / 复制、翻译框复制）。**转发：条件不满足**，缺少的是服务端许可，客户端无法提供。

上游把“会话禁止转发”和“本机禁止复制 / 保存”合用同一组查询。挂钩只放在**本机保护的消费点**，不动状态来源。Nagram 提供：

| 函数 | 语义 |
| --- | --- |
| `ForceCopy()` | 读取 `kForceCopy` |
| `AllowsCopy(peer)` | `ForceCopy() \|\| peer->allowsForwarding()` |
| `ForbidsCopy(item)` | `!ForceCopy() && item->forbidsForward()` |
| `AllowsCopyValue(peer)` | `kForceCopy` 的值与 `Data::AllowsForwardingValue(peer)` 组合后的 `rpl::producer<bool>` |

上游挂钩位置：

| 文件 | 函数 | 改动 | 覆盖的行为 |
| --- | --- | --- | --- |
| `history/history_item.cpp` | `HistoryItem::forbidsSaving()` | 首个分支 `forbidsForward()` 改为 `Nagram::Privacy::ForbidsCopy(this)`；限时与付费媒体分支不动 | 所有 `forbidsSaving()` 消费者：媒体查看器（`documentUpdated`、`updateControls`、`fillContextMenuActions`）、`Menu` 的批量下载（`menu/menu_item_download_files.cpp` 的 `Collected`）、`CopyMediaRestrictionTypeFor` |
| `history/history_item.cpp` | `HistoryItem::allowsMediaDownloadControls()` | `_history->peer->allowsForwarding()` 改为 `AllowsCopy(...)` | GIF、文件、共享媒体条目上的下载控件 |
| `history/history_inner_widget.cpp` | `HistoryInner::hasCopyRestriction()` | 两个条件分别改用 `AllowsCopy` / `ForbidsCopy` | 普通聊天的复制、拖出、右键菜单、提示条 |
| `history/history_inner_widget.cpp` | `HistoryInner::hasCopyRestrictionForSelected()` | 函数开头加一行：`ForceCopy()` 时返回 `false` | 多选复制 |
| `history/history_inner_widget.cpp` | `HistoryInner::setupSharingDisallowed()` | `Data::AllowsForwardingValue(_peer)` 改为 `AllowsCopyValue(_peer)` | `hasSelectRestriction()` 读取的 `_sharingDisallowed`；开关变化时沿用上游的 `clearIfRestricted` |
| `history/view/history_view_list_widget.cpp` | `CopyRestrictionTypeFor()` | 条件改用 `AllowsCopy` / `ForbidsCopy` | 话题、置顶、计划消息等所有 `ListWidget` 分区（`ChatWidget`、`PinnedWidget`、`ScheduledWidget` 的 `listCopyRestrictionType` 都转到这里），以及 `SelectRestrictionTypeFor()` |
| `history/view/history_view_list_widget.cpp` | `ListWidget::hasCopyRestrictionForSelected()` | 开头加一行，同上 | 多选复制 |
| `history/view/history_view_context_menu.cpp` | `AddSelectRestrictionAction()` | 提前返回的条件改用 `AllowsCopy` / `ForbidsCopy` | 菜单底部“禁止复制”说明项不再出现 |
| `history/view/history_view_context_menu.cpp` | `AddPollActions()` | 传给 `Ui::TranslateBox` 的 `item->forbidsForward()` 改为 `ForbidsCopy(item)` | 投票翻译框的复制 |
| `media/view/media_view_overlay_widget.cpp` | `OverlayWidget::hasCopyMediaRestriction()` | 非动态分支的 `allowsForwarding()` 改用 `AllowsCopy` | 查看器内保存、复制、另存 |
| `window/window_session_controller.cpp` | 匿名命名空间的 `HasSavingRestriction()` | 同上 | 外部媒体查看器选项 |
| `info/media/info_media_provider.cpp` | `Provider::hasSelectRestriction()` | `_peer->allowsForwarding()` 改用 `AllowsCopy` | 共享媒体页的选择 |
| `info/media/info_media_list_widget.cpp` | `ListWidget::setupSelectRestriction()` | 在 `hasSelectRestrictionChanges()` 上合并 `kForceCopy` 的变化 | 关闭开关时已打开的共享媒体页立即清除选择 |
| `iv/iv_rich_message_html_export.cpp` | `AddSaveRichMessageHtmlActionForItem()` | `item->forbidsForward()` 改为 `ForbidsCopy(item)` | 富文本消息“保存为 HTML” |

截屏保护另有两处，与复制共用同一状态，是否随 G13 一起解除见第 8 节问题 1（建议一起解除）：

| 文件 | 函数 | 改动 |
| --- | --- | --- |
| `window/window_session_controller.cpp` | `SessionController::setupScreenshotProtection()` | `Data::AllowsForwardingValue(peer)` 改为 `AllowsCopyValue(peer)` |
| `media/view/media_view_overlay_widget.cpp` | `OverlayWidget::contentNeedsScreenshotProtection()` | 非动态分支改用 `AllowsCopy`；`forbidsSaving()` 部分已由上表覆盖 |

明确不改的调用点：`HistoryItem::allowsForward()`、`FormattedDateClickHandler::onClick`（设置提醒，实际是转发到收藏夹）、`iv/iv_instance.cpp` 的 `CanShareMarkdownItem()`（转发）、`PeerData::canExportChatHistory()`、`history/view/media/history_view_story_mention.cpp`、`chat_helpers/ttl_media_layer_widget.cpp` 与 `payments/payments_checkout_process.cpp` 的截屏保护。

Nagram 自己的消费者：`nagram/services/transcription.cpp` 中转写结果标签的 `setSelectable(current->allowsForward())` 改为同时接受 `ForceCopy()`；`nagram/menu/repeat.cpp`、`batch.cpp`、`message_tools.cpp` 属于发送 / 转发，保持 `allowsForward()`；`nagram/snapshot/snapshot.cpp` 是否放开见第 8 节问题 2（建议保持不变）。

**关闭时与上游一致**：`ForceCopy()` 为假时，`AllowsCopy(peer)` 恒等于 `peer->allowsForwarding()`，`ForbidsCopy(item)` 恒等于 `item->forbidsForward()`，`AllowsCopyValue(peer)` 去重后与 `Data::AllowsForwardingValue(peer)` 的序列相同，新增的提前返回不触发。这四条等价关系由纯函数的单元测试固定。选项带 `RefreshMessageView` 标记，切换后由现有 `ViewRefresher` 刷新已加载消息，下载控件随之重建。

### 2.2 G14 忽略内容限制（N095）

**结论：可实现**，限定为“客户端已收到的数据”。服务端按限制拒绝返回的会话历史无法恢复。

上游的全部限制判断汇聚到一个函数：`data/data_peer.cpp` 的 `Data::UnavailableReason::Compute(session, list)`。它被 `PeerData::computeUnavailableReason()` 和 `HistoryItem::computeUnavailableReason()` 调用，后两者的消费者是：

- 会话级：`MainWidget::showHistory()`（打开时弹出原因并拒绝）、`HistoryWidget` 构造函数与 `ChatWidget` 构造函数中对 `PeerUpdateFlag::UnavailableReason` 的订阅（收到后关闭当前会话）、`SeparateId::hasChatsList()`、`history/history_view_pull_to_next_channel.cpp`（下一个频道的候选）。
- 消息级：`Element::refreshMedia()`（有原因时不创建媒体）、`Element::validateText()`（正文替换为斜体原因）。

挂钩：

| 文件 | 函数 | 改动 |
| --- | --- | --- |
| `data/data_peer.cpp` | `Data::UnavailableReason::Compute()` | 开头加一行：`Nagram::Privacy::IgnoreRestrictions()` 为真时返回空字符串 |
| `window/window_session_controller.cpp` | `SessionController::SessionController()`（已有 `Nagram::Chats::Watch*` 的位置） | 加一行 `Nagram::Privacy::WatchRestrictions(this)` |

`WatchRestrictions` 订阅 `kIgnoreContentRestrictions` 的变化（跳过初始值），对该窗口 `activeChatCurrent()` 的 peer 调用 `session().changes().peerUpdated(peer, Data::PeerUpdate::Flag::UnavailableReason)`。这样关闭开关时，上游已有的订阅会按原逻辑关闭正在显示的受限会话并提示原因；不需要访问 `PeerData::unavailableReasons()`（私有）。消息级刷新靠选项的 `RefreshMessageView` 标记。

`HistoryItem::computeUnavailableReason()` 里对 `Data::Session::registerRestricted()` 的登记在调用 `Compute` 之前，不受影响，上游 `ignoredRestrictionReasonsChanges` 的刷新照常工作。

限度：`Data::UnavailableReason::Extract()` 只保留平台为 `all` 的原因（商店构建另含 `ios` / `ms`），其他平台的限制上游本来就不生效；`sensitive` 原因由 `Compute` 单独排除，归 G15。

**关闭时与上游一致**：`Compute` 的提前返回不触发，其余代码未改；`WatchRestrictions` 只在选项变化时发出一次上游已有类型的通知。

### 2.3 G15 自动显示敏感媒体（I002）

**结论：可实现，但带能力门槛**。需求要求“仅在 Telegram 返回可调整时显示”“不能绕过年龄 / 服务端校验”，因此生效条件为四项同时成立：

```
开关开启 && sensitiveContent().loaded() && sensitiveContent().canChangeCurrent() && !appConfig().ageVerifyNeeded()
```

任一项不满足时行为与上游相同（显示模糊与“18+”标签，点击后走 `ShowSensitiveConfirm` 或 `ShowAgeVerificationRequired`）。与上游“停用过滤”（`Settings::SetupSensitiveContent`，调用 `account.setContentSettings`）的区别是：G15 只影响本机显示，不改账号的服务端设置，其他设备不受影响。

上游的判断点同样集中：`HistoryItem::isMediaSensitive()` 最后返回 `!Data::UnavailableReason::IgnoreSensitiveMark(session)`，其消费者是 `HistoryView::Photo`、`Gif`、`Sticker`、`GroupedMedia`、`Media::setupSpoilerTag()` 以及 `Overview::Layout` 的 `Photo`、`Video`、`Gif`。

挂钩：

| 文件 | 函数 | 改动 |
| --- | --- | --- |
| `data/data_peer.cpp` | `Data::UnavailableReason::IgnoreSensitiveMark()` | 返回值前加 `Nagram::Privacy::SkipSensitiveWarning(session) \|\|` |
| `main/main_session.cpp` | `Main::Session::Session()`（已有 `Nagram::ViewRefresher::Attach(this)` 的位置之后） | 加一行 `Nagram::Privacy::AttachSensitive(this)` |
| `info/media/info_media_provider.cpp` | `Provider::Provider()` | 在已有的 `ignoredRestrictionReasonsChanges` 订阅旁加一个短订阅：`Nagram::Privacy::SensitiveRevealed(session)` 变为真时，对 `_layouts` 调用 `maybeClearSensitiveSpoiler()`（约 6 行，需要访问该类私有成员 `_layouts`） |

`SkipSensitiveWarning(session)` 读取选项并按上面的四项条件计算。`AttachSensitive(session)` 组合选项值、`Api::SensitiveContent::canChange()`、`loadedValue()` 和 `AppConfig::refreshed()` 后重新计算生效状态，去重后变化时调用 `ViewRefresher::Refresh(session->data())`，并向 `SensitiveRevealed(session)` 推送。这解决异步竞态：`Api::SensitiveContent` 由 `HistoryItem::flagSensitiveContent()` 触发 `preload()`，加载完成前按“不生效”处理，完成后再刷新一次。

实现注意：`AttachSensitive` 使用 `session->lifetime()` 和 `session->api()`，必须放在两者都已构造之后（见 `fix(core): do not use the session lifetime before it is constructed`）；在 `ViewRefresher::Attach` 之后调用即可。

已知限度：共享媒体页在开关**关闭**后不会把已揭示的条目重新遮盖，重新进入页面后恢复。上游在服务端设置关闭时也是同样表现（`maybeClearSensitiveSpoiler` 只清不加）。

与 C19“直接显示剧透内容”的关系不变：`Nagram::Messages::RevealMediaSpoiler()` 仍排除敏感媒体，敏感媒体只由 G15 控制。

**关闭时与上游一致**：`SkipSensitiveWarning` 为假，`IgnoreSensitiveMark` 的结果只取决于上游的 `ignore_restriction_reasons`；`AttachSensitive` 的生效状态恒为假，去重后不触发刷新。

### 2.4 G16 添加联系人时默认不分享我的手机号（A063）

**结论：可实现。**

上游只有一处默认分享：`boxes/peers/edit_contact_box.cpp` 的 `Controller::setupSharePhoneNumber()`，在对方的 `barSettings()` 含 `PeerBarSetting::NeedContactsException` 时加入复选框，初始值写死为 `true`；提交时 `SendRequest()` 按复选框决定是否带 `MTPcontacts_AddContact::Flag::f_add_phone_privacy_exception`。

挂钩：`Controller::setupSharePhoneNumber()` 中复选框的初始值 `true` 改为 `!Nagram::Privacy::DoNotSharePhoneByDefault()`。复选框和下方说明照常显示，用户仍可手动勾选。

不需要改的路径：`boxes/add_contact_box.cpp` 按手机号添加（`MTPcontacts_ImportContacts`）没有分享标志；`ContactStatus::setupShareHandler()` 的 `MTPcontacts_AcceptContact` 是用户点击顶部提示条后的明确操作，已有确认框，且提示条可由 G04 隐藏。

**关闭时与上游一致**：初始值表达式求值为 `true`。

## 3. 注册表条目

在 `nagram/privacy/options.h` 中声明并加入 `Privacy::RegisterOptions`。

| 条目 | 键 | 类型 | 默认 | 作用域 | 分栏 | 可导出 | 需重启 | 标记 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| G13 | `nagram.forceCopy` | `bool` | `false` | 设备 | `Category::Privacy` | 是 | 否 | `RefreshMessageView` |
| G14 | `nagram.ignoreContentRestrictions` | `bool` | `false` | 设备 | `Category::Privacy` | 是 | 否 | `RefreshMessageView` |
| G15 | `nagram.skipSensitiveWarning` | `bool` | `false` | 设备 | `Category::Privacy` | 是 | 否 | `RefreshMessageView` |
| G16 | `nagram.doNotSharePhone` | `bool` | `false` | 设备 | `Category::Privacy` | 是 | 否 | 无 |

- “可导出”由 `Registry::Add` 按“设备作用域且非 `Hidden`”自动得出，不手写标记。
- 没有对话 / 话题级覆盖，不建立 C/T 映射。
- N095 在来源端默认 `true`；按“默认不改变行为”这里默认 `false`。
- 键名：实施计划第 2 节第 6 条要求语义相同时沿用旧键，但 `main` 现已指向新实现，`jj file show -r main Telegram/SourceFiles/nagram/nagram_settings.h` 返回 `No such path`，旧表无从核对。上表按功能目录的核对名取名（`ForceCopy`、`ignoreContentRestrictions`、`skipSensitiveContentWarning`、`DoNotShareMyPhoneNumber`）。旧版数据不迁移，键名不影响兼容。

## 4. 设置页行

位于“隐私与资料”分栏（`nagram/settings/privacy.cpp`），编号接在 G12 之后。G16 归入已有的“本机隐私”分组，放在 G04 之后；G13–G15 组成新的小分组“受保护与受限内容”，放在“本机隐私”之后、“资料信息”之前。

**本机隐私（追加）**

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| G16 | 添加联系人时默认不分享我的手机号 | 开关 | D | 只改变“分享我的手机号”的初始勾选，仍可在添加时手动勾选。 |

**受保护与受限内容**

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| G13 | 允许复制和保存受保护的内容 | 开关 | D | 只作用于本机已收到的消息和已下载的媒体。转发仍由 Telegram 限制，限时和付费媒体不受影响。 |
| G14 | 忽略内容限制 | 开关 | D | 显示被标记为在本平台不可用的会话和消息。Telegram 未下发的内容无法显示。 |
| G15 | 自动显示敏感媒体 | 开关（仅在账号允许调整敏感内容设置时显示） | D | 只影响本机，不改变账号的敏感内容设置。需要年龄验证时不生效。 |

- 控件沿用该页已有的 `AddToggle`；说明文字用 `builder.addDividerText`，与 G02 的做法一致。
- G15 行的显示绑定 `session->api().sensitiveContent().canChange()`；进入页面时调用一次 `reload()`，与上游 `SetupSensitiveContent` 相同。不可调整时该行不显示，也不进入搜索结果。
- 搜索关键词：G13 `copy`、`save`、`protected`、`forward`；G14 `restriction`、`unavailable`；G15 `sensitive`、`18+`、`spoiler`；G16 `share`、`phone`、`contact`。
- 条目 ID：`nagram/privacy/force-copy`、`nagram/privacy/ignore-restrictions`、`nagram/privacy/skip-sensitive-warning`、`nagram/privacy/do-not-share-phone`。
- 四项都立即生效，不加“重启后生效”标签。

## 5. 三语文案键

英文在 `Telegram/Resources/langs/nagram/nagram.strings`，简繁在同目录 `zh-hans.strings`、`zh-hant.strings`。

| 键 | English | 简体中文 | 繁體中文 |
| --- | --- | --- | --- |
| `lng_nagram_protected_content` | Protected and restricted content | 受保护与受限内容 | 受保護與受限內容 |
| `lng_nagram_force_copy` | Allow copying and saving protected content | 允许复制和保存受保护的内容 | 允許複製和儲存受保護的內容 |
| `lng_nagram_force_copy_about` | Applies only to messages and media already received on this device. Forwarding is still restricted by Telegram. Self-destructing and paid media are not affected. | 只作用于本机已收到的消息和已下载的媒体。转发仍由 Telegram 限制，限时和付费媒体不受影响。 | 只作用於本機已收到的訊息和已下載的媒體。轉傳仍由 Telegram 限制，限時和付費媒體不受影響。 |
| `lng_nagram_ignore_restrictions` | Ignore content restrictions | 忽略内容限制 | 忽略內容限制 |
| `lng_nagram_ignore_restrictions_about` | Show chats and messages marked as unavailable on this platform. Content that Telegram does not deliver cannot be shown. | 显示被标记为在本平台不可用的会话和消息。Telegram 未下发的内容无法显示。 | 顯示被標記為在本平台無法使用的對話和訊息。Telegram 未下發的內容無法顯示。 |
| `lng_nagram_skip_sensitive_warning` | Show sensitive media automatically | 自动显示敏感媒体 | 自動顯示敏感媒體 |
| `lng_nagram_skip_sensitive_warning_about` | Affects this device only and does not change the sensitive content setting of your account. Has no effect when age verification is required. | 只影响本机，不改变账号的敏感内容设置。需要年龄验证时不生效。 | 只影響本機，不改變帳號的敏感內容設定。需要年齡驗證時不生效。 |
| `lng_nagram_do_not_share_phone` | Do not share my phone number by default when adding contacts | 添加联系人时默认不分享我的手机号 | 新增聯絡人時預設不分享我的手機號碼 |
| `lng_nagram_do_not_share_phone_about` | Only changes the initial state of “Share my phone number”. You can still tick it when adding a contact. | 只改变“分享我的手机号”的初始勾选，仍可在添加时手动勾选。 | 只改變「分享我的手機號碼」的初始勾選，仍可在新增時手動勾選。 |

`lng_nagram_protected_content` 只在该页实际使用分组小标题时加入；`privacy.cpp` 目前没有小标题，分组靠分隔说明区分，届时不注册没有使用者的键。

## 6. 测试

### 6.1 单元测试（`test_nagram`）

新增 `nagram/tests/test_privacy.cpp`，链接 `nagram/privacy/protection_model.cpp`；两者加入 `Telegram/cmake/nagram.cmake`。

| 对象 | 用例 |
| --- | --- |
| 注册表 | 四个键存在，类型 `bool`，默认 `false`，设备作用域，可导出；G13–G15 带 `RefreshMessageView`，G16 不带；导出再导入往返一致 |
| 复制判定（输入：开关、会话允许转发、消息禁止转发、限时、付费） | 开关关闭时 32 种组合的结果与上游表达式逐一相等；开关开启时仅当限时或付费为真才禁止保存；复制文字在开关开启时恒允许 |
| 敏感媒体判定（输入：开关、已加载、可调整、需年龄验证） | 16 种组合中只有“开、已加载、可调整、不需验证”为真 |
| 分享手机号初始值 | 开关关闭为 `true`，开启为 `false` |
| 文案 | 现有三语键集合与占位符一致性检查覆盖新键 |

### 6.2 手动检查

基础场景（每项都检查：默认关闭时与上游一致、开启生效、关闭还原、重启后保持、设置搜索可达）：

| 条目 | 场景 |
| --- | --- |
| G13 | 在开启“限制保存内容”的群组、频道，以及对方开启分享限制的私聊中：选择文字并复制；右键“复制文字”“复制图片”“图片另存为”；多选后复制；把图片拖出窗口；媒体查看器内保存、复制和“在文件夹中显示”；文件消息出现下载控件；共享媒体页可以多选；右键菜单底部不再出现“禁止复制”说明。普通聊天和话题（两套消息视图）各做一遍 |
| G13 反向 | 开启时“转发”仍不出现；群组设置中的“限制保存内容”仍显示真实状态；限时照片、付费媒体仍不能保存；带保护的动态不受影响 |
| G13 截屏 | 按问题 1 的决定核对：Windows 与 macOS 上受保护会话的窗口是否可被截取 |
| G14 | 找一个带 `all` 平台限制的频道或消息：关闭时打开会话弹出原因、消息正文显示斜体原因；开启后会话可进入、消息与媒体显示；停留在该会话内关闭开关，会话被关闭并提示原因 |
| G15 | 敏感媒体样本：开启后图片、视频、贴纸、相册和共享媒体页直接显示，不弹确认；关闭后聊天内恢复模糊与“18+”标签；上游“停用过滤”开关的状态不被改动；账号不可调整时该行不显示 |
| G16 | 从带“添加联系人”提示条的陌生人会话打开添加联系人框：开启后复选框初始未勾选，手动勾选后提交仍会分享；关闭后初始勾选 |

P3 追加场景：

| 类别 | 场景 |
| --- | --- |
| 断线重连 | 断网后切换 G13–G15，界面按本机已有数据立即变化；恢复连接、收到差量更新后状态保持。G15：离线启动时 `Api::SensitiveContent` 未加载，敏感媒体保持模糊；联网加载完成后自动揭示，不需要手动刷新 |
| 跨账号 | 两个账号同时登录：G13、G14、G16 对两个账号同样生效；G15 在一个账号可调整、另一个不可调整（或需年龄验证）时，只在前者生效，设置页的行随当前账号显示或隐藏。切换账号后两边的消息视图状态正确 |
| 异步竞态 | 开关开启时，服务端推送会话的 `noforwards` 标志变化（管理员切换“限制保存内容”）：选择状态与截屏保护不抖动、不残留。G14：`ignore_restriction_reasons` 的应用配置刷新与开关切换交错时，消息视图最终状态正确。G15：快速连续切换开关，刷新只按最终状态执行；应用配置返回 `need_age_video_verification` 后立即恢复遮盖 |
| 回退 | 逐项关闭后与未安装该功能的构建对照；导入一份不含这些键的配置文件，四项回到默认；把偏好值手动改成非法字节后启动，读取回落到默认并在诊断中报告，原值不被覆盖 |
| 外部能力不可用 | Linux 上上游不支持截屏保护（`Platform::ScreenshotProtectionSupported()` 为假），G13 的截屏部分没有可见效果，其余部分正常 |

没有可用样本的场景（带 `all` 平台限制的会话、需要年龄验证的账号）在提交备注中如实标为未验证，并汇总进 `design.md` 第 8 节。

## 7. 提交拆分

四个条目的上游挂钩互不重叠（`data/data_peer.cpp` 中是两个不同函数），验证样本也不同，因此每个条目一个提交，接在 S110 之后。每个提交都包含注册表条目、设置页行、三语文案、上游挂钩和对应的单元测试，并通过 V1。提交信息用英文，不加 `Co-Authored-By`。

| 步骤 | 提交 | 条目 | 上游改动文件 | 验证 |
| --- | --- | --- | --- | --- |
| S120 | `feat(privacy): keep phone number unshared by default when adding contacts` | G16 | `boxes/peers/edit_contact_box.cpp` | `test_nagram`；添加联系人框初始勾选的开 / 关对照 |
| S121 | `feat(privacy): copy and save protected content` | G13 | `history/history_item.cpp`、`history/history_inner_widget.cpp`、`history/view/history_view_list_widget.cpp`、`history/view/history_view_context_menu.cpp`、`media/view/media_view_overlay_widget.cpp`、`window/window_session_controller.cpp`、`info/media/info_media_provider.cpp`、`info/media/info_media_list_widget.cpp`、`iv/iv_rich_message_html_export.cpp` | `test_nagram`（复制判定全组合）；受保护群组、频道、私聊在两套消息视图、媒体查看器、共享媒体页的检查；转发、限时、付费媒体的反向检查 |
| S122 | `feat(privacy): ignore content restrictions` | G14 | `data/data_peer.cpp`、`window/window_session_controller.cpp` | `test_nagram`；受限会话与消息的开 / 关对照，会话内关闭开关 |
| S123 | `feat(privacy): show sensitive media without the warning` | G15 | `data/data_peer.cpp`、`main/main_session.cpp`、`info/media/info_media_provider.cpp` | `test_nagram`（敏感判定全组合）；敏感媒体样本、离线启动后联网、双账号能力不同 |

提交正文按实施计划第 2 节第 3 条列出条目编号、上游改动文件和验证方式。`nagram/privacy/protection.*`、`protection_model.cpp` 与 `tests/test_privacy.cpp` 随第一个使用者 S120 建立，后续提交各自追加函数和用例，不单独提交没有使用者的代码。

S123 完成后做一次 V2：rebase 到最新上游 `dev`、完整构建、三平台 CI；随后更新 `settings-page.md`（G13–G16）、`upstream-hooks.md`（第 2.7 节新增四行）、`implementation-plan.md`（P3 小节）和 `design.md` 的里程碑状态与上游改动统计。这些文档更新并入各自条目的提交。

## 8. 需要维护者决定的问题

1. **G13 是否同时解除截屏保护。** 上游对受保护会话有两处窗口截屏保护（`SessionController::setupScreenshotProtection()` 与 `OverlayWidget::contentNeedsScreenshotProtection()`）。建议随 G13 一起解除：复制和保存已放开，单独保留截屏保护没有实际意义，也避免再加一个开关。如果希望保留，从 S121 中去掉这两处挂钩即可，其余不受影响。
2. **G13 是否放开 Nagram 消息截图（E21）。** `nagram/snapshot/snapshot.cpp` 目前用 `allowsForward()` 排除受保护消息。建议保持不变，G13 只覆盖上游已有的复制 / 保存动作；若要放开，需要把该判断拆成“可转发”与“可本机复制”两部分。
3. **G15 的能力门槛是否过严。** 本设计按需求原文只在 `canChange` 为真且不需要年龄验证时生效。在这种条件下用户也可以直接打开上游的“停用过滤”，G15 的差别只是不改服务端设置、不影响其他设备。来源端（iOS `skipSensitiveContentWarning`）没有这个门槛。如果维护者认为应在 `canChange` 为假时也生效，需要先修改 `requirements.md` F10 的约束，再去掉判定中的 `canChange` 项；年龄验证的门槛建议无论如何保留。
4. **作用域。** 四项都按设备作用域设计（可导出、所有账号共用）。如果希望 G14、G15 按账号分别设置，改为账号作用域后不可导出，`Compute` 与 `IgnoreSensitiveMark` 已有 `session` 参数，挂钩位置不变。建议保持设备作用域。
