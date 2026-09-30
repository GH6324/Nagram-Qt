# 本机说明（不提交）

## 在这台 Mac 上构建 Linux 版

这台机器是 arm64，Linux 构建环境镜像只支持 x86_64，所以全程用 `--platform linux/amd64` 走模拟，比 CI 慢几倍。只在需要 Release 包或要反复排查 GCC 专属错误时才在本机构建；只要 Debug 产物就等 CI（`Nagram Linux.` 工作流，约两小时）。

### 现有资源

- Docker 由 OrbStack 提供。
- 镜像 `tdesktop:centos_env`（Rocky Linux 8，约 3.4 GB，含从源码编译的 Qt 等全部依赖）。
- 缓存都在 `/Volumes/Cache`（区分大小写的 APFS）下，以普通目录挂进容器：
  - `/Volumes/Cache/nagram-qt-linux-env`：生成的 Dockerfile 和空的构建上下文 `ctx/`。
  - `/Volumes/Cache/nagram-qt-linux-out`：挂到容器内 `/usr/src/tdesktop/out`，保存 Linux 构建目录，避免覆盖本机 macOS 的 `out/`。
  - `/Volumes/Cache/nagram-qt-linux-ccache`：ccache，挂到容器内 `/ccache`。

先确认它们还在：

```bash
docker images tdesktop:centos_env && ls /Volumes/Cache | grep nagram-qt-linux
```

### 镜像丢失时重新生成

按 CI 的参数把 Dockerfile 生成到仓库之外，再用空目录作构建上下文。不要覆盖仓库里的 `Telegram/build/docker/centos_env/Dockerfile`，那是 Jinja 模板。

```bash
W=/Volumes/Cache/nagram-qt-linux-env && mkdir -p $W/ctx
cd Telegram/build/docker/centos_env
DEBUG= LTO= uv run --no-project --with jinja2 python -c "import gen_dockerfile; gen_dockerfile.main()" > $W/Dockerfile
cd $W/ctx && docker build --platform linux/amd64 --progress=plain -t tdesktop:centos_env -f $W/Dockerfile .
```

- 需要几个小时，其中 Qt 最久。
- 拉取 xcb 子模块时 `gitlab.freedesktop.org` 常返回 502/504，直接重跑即可，已完成的步骤有缓存。

### 缓存目录丢失时重新创建

```bash
mkdir -p /Volumes/Cache/nagram-qt-linux-out /Volumes/Cache/nagram-qt-linux-ccache
```

目录为空时下一次要走完整编译。ccache 不要挂到镜像默认的 `/var/cache/ccache`：用 Docker 卷挂到那里时 uid 501 写不进去，CMake 在检测编译器时就失败。挂到 `/ccache` 并设置 `CCACHE_DIR=/ccache`。

### 编译

在仓库根目录执行。首次或改了 CMake 配置时：

```bash
docker run --rm --platform linux/amd64 -u 501 \
  -v $PWD:/usr/src/tdesktop \
  -v /Volumes/Cache/nagram-qt-linux-out:/usr/src/tdesktop/out \
  -v /Volumes/Cache/nagram-qt-linux-ccache:/ccache \
  -e CCACHE_DIR=/ccache -e CCACHE_SLOPPINESS=pch_defines,time_macros \
  -e CONFIG=Release -e KEEP_GOING=1 \
  tdesktop:centos_env \
  env -u CCACHE_DISABLE /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D CMAKE_COMPILE_WARNING_AS_ERROR=OFF
```

- `env -u CCACHE_DISABLE` 必须带，镜像默认关闭了 ccache。
- `KEEP_GOING=1` 让 ninja 一次报出全部失败的文件。
- Release 必须关“警告当错误”：上游 `statistics/statistics_data_deserialize.cpp` 在 GCC 15 的 `-O3` 下触发 `aggressive-loop-optimizations` 警告。
- 排查 GCC 专属错误时改用 CI 的 Debug 参数：`-e CONFIG=Debug`，并传 `-D CMAKE_CONFIGURATION_TYPES=Debug -D DESKTOP_APP_TEST_APPS=ON -D CMAKE_COMPILE_WARNING_AS_ERROR=ON`。Debug 与 Release 共用同一个构建目录，切换配置类型会重新配置。
- 凭据来自未提交的 `Telegram/build/api_credentials.local.cmake`，不用传环境变量。
- 完整编译要几个小时；之后只改了源码时用下面的增量命令。

增量编译：

```bash
docker run --rm --platform linux/amd64 -u 501 \
  -v $PWD:/usr/src/tdesktop \
  -v /Volumes/Cache/nagram-qt-linux-out:/usr/src/tdesktop/out \
  -v /Volumes/Cache/nagram-qt-linux-ccache:/ccache \
  -e CCACHE_DIR=/ccache -e CCACHE_SLOPPINESS=pch_defines,time_macros \
  tdesktop:centos_env \
  sh -c 'cd /usr/src/tdesktop && env -u CCACHE_DISABLE cmake --build out --config Release'
```

### 打包

产物是容器内的 `/usr/src/tdesktop/out/Release/Nagram`（未 strip 约 460 MB）。strip 后打成压缩包放到本机 `out/nagram-linux/`：

```bash
mkdir -p out/nagram-linux
docker run --rm --platform linux/amd64 -u 501 \
  -v /Volumes/Cache/nagram-qt-linux-out:/o -v $PWD/out/nagram-linux:/dst \
  tdesktop:centos_env \
  sh -c 'cd /tmp && mkdir Nagram && cp /o/Release/Nagram Nagram/Nagram && strip Nagram/Nagram && tar -cJf /dst/Nagram-7.2.10-linux-x86_64.tar.xz Nagram'
```

文件名里的版本号按当前版本改。

### 验证的限度

- 容器里没有显示环境，内置的 Qt 平台插件只有 `xcb` 和 `wayland`，程序启动后会因找不到显示而退出，所以本机无法验证界面和登录。
- 能做的检查：`ldd` 没有缺失的库；Debug 配置下可运行 `/usr/src/tdesktop/out/nagram-tests/Debug/test_nagram`。
- 实际运行要拿到真实的 Linux 桌面上试。

### GCC 与 clang 的差异

- 循环变量写 `for (const auto &x : ...)`。按值拷贝 `not_null` 等非平凡类型会触发 `-Werror=range-loop-construct`，clang 不报。
- 未初始化内存的问题在 macOS 上可能碰巧不崩，在 Linux、Windows 上会崩。例子：`Main::Session` 构造期间访问 `session->lifetime()`。
