# P3-06 网络调优专项设计

本文件是 [功能需求](requirements.md) 中 P3-06 功能包（功能族 [F15](requirements.md#f15)）的专项设计。架构约束见 [设计与路线](design.md) 第 3 节，提交规则与检查清单见 [分步实施计划](implementation-plan.md) 第 2、3、5 节，设置页规则见 [设置页设计](settings-page.md)。路径除注明外相对 `Telegram/SourceFiles/`。

文中的上游常量、函数和行为均来自当前工作副本的源码；实现每一步时以当时的上游代码重新确认。

## 1. 范围

### 1.1 本包条目

| 目录编号 | 功能 | 目录中的来源键与默认值 |
| --- | --- | --- |
| I046 | 上传加速 | `nagram.uploadSpeedBoost`，`Bool` / `false` |
| I047 | 下载加速（档位） | `nagram.downloadSpeedBoost`，`String` / `"none"` |
| N083 | 使用系统 DNS | `useSystemDNS`，`Bool` / `false` |
| N084 | 自定义 DoH | `customDoH`，`String` / `""` |
| A191 | 自定义 IP 策略 | `CustomIpStrategy`，`Int` / `0` |
| A002 | 禁用请求备用服务器地址 | `disableSecondAddress`，`Bool` / `true` |

旧实现（`main` 分支 `nagram/nagram_settings.h`）中没有这六项的键，不存在沿用旧键名的问题。

### 1.2 不做的事

- 已搁置的细项不设计：增强文件加载器（N123）、VPN 时禁用代理（A048）、代理自动切换（N087）、公共代理（N009）、代理订阅（N010）、VMess/SS/SSR/Trojan 协议。
- 不改下载分片大小。上游 `storage/download_manager_mtproto.h` 的 `kDownloadPartSize`（128 KB）注释说明：CDN 重定向后的哈希校验只支持固定分片。该常量还被 `storage/file_download_mtproto.cpp`、`data/data_media_preload.cpp`、`media/streaming/media_streaming_loader.h`（`Loader::kPartSize`）共用。
- 不改服务端限速的处理。`FLOOD_WAIT_*` 与 `FLOOD_PREMIUM_WAIT_*` 仍由 `mtproto/mtp_instance.cpp` 的延迟队列（`_delayedRequests`、`checkDelayedRequests`）处理，`nonPremiumDelayedRequests` 的提示保持不变。加速档位只调整客户端自己的并发与起步参数。
- 不改上游超时退让：下载的 `sessionTimedOut` / `removeSession`，上传的 `removeDcIndex`、`kSlowRequestThreshold`、`kWaitForNormalizeTimeout` 全部保留。
- 不新增代理协议、不新增 DNS 报文编解码（RFC 8484 二进制格式），不改 `lib_ui`。
- 不写入上游已有的设置（`Core::SettingsProxy::tryIPv6`、实验项 `prefer-ipv6`）。Nagram 只在读取处覆盖，恢复默认后上游设置原样生效。
- 不把选项做成账号作用域：`MTP::Instance` 在登录前就要联网，上游代理设置（`Core::SettingsProxy`）也是本机级。

## 2. 上游现状

### 2.1 下载（`storage/download_manager_mtproto.cpp`）

| 常量 | 上游值 | 含义 |
| --- | --- | --- |
| `kDownloadPartSize` | 128 KB | 固定分片，不可调 |
| `kStartSessionsCount` | 1 | 每个 DC 起步会话数，也是超时回收的下限 |
| `kMaxSessionsCount` | 8 | 每个 DC 会话数上限 |
| `kStartWaitedInSession` | 4 片（512 KB） | 单会话在途窗口起步值 |
| `kMaxWaitedInSession` | 16 片（2 MB） | 单会话在途窗口上限 |
| `kRetryAddSessionSuccesses` | 3 | 每个会话累计成功数达到后才考虑加会话 |
| `kRetryAddSessionTimeout` | 8 秒 | 回收会话后再次增加前的等待基数 |
| `kRemoveSessionAfterTimeouts` | 4 | 累计超时次数达到后回收一个会话 |
| `kBadRequestDurationThreshold` | 8 秒 | 单请求超过即按超时处理 |
| `kKillSessionTimeout` | 15 秒 | 空闲后停止会话 |

稳态上限是每个 DC 8 个会话 × 16 片 = 128 个在途请求（16 MB）。增长过程在 `DownloadManagerMtproto::requestSucceeded`：窗口用满且成功时加一片；全部会话各有足够成功数、没有未消化的超时，才 `dc.sessions.emplace_back()`。回退在 `sessionTimedOut` → `removeSession`。`killSessions(dcId)` 空闲时重置各会话的窗口，但保留会话数量。流媒体（`media/streaming/media_streaming_loader_mtproto.cpp`）与媒体预加载用的是同一个管理器。

硬上限：

- 会话下标必须小于 `mtproto/core_types.h` 的 `kMaxMediaDcCount`（`0x10`，即 16）。`mtproto/facade.h` 的 `downloadDcId` / `uploadDcId` 有 `Expects(index < kMaxMediaDcCount)`；下载占用 shift `0x10–0x1F`，上传占用 `0x20–0x2F`，超过会与对方区间重叠。
- 文件会话线程数是 `mtproto/mtp_instance.cpp` 中 `_fileSessionThreads.resize(2 * std::max(idealThreadPoolSize / 2, 1))`，按下标取模复用，不构成硬限制，但会话多于线程时不再增加并行度。
- 官方接口约束（<https://core.telegram.org/api/files>）：`upload.getFile` 的 `offset`、`limit` 须被 4 KB 整除，1 MB 须被 `limit` 整除。

### 2.2 上传（`storage/file_upload.cpp`）

| 常量 | 上游值 | 含义 |
| --- | --- | --- |
| `kDocumentUploadPartSize0`–`4` | 32 / 64 / 128 / 256 / 512 KB | `Uploader::Entry::setDocSize` 按文件大小选择：小于 1 MB 用 32 KB，不超过 32 MB 用 64 KB，之后取使分片数不超过上限的最小档 |
| `kDocumentMaxPartsCountDefault` | 4000 | 分片数上限 |
| `kMaxUploadPerSession` | 1 MB | 单会话在途字节上限（`sendDocPart`、`sendSlicedPart`） |
| `kMaxSessionsCount` | 8 | 会话数上限（`canAddDcIndex`） |
| `kUploadRequestInterval` | 250 毫秒 | `maybeSend` 的补发间隔 |
| `kAcceptAsFastIfTotalAtLeast` | 512 KB | 单轮每会话的填充阈值，也是“快请求”判定的最小字节数 |
| `kFastRequestThreshold` / `kSlowRequestThreshold` | 1 秒 / 8 秒 | 全部会话都“快”才加会话；“慢”则回收一个 |

上传始终发往主 DC（`MTP::uploadDcId(index)`）。会话从 0 个开始按需增加。

硬上限：会话下标同样受 `kMaxMediaDcCount` 限制；官方接口约束 `part_size % 1024 = 0`、`524288 % part_size = 0`，即分片最大 512 KB，文档同时建议用 512 KB 以减少协议开销。上游的 512 KB 已经是协议上限。

### 2.3 域名解析

直连不做 DNS：DC 地址是 `mtproto/mtproto_dc_options.cpp` 中的 IP 字面量。会解析域名的只有下面几处。

| 场景 | 上游做法 |
| --- | --- |
| SOCKS5 / MTProto 代理的主机名是域名（`ProxyData::tryCustomResolve`） | `AbstractConnection::Create`（`mtproto/connection_abstract.cpp`）包一层 `ResolvingConnection`。子连接先按域名连接，由 Qt 走系统解析；同时 `Instance::Private::resolveProxyDomain`（`mtproto/mtp_instance.cpp`）让 `DomainResolver` 发 DoH。结果到达后 `ResolvingConnection::domainResolved` 在 `_ipIndex < 0` 时 `refreshChild()` 改用解析出的 IP |
| HTTP 代理 | `ToNetworkProxy` 交给 Qt，系统解析 |
| 备用服务器地址（special config） | `SpecialConfigRequest` 用 DoH 查 `txtDomainString` 的 TXT 记录，另有 Firestore 请求 |
| HTTP 时间同步 | `Instance::Private::syncHttpUnixtime` 复用 `SpecialConfigRequest`，只取响应的 `Date` 头 |

上游已有 DoH：`mtproto/details/mtproto_domain_resolver.cpp` 的 `DomainResolver::resolve` 生成尝试列表（`dns.google.com`、`mozilla.cloudflare-dns.com` 以及经 `DnsDomains()` 的域名前置），`performRequest` 按 `Type::Google`（`/resolve`）和 `Type::Mozilla`（`/dns-query`，`accept: application/dns-json`）发 JSON 接口请求，`ParseDnsResponse` 解析 `Answer[].data` 与 `TTL`，TTL 钳制在 `kMinTimeToLive`（10 秒）到 `kMaxTimeToLive`（300 秒）。请求间隔 `kSendNextTimeout`（800 毫秒），`QNetworkAccessManager` 固定 `NoProxy`。DoH 端点自身的主机名由 Qt 走系统解析。

### 2.4 IP 版本

- 开关：`Core::SettingsProxy::tryIPv6`（`core/core_settings_proxy.cpp`，默认 `!Platform::IsWindows()`），界面在代理设置框（`boxes/connection_box.cpp`，文案 `lng_connection_try_ipv6`）。`ProxiesBoxController::setTryIPv6` 写入后调用 `_account->mtp().restart()`。
- 实验项：`prefer-ipv6`（`mtproto/session_private.cpp` 的 `OptionPreferIPv6`，在 `settings/settings_experimental.cpp` 列出）。
- 读取：`Session::refreshOptions`（`mtproto/session.cpp`）中 `useIPv4` 写死为 `true`，`useIPv6 = settings.tryIPv6()`；`Session::restart` 会重新调用它。
- 生效：`SessionPrivate::connectToServer` 按 `useIPv4` / `useIPv6` 跳过地址族；临时 DC（special config 给出的地址）强制只用 IPv4。`SessionPrivate::appendTestConnection` 的优先级是 IPv6 为 0（开启实验项为 2）、IPv4 为 1，再按 TCP 和带密钥各加 1；先连上的低优先级连接等待 `kWaitForBetterTimeout`（2 秒）。
- 代理测速：`MTP::StartProxyCheck`（`mtproto/proxy_check.cpp`）接收 `tryIPv6` 参数，IPv4 总是测。

上游已覆盖“仅 IPv4”“优先 IPv4”“优先 IPv6”三种，缺“仅 IPv6”。

### 2.5 备用服务器地址

`ConfigLoader::refreshSpecialLoader`（`mtproto/config_loader.cpp`）在 `_proxyEnabled || _instance->isKeysDestroyer()` 时不创建 `SpecialConfigRequest`；`sendSpecialRequest` 在 `_proxyEnabled` 时同样放弃。也就是说，启用代理时上游本来就不请求备用地址。`ConfigLoader::enumerate` 每 `kEnumerateDcTimeout`（8 秒）调用一次 `refreshSpecialLoader`。

## 3. 逐项结论

Nagram 侧代码放在新目录 `nagram/network/`：`model.h/.cpp` 是纯逻辑（档位表、IP 策略映射、DoH 地址校验与请求地址拼接、IP 列表排序），只依赖 Qt Core，进入 `test_nagram`；`runtime.h/.cpp` 读取注册表并向上游提供下文的取值函数；`options.h` 定义注册表条目。

会话线程也要读的值（IP 策略、系统 DNS）由 `runtime.cpp` 在主线程订阅选项后写入 `std::atomic`，会话线程只读原子值，不直接访问 `Core::Settings`。

### 3.1 I047 下载加速：可实现，档位数值须经基准确认

挂钩全部在 `storage/download_manager_mtproto.cpp`，方式为替换：

| 位置 | 改动 |
| --- | --- |
| `DcSessionBalanceData::DcSessionBalanceData()` | `maxWaitedAmount` 的初值由 `kStartWaitedInSession` 改为 `Nagram::Network::DownloadStartWindow(kStartWaitedInSession)` |
| `DcBalanceData::DcBalanceData()` | `sessions` 的初始数量由 `kStartSessionsCount` 改为 `Nagram::Network::DownloadStartSessions(kStartSessionsCount)` |
| `requestSucceeded` 中 `dc.sessions.size() == kMaxSessionsCount` | 改为 `>= Nagram::Network::DownloadMaxSessions(kMaxSessionsCount)` |
| `removeSession` 中的占位值 `kMaxWaitedInSession * kMaxSessionsCount`（两处） | 会话数因子改用同一个取值函数，保证占位值仍大于任何真实在途量 |

`sessionTimedOut` 与 `removeSession` 里作为回收下限的 `kStartSessionsCount` 不改，网络变差时仍可退到 1 个会话。`kMaxWaitedInSession` 不改。

档位候选值（`none` 即上游值）：

| 参数 | `none` | `balanced` | `fast` | 硬上限与依据 |
| --- | --- | --- | --- | --- |
| 起步会话数 | 1 | 2 | 4 | 不超过会话上限 |
| 会话上限 | 8 | 8 | 12 | 16：`kMaxMediaDcCount`，见 2.1 |
| 起步窗口 | 4 片 | 8 片 | 16 片 | 不超过 `kMaxWaitedInSession`（16 片） |
| 窗口上限 | 16 片 | 16 片 | 16 片 | 不调 |
| 分片大小 | 128 KB | 128 KB | 128 KB | 不可调，见 1.2 |

`balanced` 只缩短爬升过程，稳态与上游相同；`fast` 另把会话上限提到 12。`model.cpp` 对所有档位值做钳制（会话数 1–16，窗口 4–16 片），非法的档位字符串按 `none` 处理并写日志。

基准要求（需求规定数值在 Qt 基准后确定）：

1. 样本：约 10 MB、200 MB、1.5 GB 三个文件，放在测试账号的收藏夹；另测一个长视频的流媒体起播与拖动。
2. 线路：直连、SOCKS5、MTProto 代理各一组；每组每档至少三次，清缓存后下载。
3. 记录：总耗时、调试日志中 `Download (dc,index) ... adding` / `removing` / `session timed-out` 的次数、`FLOOD_WAIT` 与 `FLOOD_PREMIUM_WAIT` 的次数。
4. 判定：某档的超时回收次数或限速次数明显多于 `none`，或耗时没有改善，就下调该档候选值；`fast` 的会话上限在 9–16 之间取满足条件的最大值，都不满足则保持 8。
5. 结果表和最终取值写回本节，再提交。

与上游一致的保证：条目标记“重启后生效”，`runtime.cpp` 在首次读取时把档位固化为进程内只读快照。值为 `none` 时三个取值函数原样返回传入的上游常量。运行中切换档位不影响已建立的会话与窗口，避免传输中途改参数。

### 3.2 I046 上传加速：可实现，取值须经基准确认

目录中的类型是布尔值，保持一个开关。候选做法只有一项：1 MB 及以上的文件直接使用 512 KB 分片（协议上限，也是官方文档建议值），不再按 64 / 128 / 256 KB 逐档选择。小于 1 MB 的文件、照片和缩略图的切片（`sendSlicedPart`）不变。

挂钩在 `storage/file_upload.cpp` 的 `Uploader::Entry::setDocSize`：赋值 `docSize` 之后加一个短块，`Nagram::Network::UploadPartSize(size)` 返回非零时调用上游 `setPartSize` 并返回。512 KB 得到的分片数是所有档中最少的，分片数上限（`kDocumentMaxPartsCountDefault`）的判断结果与上游最后一档相同。

以下参数保持上游值，基准显示有必要时再单独提出：`kMaxSessionsCount`（8，硬上限 16）、`kMaxUploadPerSession`（1 MB）、`kUploadRequestInterval`（250 毫秒）、`kAcceptAsFastIfTotalAtLeast`（512 KB，同时参与加会话判定，改动会影响退让逻辑）。

基准要求：同 3.1 的样本与线路，上传到收藏夹；记录总耗时、`Uploader: Added dc index` 与 `Slow request, removing dc index` 的次数、限速次数。开启后没有改善或回收次数增多，则本项不交付，条目不进入注册表。

与上游一致的保证：同样标记“重启后生效”并取快照；关闭时 `UploadPartSize` 返回 0，`setDocSize` 走上游原分支。分片大小在单个文件内不变（`Entry` 创建时确定），不会触发 `FILE_PART_SIZE_CHANGED`。

### 3.3 A191 IP 策略：可实现

选项值：0 跟随 Telegram，1 仅 IPv4，2 优先 IPv4，3 优先 IPv6，4 仅 IPv6。映射（`model.cpp`，输入为策略、上游 `tryIPv6`、上游实验项）：

| 策略 | `useIPv4` | `useIPv6` | 优先 IPv6 |
| --- | --- | --- | --- |
| 0 | `true`（上游值） | `tryIPv6` | 实验项的值 |
| 1 | `true` | `false` | `false` |
| 2 | `true` | `true` | `false` |
| 3 | `true` | `true` | `true` |
| 4 | `false` | `true` | 不适用 |

挂钩：

| 上游位置 | 改动 | 方式 |
| --- | --- | --- |
| `mtproto/session.cpp`，`Session::refreshOptions` | `useIPv4`、`useIPv6` 两行改为 `Nagram::Network::UseIPv4(true)`、`Nagram::Network::UseIPv6(settings.tryIPv6())` | 替换 |
| `mtproto/session_private.cpp`，`SessionPrivate::appendTestConnection` | `OptionPreferIPv6.value()` 改为 `Nagram::Network::PreferIPv6(OptionPreferIPv6.value())`（会话线程，读原子值） | 替换 |
| `mtproto/mtp_instance.cpp`，`Instance::Private::resolveProxyDomain` 的回调 | 传给 `applyDomainIps` 的 `ips` 先经 `Nagram::Network::OrderIps(ips)` 过滤与排序 | 替换 |
| `boxes/connection_box.cpp`（两处）、`core/proxy_rotation_manager.cpp`（一处）调用 `MTP::StartProxyCheck` 的 `tryIPv6` 实参 | 包一层 `Nagram::Network::UseIPv6(...)` | 替换 |

`SessionPrivate::connectToServer` 已支持 `useIPv4 == false`（`skipAddress` 分支），不需要改。

切换即时生效：设置页写入后对每个账号的 `MTP::Instance` 调用 `restart()`，与 `ProxiesBoxController::setTryIPv6` 的做法相同；`Session::restart` 会重新执行 `refreshOptions`。

限制，写入条目说明：

- 临时 DC（备用服务器地址）在上游强制只用 IPv4，策略 4 下这些地址不会被使用。
- MTProto 代理的出口由代理决定，策略只影响代理域名解析结果的取舍；开启 N083 时代理域名交给系统解析，策略不再作用于代理主机。
- `StartProxyCheck` 没有跳过 IPv4 的参数，策略 4 下代理测速仍会测 IPv4。
- 策略 4 在没有 IPv6 的网络上无法连接，不自动回退到 IPv4；界面保持“连接中”，设置页离线可用，可以改回。

与上游一致的保证：值为 0 时三个函数原样返回传入值，`OrderIps` 原样返回列表；不写入 `tryIPv6` 与实验项。

### 3.4 N083 使用系统 DNS：可实现

桌面端的含义限定为：代理主机名是域名时只用系统解析，不再向内置 DoH 端点查询。直连没有域名解析，本项对直连无影响。

挂钩：`mtproto/connection_abstract.cpp` 的 `AbstractConnection::Create`，条件 `proxy.tryCustomResolve()` 增加 `&& !Nagram::Network::UseSystemDns()`（会话线程，读原子值）。开启后不创建 `ResolvingConnection`，`TcpConnection` / `HttpConnection` 按域名连接，由 Qt 调用系统解析，也不再触发 `resolveProxyDomain`。不需要新写解析代码。

切换后对每个账号的 `MTP::Instance` 调用 `restart()`，使现有连接按新方式重建。

不覆盖的部分：special config 的 TXT 查询和时间同步仍走 DoH（受 N084、A002 控制）。上游代码没有使用 `QDnsLookup`，各平台自带 Qt 是否启用该特性未核实，本包不引入系统 TXT 查询。

与上游一致的保证：关闭时条件与上游相同。`ProxyData::resolvedIPs` 是否已有缓存不影响结果，因为不经过 `ResolvingConnection` 就不会使用它。

### 3.5 N084 自定义 DoH：可实现，仅支持 JSON 接口

复用上游的请求与解析。填写地址后，凡是上游使用内置 DoH 端点的地方都只请求这个地址：代理域名解析（N083 关闭时）、special config 的 TXT 查询、时间同步。Firestore 请求不属于 DNS，不受影响。

格式限制：上游 `ParseDnsResponse` 解析的是 JSON 接口（`application/dns-json`）。自定义端点必须支持 `GET <地址>?name=<域名>&type=<类型>` 并返回同样结构；只支持 RFC 8484 二进制报文的服务不可用。

地址校验（`model.cpp`，系统边界）：必须是 `https`；主机非空；不带用户名、密码、查询串和片段；长度不超过 256；单行。不合格时保存失败并在输入框提示，不写入。空字符串表示使用内置端点。

挂钩：

| 上游位置 | 改动 | 方式 |
| --- | --- | --- |
| `mtproto/details/mtproto_domain_resolver.cpp`，`DomainResolver::resolve(const AttemptKey &)` | 构造完 `attempts` 后，自定义地址非空时替换为单个 `{ Type::Mozilla, <地址> }` | 短块 |
| 同文件，`DomainResolver::performRequest` 的 `Type::Mozilla` 分支 | `url.setHost` / `url.setPath` 两行改为 `Nagram::Network::SetDohEndpoint(url, attempt.data)`：`attempt.data` 是完整地址时取其主机、端口和路径，否则按上游写法设置主机和 `/dns-query` | 替换 |
| `mtproto/special_config_request.cpp`，`SpecialConfigRequest` 构造函数 | 自定义地址非空时，`_attempts` 中两个 DNS 项（`Type::Google`、`Type::Mozilla`）换成一个指向自定义地址的 `Type::Mozilla` 项；随后的 `shuffle(0, 2)` 相应跳过 | 短块 |
| 同文件，`SpecialConfigRequest::performRequest` 的 `Type::Mozilla` 分支 | 同上，改用 `SetDohEndpoint` | 替换 |

两个类都运行在主线程，直接读注册表。短块需要访问类的私有类型 `Attempt` / `Type`，所以留在上游文件，在提交正文说明。

行为约定：

- 自定义端点失败时不改用内置端点。代理域名解析不受阻：上游的 `ResolvingConnection` 本来就先按域名（系统解析）建立子连接，DoH 结果只用于随后换成 IP。special config 则在该次查询中拿不到 TXT 结果，Firestore 途径照常。失败写入日志（上游已有 `Resolve Error` / `Config Error` 日志）。
- 修改地址只影响之后发出的请求；`DomainResolver::_cache` 中已有的结果按 TTL 自然过期。
- 自定义端点的主机名由 Qt 走系统解析，与上游解析 `dns.google.com` 的方式相同。
- 请求仍带上游的 `random_padding` 参数与 `DnsUserAgent()`。

与上游一致的保证：地址为空时两个短块不执行，`SetDohEndpoint` 对不含协议的 `attempt.data` 的结果与上游两行相同（单元测试覆盖）。

### 3.6 A002 不请求备用服务器地址：可实现

上游在启用代理时已经不请求备用地址（见 2.5），本项把同一条件扩展到直连。

挂钩（`mtproto/config_loader.cpp`，读取）：

- `ConfigLoader::refreshSpecialLoader` 的条件增加 `|| Nagram::Network::BackupAddressesDisabled()`。
- `ConfigLoader::sendSpecialRequest` 的 `if (_proxyEnabled)` 增加同一条件。

`ConfigLoader` 在主线程运行，直接读注册表。开启后最迟在下一次 `enumerate`（8 秒）或下一次 `sendSpecialRequest` 时清除 `_specialLoader`；已经通过 `constructAddOne` 加入的临时地址不主动移除。不需要重启连接。

不覆盖的部分：时间同步（`syncHttpUnixtime`）不是备用地址请求，仍会访问 DoH 端点取 `Date` 头；是否一并关闭见第 9 节。

默认值：来源端默认开启，桌面端按设置页规则 5 默认关闭，默认行为与上游一致。关闭时两处条件与上游相同。

## 4. 注册表条目

新增 `Category::Network`（`nagram/core/options.h` 的枚举，`nagram/settings/config.cpp` 的 `CategorySection` 增加对应分支）。全部为本机作用域；按注册表现有规则，本机作用域且非隐藏的条目自动可导出。

| 键 | 类型 | 默认值 | 作用域 | 分栏 | 可导出 | 需重启 | 校验 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `nagram.downloadSpeedBoost` | `QString` | `"none"` | D | 网络 | 是 | 是 | `none` / `balanced` / `fast` |
| `nagram.uploadSpeedBoost` | `bool` | `false` | D | 网络 | 是 | 是 | — |
| `nagram.ipStrategy` | `int` | `0` | D | 网络 | 是 | 否 | 0–4 |
| `nagram.useSystemDns` | `bool` | `false` | D | 网络 | 是 | 否 | — |
| `nagram.customDoh` | `QString` | `""` | D | 网络 | 是 | 否 | 空或通过 3.5 的地址校验 |
| `nagram.disableBackupAddresses` | `bool` | `false` | D | 网络 | 是 | 否 | — |

`nagram.customDoh` 不含凭据（校验拒绝用户名、密码和查询串），可以导出。导入配置时沿用现有流程：先校验，再逐键写入并统一通知；`runtime.cpp` 订阅 `nagram.ipStrategy` 与 `nagram.useSystemDns` 的变化并重启连接，因此导入与在设置页修改的效果相同。

## 5. 设置页

新增分栏“网络”，编号前缀 `K`，在首页排在“规则”之后、“配置管理”之前；第一个条目提交时分栏才出现。上游代理设置（代理列表、“尝试通过 IPv6 连接”）仍在上游入口，本页不重复。

**连接**

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| K01 | IP 版本 | 选项：跟随 Telegram / 仅 IPv4 / 优先 IPv4 / 优先 IPv6 / 仅 IPv6 | D | 修改后重新连接。选择“仅 IPv6”时，网络不支持 IPv6 将无法连接。非默认值优先于代理设置中的“尝试通过 IPv6 连接”。 |
| K02 | 不请求备用服务器地址 | 开关 | D | 无法连接时不再通过 DNS 和云服务查询备用地址。使用代理时 Telegram 本来就不请求。 |

**域名解析**

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| K03 | 用系统 DNS 解析代理域名 | 开关 | D | 修改后重新连接。只影响主机名为域名的 SOCKS5 和 MTProto 代理；直连不需要解析域名。 |
| K04 | 自定义 DoH 地址 | 文本 | D | 留空使用 Telegram 内置的 DoH 服务。地址必须以 https 开头，并支持 JSON 查询接口。该地址不可用时不会改用内置服务。 |

**文件传输**

| 编号 | 标题 | 形式 | 作用域 | 说明 |
| --- | --- | --- | --- | --- |
| K05 | 下载加速 | 选项：跟随 Telegram / 均衡 / 快速 | D | 重启后生效。增加同时下载的连接数；速度仍受 Telegram 服务端限制，也作用于视频边下边播。 |
| K06 | 上传加速 | 开关 | D | 重启后生效。大于 1 MB 的文件使用更大的分片；速度仍受 Telegram 服务端限制。 |

K05、K06 修改后弹出上游重启确认框（`Nagram::ShowRestartPrompt`）。每个条目按现有做法注册搜索关键词（英文标题词）。

## 6. 文案键

英文在 `Telegram/Resources/langs/nagram/nagram.strings`，简繁在同目录 `zh-hans.strings`、`zh-hant.strings`，三个文件同时提交。

| 键 | 英文 | 简体 |
| --- | --- | --- |
| `lng_nagram_network` | Network | 网络 |
| `lng_nagram_network_connection` | Connection | 连接 |
| `lng_nagram_network_dns` | Domain resolution | 域名解析 |
| `lng_nagram_network_transfer` | File transfer | 文件传输 |
| `lng_nagram_ip_strategy` | IP version | IP 版本 |
| `lng_nagram_ip_strategy_ipv4_only` | IPv4 only | 仅 IPv4 |
| `lng_nagram_ip_strategy_prefer_ipv4` | Prefer IPv4 | 优先 IPv4 |
| `lng_nagram_ip_strategy_prefer_ipv6` | Prefer IPv6 | 优先 IPv6 |
| `lng_nagram_ip_strategy_ipv6_only` | IPv6 only | 仅 IPv6 |
| `lng_nagram_ip_strategy_about` | Reconnects after a change. With IPv6 only, a network without IPv6 can't connect. A non-default value overrides “Try connecting through IPv6” in proxy settings. | 见 K01 说明 |
| `lng_nagram_disable_backup_addresses` | Don't request backup server addresses | 不请求备用服务器地址 |
| `lng_nagram_disable_backup_addresses_about` | Stops looking up backup addresses through DNS and cloud services when the connection fails. Telegram already skips this while a proxy is used. | 见 K02 说明 |
| `lng_nagram_use_system_dns` | Resolve proxy hosts with system DNS | 用系统 DNS 解析代理域名 |
| `lng_nagram_use_system_dns_about` | Reconnects after a change. Affects only SOCKS5 and MTProto proxies with a domain name; direct connections don't resolve domains. | 见 K03 说明 |
| `lng_nagram_custom_doh` | Custom DoH address | 自定义 DoH 地址 |
| `lng_nagram_custom_doh_about` | Leave empty to use Telegram's built-in DoH services. The address must start with https and support the JSON query API. Built-in services are not used when it is unavailable. | 见 K04 说明 |
| `lng_nagram_custom_doh_placeholder` | https://… | https://… |
| `lng_nagram_custom_doh_invalid` | Enter an https address without a query string. | 请输入不带查询参数的 https 地址。 |
| `lng_nagram_download_speed_boost` | Download acceleration | 下载加速 |
| `lng_nagram_download_speed_boost_balanced` | Balanced | 均衡 |
| `lng_nagram_download_speed_boost_fast` | Fast | 快速 |
| `lng_nagram_download_speed_boost_about` | Applies after restart. Uses more simultaneous connections; speed is still limited by Telegram servers. Also affects video streaming. | 见 K05 说明 |
| `lng_nagram_upload_speed_boost` | Upload acceleration | 上传加速 |
| `lng_nagram_upload_speed_boost_about` | Applies after restart. Files larger than 1 MB use bigger parts; speed is still limited by Telegram servers. | 见 K06 说明 |

“跟随 Telegram”“默认”“重启后生效”复用已有的 `lng_nagram_preview_follow`、`lng_nagram_restart_required` 等键。繁体译文在实现时与简体同步给出，`test_nagram` 的三语一致性检查必须通过。

## 7. 测试

### 7.1 单元测试（`nagram/tests/test_network.cpp`，加入 `test_nagram`）

- 注册表：六个键的默认值、类型、校验（非法档位字符串、越界的 IP 策略、非法 DoH 地址被拒绝）、导出与导入往返。
- 下载档位：三档的取值；`none` 原样返回传入常量；所有档位的会话数不超过 16、起步值不超过上限、窗口在 4–16 片之内；未知档位按 `none`。
- 上传分片：关闭时返回 0；开启时小于 1 MB 返回 0，其余返回 512 KB；返回值满足 `% 1024 == 0` 且整除 524288。
- IP 策略映射：5 种策略 × 上游 `tryIPv6` 两种取值 × 实验项两种取值，共 20 组；策略 0 的输出等于输入。
- `OrderIps`：各策略下对混合 IPv4/IPv6 列表的过滤与排序；策略 0 原样返回；过滤后为空时返回空列表。
- DoH 地址：校验的接受与拒绝样例（`http`、带用户名、带查询串、带片段、超长、含换行、空主机）；`SetDohEndpoint` 对内置主机名的结果与上游写法相同，对自定义地址保留端口与路径；查询参数拼接。

### 7.2 手动检查

基础场景：每个条目默认值下的行为与未改动的构建对照；开启、恢复默认后再次对照；英、简、繁文案与搜索跳转。

| 维度 | 场景 |
| --- | --- |
| 断线重连 | 下载、上传进行中断网再恢复，三个档位下都能续传，日志中会话回收与重建正常；IP 策略切换后各账号重新连接；开启 K02 后断网重连，调试日志中不再出现 `Special endpoint received` |
| 跨账号 | 两个账号（可位于不同 DC）同时下载，各 DC 的会话数分别不超过档位上限；切换 K01、K03 后两个账号都重连；添加账号时的登录页也遵守 IP 策略 |
| 异步竞态 | 传输中修改 K05、K06，当前传输参数不变，重启后才变化；DoH 请求在途时修改 K04 或切换 K03，不崩溃，旧结果按 TTL 过期；配置请求进行中切换 K02；连接建立过程中快速来回切换 K01 |
| 回退 | `fast` 档下用限速或丢包线路，确认 `session timed-out` 后会话数下降并可降到 1；上传出现慢请求时会话被回收；所有条目恢复默认后重启，行为与上游一致 |
| 外部能力不可用 | K04 填不可达地址：域名代理仍可通过系统解析连接，日志有解析失败记录，没有对内置端点的请求；K04 填只支持二进制格式的服务：结果同上；K01 选“仅 IPv6”而网络无 IPv6：保持连接中，改回后恢复；K03 开启且系统 DNS 无法解析代理域名：保持连接中，不发起 DoH |
| 平台 | Windows 上游默认关闭 `tryIPv6`，核对策略 0 的行为不变；三平台 CI 通过 |

网络请求的核对用调试日志和抓包（只看目的主机），不记录账号数据。

## 8. 提交拆分

按小分组各一个提交；分栏、`Category::Network` 与 `nagram/network/` 的公共部分随第一个提交。步骤编号暂用 `S160` 起，并入 [分步实施计划](implementation-plan.md) 时再定。每个提交同时更新 `settings-page.md`、`upstream-hooks.md` 与 `implementation-plan.md` 的对应行。

| 步骤 | 提交 | 条目 | 上游改动文件 | 验证 |
| --- | --- | --- | --- | --- |
| S160 | `feat(network): connection options` | K01、K02（A191、A002） | `mtproto/session.cpp`、`mtproto/session_private.cpp`、`mtproto/mtp_instance.cpp`、`mtproto/config_loader.cpp`、`boxes/connection_box.cpp`、`core/proxy_rotation_manager.cpp` | V1；`test_nagram`（策略映射、`OrderIps`、注册表）；手动：五种策略下的连接、双账号重连、K02 开启后的断网重连 |
| S161 | `feat(network): domain resolution` | K03、K04（N083、N084） | `mtproto/connection_abstract.cpp`、`mtproto/details/mtproto_domain_resolver.cpp`、`mtproto/special_config_request.cpp` | V1；`test_nagram`（地址校验、端点拼接）；手动：域名 SOCKS5 与 MTProto 代理、不可达端点、在途切换 |
| S162 | `feat(network): transfer acceleration` | K05、K06（I047、I046） | `storage/download_manager_mtproto.cpp`、`storage/file_upload.cpp` | 先完成 3.1、3.2 的基准并把结果与最终取值写回本文件；V1；`test_nagram`（档位表、分片选择）；手动：三档下载、上传、流媒体、限速线路回退、重启后生效 |

提交正文示例（S160）：`K01, K02; mtproto/session.cpp, mtproto/session_private.cpp, mtproto/mtp_instance.cpp, mtproto/config_loader.cpp, boxes/connection_box.cpp, core/proxy_rotation_manager.cpp; test_nagram + manual checks for each IP strategy, two accounts, reconnect with backup addresses disabled`。

S162 中若基准否定了上传加速，该提交只含 K05，K06 不进入注册表与设置页，并在本文件 3.2 记录结论。包内最后一个提交之后执行 V2（rebase 到最新上游、完整构建、三平台 CI、隔离数据目录冒烟），并在 `design.md` 更新状态与上游改动统计。

上游改动预计 11 个文件，均为已有判断中的条件、取值替换或 10 行以内的短块；不改上游头文件，不新增 `friend`。

## 9. 需要维护者决定的问题

1. **`fast` 档是否允许超过上游的 8 个下载会话。** 候选值 12，硬上限 16。超过上游值会增加对单个 DC 的并发连接，是否接受取决于基准中的限速与超时数据；不接受则 `fast` 与 `balanced` 只在起步参数上区分。
2. **基准由谁在什么环境执行。** 需要测试账号、真实线路和三类代理，本机自动化无法代替；基准完成前 S162 不提交。
3. **自定义 DoH 失败时是否回退到内置端点。** 本设计选择不回退（用户填写地址通常是为了不访问内置端点）。代价是 special config 在自定义端点不可用时少一条途径。
4. **自定义 DoH 是否需要支持 RFC 8484 二进制格式。** 本设计只支持 JSON 接口；支持二进制格式要新增 DNS 报文编解码与对应测试，建议另行立项。
5. **A002 是否同时停止 HTTP 时间同步对 DoH 端点的访问。** 本设计不停止：`syncHttpUnixtime` 用于本机时间错误时校时，停掉后可能无法建立连接。
6. **新增“网络”分栏（编号 K）是否合适。** 备选是并入“配置管理”，但那里的条目都是配置与诊断动作，与连接参数性质不同。
