# lfspkg – Low-Level Package Tracker for LFS-Based Systems

> *File-level tracking, dependency scanning, and a clean C++ library API*

[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

**lfspkg** is a minimal, standalone package tracker. It records every file a package installs, resolves ELF shared-library dependencies, and provides atomic transactions with rollback. It can be used directly from the command line, or as a C++ library (`liblfspkg`) linked by higher-level package managers such as [qp](https://github.com/Qaaxaap/qp).

**Repository:** [https://github.com/Qaaxaap/lfspkg.git](https://github.com/Qaaxaap/lfspkg.git)

---

## Philosophy

> *"Track what you install. Stay lean. Stay interoperable."*

lfspkg does **not** download sources, resolve remote dependencies, or run build scripts. It does exactly two things:

- **Track** installed files, directories, and symlinks on a per-package basis.
- **Suggest** runtime dependencies by scanning ELF binaries for `DT_NEEDED` entries.

Everything else — fetching sources, executing builds, managing repositories — belongs in the layer above (i.e., `qp`).

Because lfspkg exposes a public `namespace lfspkg` API, any tool can link against it and use its package database without shelling out to the CLI.

---

## Features

| Feature | Description |
|---------|-------------|
| File-level tracking | Manifest records every file, symlink, and directory per package |
| Atomic transactions | Writes happen to `.tmp` files first, then `rename()` — crash-safe |
| Rollback on failure | If an install or upgrade fails mid-way, previous files are restored |
| ELF dependency scan | `suggest-deps` parses `readelf -d` output to guess runtime dependencies |
| Reverse-dependency check | `remove` refuses to uninstall a package that is still depended on |
| Dependency tree | `tree` prints a visual `├──`/`└──` hierarchy |
| Package verification | `verify` checks that every manifest entry still exists on disk |
| gettext i18n | Chinese (`zh_CN`) translations included; others can be added via `.po` files |
| Modular C++ library | Headers in `include/lfspkg/`, sources in `src/` — linkable as `liblfspkg` |
| Standalone CLI | 10 subcommands for direct use without any higher-level tool |

---

## Build & Install

lfspkg uses **GNU Autotools**. Build dependencies: `g++`, `autoconf`, `automake`, `gettext`.

```bash
git clone https://github.com/Qaaxaap/lfspkg.git
cd lfspkg
autoreconf -i
./configure
make
sudo make install
```

---

## Quick Start

### 1. Build from stage directory

```bash
DESTDIR=/tmp/staging/hello make -C ~/src/hello install
lfspkg install --stage /tmp/staging/hello hello 1.0
```

### 2. List installed packages

```bash
lfspkg list
```

### 3. Show package details

```bash
lfspkg info hello
```

### 4. Show dependency tree

```bash
lfspkg tree hello
```

### 5. Suggest runtime dependencies (ELF scanning)

```bash
lfspkg suggest-deps /tmp/staging/hello
```

### 6. Verify package integrity

```bash
lfspkg verify hello
```

### 7. Remove a package

```bash
lfspkg remove hello
```

---

## CLI Reference

```
lfspkg install   [--stage <dir>] <name> <version> [dep,...]
lfspkg upgrade   [--stage <dir>] <name> <version> [dep,...]
lfspkg install-meta <meta-file>
lfspkg remove    <name>
lfspkg list
lfspkg info      <name>
lfspkg tree      <name>
lfspkg owners    <name>
lfspkg verify    <name>
lfspkg suggest-deps <stage-dir>
```

---

## How It Works

When you run `lfspkg install --stage /tmp/staging/foo foo 1.0`:

1. lfspkg walks the stage directory, collecting every file, symlink, and directory.
2. It checks the `owners.tsv` index for conflicts (refuses to overwrite files owned by a different package).
3. It copies files from the stage directory to the target root (`/` by default; override with `LFSPKG_ROOT`).
4. It writes the file manifest (`/var/lib/lfspkg/manifests/foo.list`) and metadata (`/var/lib/lfspkg/packages/foo.meta`).
5. It updates the global owner index (`/var/lib/lfspkg/owners.tsv`).

If any step fails, the transaction is rolled back and the filesystem is left unchanged.

---

## Database Layout

```
/var/lib/lfspkg/
├── packages/
│   ├── zlib.meta           # key=value (name, version, install_time, stage_dir, deps)
│   └── nginx.meta
├── manifests/
│   ├── zlib.list           # TSV (type \t path)
│   └── nginx.list          #   type: D=directory, F=file, L=symlink
├── owners.tsv              # path \t package  — global reverse index
└── txn/                    # temporary transaction data and backups
```

---

## Library API

qp and other tools can link against lfspkg directly:

```cpp
#include <lfspkg/db.hpp>
#include <lfspkg/package.hpp>
#include <lfspkg/elf.hpp>

lfspkg::PackageDB db;                         // default DB root
auto owners = db.load_owners();
auto manifest = lfspkg::collect_manifest_from_stage("/tmp/staging/foo");

lfspkg::PackageSpec spec{ "foo", "1.0", "/tmp/staging/foo", { "zlib" } };
lfspkg::apply_install_or_upgrade(db, spec, false);

lfspkg::suggest_deps(db, "/tmp/staging/bar", pkgs, unresolved);
```

Headers: `include/lfspkg/db.hpp`, `package.hpp`, `util.hpp`, `elf.hpp`.

---

## Configuration

lfspkg reads two environment variables; sensible defaults are used if they are unset:

| Variable | Default | Purpose |
|----------|---------|---------|
| `LFSPKG_DB` | `/var/lib/lfspkg` | Database root directory |
| `LFSPKG_ROOT` | `/` | Target filesystem root (useful for chroot / DESTDIR installs) |

For development, point both at a temporary directory:

```bash
export LFSPKG_DB=/tmp/lfspkg-test/db
export LFSPKG_ROOT=/tmp/lfspkg-test/root
```

---

## Relationship with `qp`

| Layer | Tool | Role |
|-------|------|------|
| High-level | `qp` | Repository interaction, MAKEFS execution, user-facing CLI |
| Low-level | `lfspkg` | File tracking, owner index, ELF scanning, atomic DB writes |

`qp` links `liblfspkg` and calls its C++ API. Both tools remain independently usable — you can use `lfspkg` on its own without `qp`.

---

## FAQ

**Q: Why not just use a real package manager?**  
A: lfspkg is a *building block*, not a full package manager. It is designed for LFS/BLFS users who want fine-grained control and for higher-level tools (`qp`) that need a reliable tracking layer.

**Q: Do I need a database server?**  
A: No. All state is stored as plain text files (`key=value` metadata, TSV manifests and owner index). You can inspect, edit, or back them up with any text editor.

**Q: Can I use lfspkg on a system that also has `apt` or `dnf`?**  
A: Yes. lfspkg only tracks what you explicitly install through it; it does not interfere with other package managers.

**Q: What happens if an install is interrupted?**  
A: The database uses atomic `write-to-tmp-then-rename` semantics and transactional rollback. An interrupted install leaves no partial state.

**Q: How is `lfspkg` different from `qp`?**  
A: `lfspkg` is the low-level tracker — it records files and scans ELF dependencies. `qp` is the user-facing tool that fetches repositories, runs MAKEFS build scripts, and calls lfspkg's API to register results.

---

## Contributing

1. Fork the repository.
2. Create a branch for your change.
3. Make your changes (C++17, following the existing code style).
4. Ensure `make check` passes.
5. Submit a pull request.

---

## License

lfspkg is released under the **BSD 3-Clause License**. See [LICENSE](LICENSE) for the full text.

---

**Track what you install. Keep it simple. Keep it yours.**

---

# lfspkg — 为 LFS 打造的轻量级软件包追踪工具

> *精细到文件级的安装追踪、自动依赖扫描，还有一套干净的 C++ 库 API*

[![License: BSD 3-Clause](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

**lfspkg** 是一款极简、独立运行的软件包追踪工具。它记录每个软件包安装了哪些文件，解析 ELF 可执行文件以确定运行时共享库依赖，并通过事务机制保证操作失败时自动回滚。

你可以在命令行直接使用它，也可以将其作为 C++ 库（`liblfspkg`）链接到 [qp](https://github.com/Qaaxaap/qp) 等上层工具中。

**仓库地址：** [https://github.com/Qaaxaap/lfspkg.git](https://github.com/Qaaxaap/lfspkg.git)

---

## 理念

> *"装过即留痕。极简，兼容，不越界。"*

lfspkg **不负责**下载源码、解析远程依赖、运行构建脚本。它只做两件事：

- **追踪**每个软件包安装了哪些文件、目录和符号链接。
- **推测**运行时依赖——扫描 ELF 二进制文件的 `DT_NEEDED` 条目，告诉你这个包大概需要哪些库。

获取源码、执行构建、管理仓库——这些都交给上层工具（也就是 `qp`）去处理。

lfspkg 暴露了完整的 `namespace lfspkg` API，任何工具都可以直接链接它来读写包数据库，不需要通过命令行绕一圈。

---

## 功能

| 功能 | 描述 |
|------|------|
| 文件级安装追踪 | 为每个软件包生成一份清单，精确列出它安装的所有文件、目录和符号链接 |
| 事务性写入 | 先写 `.tmp` 临时文件，再 `rename()` 覆盖，保障崩溃安全 |
| 失败自动回滚 | 安装或升级中途出错，自动恢复之前的状态 |
| ELF 依赖扫描 | `suggest-deps` 调用 `readelf -d`，从结果中推断运行时依赖 |
| 反向依赖检查 | `remove` 卸载前检查：若其他包仍依赖此包，拒绝卸载 |
| 依赖树 | `tree` 用 `├──` / `└──` 展示依赖层级 |
| 软件包完整性校验 | `verify` 逐一检查清单中的文件是否仍存在于磁盘 |
| gettext 国际化 | 已内置中文（`zh_CN`），可通过 `.po` 文件添加其他语言 |
| 模块化 C++ 库 | 头文件位于 `include/lfspkg/`，源文件位于 `src/`，可作为 `liblfspkg` 链接 |
| 自带命令行 | 10 个子命令开箱即用，无需任何上层管理器 |

---

## 构建与安装

lfspkg 使用 **GNU Autotools**。需要预先装好：`g++`、`autoconf`、`automake`、`gettext`。

```bash
git clone https://github.com/Qaaxaap/lfspkg.git
cd lfspkg
autoreconf -i
./configure
make
sudo make install
```

---

## 快速开始

### 1. 从 stage 目录安装

```bash
DESTDIR=/tmp/staging/hello make -C ~/src/hello install
lfspkg install --stage /tmp/staging/hello hello 1.0
```

### 2. 列出已安装的软件包

```bash
lfspkg list
```

### 3. 查看软件包详情

```bash
lfspkg info hello
```

### 4. 查看依赖树

```bash
lfspkg tree hello
```

### 5. 推断运行时依赖（ELF 扫描）

```bash
lfspkg suggest-deps /tmp/staging/hello
```

### 6. 校验软件包完整性

```bash
lfspkg verify hello
```

### 7. 卸载软件包

```bash
lfspkg remove hello
```

---

## 命令行参考

```
lfspkg install   [--stage <dir>] <name> <version> [dep,...]
lfspkg upgrade   [--stage <dir>] <name> <version> [dep,...]
lfspkg install-meta <meta-file>
lfspkg remove    <name>
lfspkg list
lfspkg info      <name>
lfspkg tree      <name>
lfspkg owners    <name>
lfspkg verify    <name>
lfspkg suggest-deps <stage-dir>
```

---

## 工作原理

当你执行 `lfspkg install --stage /tmp/staging/foo foo 1.0` 时：

1. lfspkg 遍历 stage 目录，将其中每个文件、目录和符号链接登记。
2. 检查 `owners.tsv` 所有权索引——若某文件已属于其他软件包，拒绝覆盖，防止文件冲突。
3. 将文件从 stage 目录复制到目标根目录（默认 `/`，可通过 `LFSPKG_ROOT` 环境变量修改）。
4. 写入文件清单（`/var/lib/lfspkg/manifests/foo.list`）和元数据（`/var/lib/lfspkg/packages/foo.meta`）。
5. 更新全局所有者索引（`/var/lib/lfspkg/owners.tsv`）。

任何步骤出错，整个事务回滚，文件系统恢复原样。

---

## 数据库布局

```
/var/lib/lfspkg/
├── packages/
│   ├── zlib.meta           # key=value 格式，存包名、版本、安装时间、stage 目录、依赖等
│   └── nginx.meta
├── manifests/
│   ├── zlib.list           # TSV 格式，字段：类型 \t 路径
│   └── nginx.list          #   类型取值：D=目录，F=文件，L=符号链接
├── owners.tsv              # 路径 \t 所属包  —— 全局反向索引
└── txn/                    # 事务临时数据和备份
```

---

## 库接口

qp 之类的工具可以直接 `#include` 然后链接 lfspkg：

```cpp
#include <lfspkg/db.hpp>
#include <lfspkg/package.hpp>
#include <lfspkg/elf.hpp>

lfspkg::PackageDB db;                         // 使用默认 DB 根目录
auto owners = db.load_owners();
auto manifest = lfspkg::collect_manifest_from_stage("/tmp/staging/foo");

lfspkg::PackageSpec spec{ "foo", "1.0", "/tmp/staging/foo", { "zlib" } };
lfspkg::apply_install_or_upgrade(db, spec, false);

lfspkg::suggest_deps(db, "/tmp/staging/bar", pkgs, unresolved);
```

头文件：`include/lfspkg/db.hpp`、`package.hpp`、`util.hpp`、`elf.hpp`。

---

## 配置

lfspkg 通过两个环境变量控制行为，未设置时使用默认值：

| 变量 | 默认值 | 用途 |
|------|--------|------|
| `LFSPKG_DB` | `/var/lib/lfspkg` | 数据库根目录 |
| `LFSPKG_ROOT` | `/` | 目标文件系统根目录（chroot / DESTDIR 安装场景） |

开发调试时可将两者指向临时目录：

```bash
export LFSPKG_DB=/tmp/lfspkg-test/db
export LFSPKG_ROOT=/tmp/lfspkg-test/root
```

---

## 与 `qp` 的关系

| 层次 | 工具 | 职责 |
|------|------|------|
| 上层 | `qp` | 仓库交互、MAKEFS 执行、用户界面 |
| 底层 | `lfspkg` | 文件追踪、所有者索引、ELF 扫描、事务性 DB 写入 |

`qp` 链接 `liblfspkg` 并调用其 C++ API。两者各自独立——不使用 `qp` 也可以单独使用 `lfspkg`。

---

## 常见问题

**问：为什么不直接用成熟的包管理器？**  
**答：** lfspkg 定位是一个*构建组件*，而非完整的包管理器。它为希望精细控制系统的 LFS/BLFS 用户设计，也为 `qp` 等上层工具提供可靠的追踪层。

**问：需要安装数据库服务器吗？**  
**答：** 不需要。所有状态以纯文本文件存储——元数据为 `key=value` 格式，清单和索引为 TSV。使用任意文本编辑器即可查看、修改或备份。

**问：系统上已有 `apt` 或 `dnf`，还能使用 lfspkg 吗？**  
**答：** 可以。lfspkg 只追踪通过它明确安装的软件包，不会干扰其他包管理器。

**问：安装过程中断电会怎样？**  
**答：** 数据库写入采用"先写临时文件再 rename"的策略，配合事务回滚机制，中断的安装不会留下不一致的状态。

**问：`lfspkg` 和 `qp` 是什么关系？**  
**答：** `lfspkg` 是底层组件，负责记录文件和扫描 ELF 依赖。`qp` 是面向用户的上层工具，负责获取仓库、执行 MAKEFS 构建脚本，并调用 lfspkg 的 API 登记结果。

---

## 贡献指南

1. Fork 本仓库。
2. 为修改内容创建新分支。
3. 修改代码（C++17，遵循现有代码风格）。
4. 确保 `make check` 通过。
5. 提交 Pull Request。

---

## 许可证

lfspkg 采用 **BSD 3-Clause License**。详见 [LICENSE](LICENSE)。

---

**装了什么，一清二楚。够简单，才够自由。**
