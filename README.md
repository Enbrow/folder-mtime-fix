# FolderMTimeFix

一个不依赖 PowerShell / .NET 的 Windows 原生 GUI 小工具，用来根据目录中的内容恢复文件夹 `LastWriteTime`（“修改时间”）。

编译产物是单个 `FolderMTimeFix.exe`。程序使用 Win32 API + C++17，不需要额外运行库；GitHub Actions 会在 Windows x64 上自动编译并运行集成测试。

## 时间规则

程序严格采用这一套规则：

1. 从最深层子目录开始处理，再逐级处理父目录。
2. **如果当前目录存在直属文件**：
   - 只比较这些直属文件；
   - 使用其中最新的 `LastWriteTime`；
   - **忽略直属子目录时间**。
3. **如果当前目录没有任何直属文件，但存在直属子目录**：
   - 使用处理完成后的直属子目录时间中最新的一个。
4. 空目录保持原时间不变。
5. 忽略以 `.` 开头的文件和文件夹，例如 `.git`。
6. 为避免误入其他位置，忽略 Junction、目录符号链接以及其他 Reparse Point。

因此：

```text
A\
├─ old.txt       2024-01-01
└─ B\
   └─ new.txt    2025-06-01
```

处理后：

```text
B = 2025-06-01
A = 2024-01-01
```

`B` 中更新的文件不会越过 `B` 覆盖 `A` 的直属文件时间。

如果连续几级目录都没有直属文件，则时间可以沿这些“纯目录层”向上传递：

```text
C\
└─ D\
   └─ deep.txt   2025-06-01
```

处理后 `D` 和 `C` 都会是 `2025-06-01`。

## GUI 功能

- 选择根文件夹
- **Dry Run 预览**：不修改任何时间
- **应用修改**：二次确认后才真正写入
- 显示每个目录：
  - 修改前时间
  - 参考文件 / 子目录
  - 目标时间
  - 修改后时间
  - 错误信息
- 自动把日志写到 `%TEMP%`（仅当 `%TEMP%` 不在目标目录树内）
- 复制日志到剪贴板
- 手动保存日志；程序会阻止把日志保存到刚处理的目录树中

Dry Run 会尝试以 `FILE_WRITE_ATTRIBUTES` 权限打开目录，但不会调用 `SetFileTime`。这样可以提前发现大部分权限问题，并在内存中模拟子目录修改后的时间，使预览尽量与实际 Apply 一致。

## GitHub 自动编译（推荐）

仓库包含：

```text
.github/workflows/build-windows.yml
```

每次 push 到 `main` / `master`，或手动运行 workflow 时：

1. GitHub 使用 `windows-latest`；
2. CMake 生成 x64 Release；
3. MSVC 编译程序；
4. 运行 Windows 集成测试；
5. 测试通过后上传 `FolderMTimeFix.exe` Artifact。

下载位置：

**GitHub 仓库 → Actions → 最近一次成功的 Build Windows EXE → Artifacts → FolderMTimeFix-windows-x64**

GitHub 下载的 Artifact 本身是 ZIP，解压后就是 `FolderMTimeFix.exe`。

## 本地编译

需要 Visual Studio 2022，安装 workload：

- **Desktop development with C++（使用 C++ 的桌面开发）**
- CMake tools for Windows
- Windows 10/11 SDK

然后在仓库根目录运行：

```cmd
build-local.cmd
```

成功后：

```text
build\Release\FolderMTimeFix.exe
```

也可以手动执行：

```cmd
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

## 自动测试覆盖的关键情况

`tests/test_core.cpp` 会在 Windows 上真实创建临时目录和文件并操作 `LastWriteTime`，目前覆盖：

- 父目录有直属旧文件、子目录有更新文件：更新不会越级覆盖父目录；
- 连续无直属文件的目录：Dry Run 会正确模拟逐级传播，Apply 结果与预览一致；
- 空目录：保持不变；
- 以 `.` 开头的文件：不会参与时间计算；
- Dry Run：确认不会修改实际目录时间。

## 与 `git restore-mtime` 配合

如果你的目标是恢复一个 Git 仓库的历史文件时间，可以先用你现有的方法恢复文件本身的 mtime，然后再运行 FolderMTimeFix 修复文件夹时间。

FolderMTimeFix 本身不会调用 Git，也不会修改文件内容、文件时间、创建时间或访问时间；它只在 **Apply** 模式下修改符合规则的目录 `LastWriteTime`。
