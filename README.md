# GPTtoMBR

GPTtoMBR is a small C++17 Windows console program that converts supported GPT
partition layouts to MBR on a selected physical drive.

> **Warning:** This program writes directly to a physical disk. Its read-only
> preflight checks GPT header and partition-array CRCs, validates both GPT
> copies, and rejects unsupported layouts. Apply mode saves and verifies a
> metadata backup, writes only sector 0 and GPT metadata sectors, then reads
> those sectors back. Partition contents are not intentionally modified.
> This is not a guarantee against software bugs, power loss, hardware failure,
> data loss, or an unbootable disk. Make and verify a full-disk backup first;
> test on a disk image or disposable disk.

## What the current code does

- Defaults to a read-only preflight. It checks 512-byte sectors, disk size,
  both GPT headers and CRCs, both partition-array CRCs, and matching primary
  and backup GPT copies.
- Rejects overlapping partitions, non-protective MBR entries, nonzero GPT
  attributes, unsupported partition types, more than four partitions, and
  layouts outside MBR's LBA limits. Supported type mappings are Windows basic
  data, EFI System, Windows recovery, Linux filesystem, and Linux swap.
- Apply mode requires exclusive access and a new metadata-backup file path.
  The backup contains sector 0, both GPT headers, and both partition arrays.
  The program verifies the saved file before writing and attempts to restore
  the original metadata if a write or read-back check fails.
- Preserves the first 446 bytes of sector 0, writes the new MBR table, and
  clears both GPT copies. It does not write partition-content sectors. GPT
  partition GUIDs, names, attributes, and boot configuration are not all
  representable in MBR and are not preserved.
- Requires Windows and administrator privileges for physical-drive access.
  Wine is explicitly refused; running the program on Linux builds only a stub
  that reports that Windows is required.

## Run from Windows Command Prompt

Open **Command Prompt as Administrator**, then change to the directory
containing the executable. For example:

```cmd
cd /d "C:\path\to\GptToMbr"
GptToMbr.exe 1
```

Replace `1` with the number of the target physical drive; `1` selects
`\\.\PhysicalDrive1`. First run the read-only preflight:

```cmd
"C:\path\to\GptToMbr.exe" 1
```

To convert, specify a new backup file on a different physical disk:

```cmd
"C:\path\to\GptToMbr.exe" 1 --apply --backup "E:\GptToMbr-PhysicalDrive1.gptbackup"
```

The program first requires typing `BACKUP ON ANOTHER DISK`, then
`CONVERT PhysicalDrive1`. The metadata backup is not a backup of partition
contents and is not a substitute for a verified full-disk backup. A sudden
power loss can interrupt writes before automatic rollback is possible.
Converting the partition table also does not convert an installed operating
system's boot mode or guarantee that it will boot.

To restore the saved partition metadata after a failed or interrupted
conversion, run:

```cmd
"C:\path\to\GptToMbr.exe" 1 --restore "E:\GptToMbr-PhysicalDrive1.gptbackup"
```

The restore path validates the backup format, checksum, disk size, GPT headers,
and arrays before writing, then reads the restored GPT back for CRC checks.
Keep the backup file until the disk has been independently inspected.

## Dependencies

For a native CMake build, install:

- CMake 3.15 or newer
- A C++17 compiler (GCC, Clang, or MSVC)
- A build tool supported by CMake (such as Make or Ninja)

To build the Windows executable from Linux, also install a MinGW-w64
cross-compiler. To publish a GitHub release, install Bash, Git, coreutils
(`sha256sum`), and GitHub CLI (`gh`); log in with `gh auth login` and make sure
the current branch is clean and pushed to `origin`.

### Debian and Ubuntu

```sh
sudo apt update
sudo apt install build-essential cmake mingw-w64 git gh
```

### Fedora

```sh
sudo dnf install gcc-c++ cmake make mingw64-gcc-c++ git gh
```

### Arch Linux

```sh
sudo pacman -S --needed base-devel cmake mingw-w64-gcc git github-cli
```

### openSUSE

```sh
sudo zypper install gcc-c++ cmake make mingw64-cross-gcc-c++ git gh
```

For other distributions, install the equivalent CMake, C++17 toolchain,
Git, and (for Windows releases) MinGW-w64 and GitHub CLI packages.

## Build

In VS Code, press **Ctrl+Shift+B** (or choose **Terminal > Run Build Task**) to
run the default MinGW-w64 build-and-release task. After a successful clean
build, it commits tracked changes, pushes the current branch to `origin`, and
publishes a GitHub release with the next patch version (starting at `v1.0.0`).
The release includes the Windows executable and its SHA-256 checksum. The task
requires Make, the MinGW-w64 cross-compiler, Git, and an authenticated GitHub
CLI (`gh`) on `PATH`. It commits all tracked changes, so review them before
building. It stops if there are staged changes or untracked files; commit or
unstage staged changes and commit or ignore untracked files before starting.

The regular CMake build compiles the program for the current host. On Linux
this is the Windows-required stub; it is not a Linux disk-conversion utility.

```sh
cmake -S . -B build
cmake --build build
```

On Windows, run `build/GptToMbr.exe` (or `build/Release/GptToMbr.exe` with a
multi-configuration generator) from an elevated terminal. Specify the physical
drive number as its argument.

To cross-compile the actual Windows executable from Linux:

```sh
cmake -S . -B build/windows \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build/windows
```

The executable is written to `build/windows/GptToMbr.exe`.

## Create a GitHub release

After committing and pushing a clean branch, create a versioned Windows release
with the included script:

```sh
./release.sh v1.0.0
```

The script cross-compiles the Windows executable, creates a SHA-256 checksum,
and publishes both as assets on the `UsefulVideos/GPTtoMBR` GitHub release.
It requires the `gh` CLI to be authenticated, and verifies that the current
branch's `HEAD` matches its `origin` branch before building.

The same action is available as a CMake target on Unix-like systems:

```sh
cmake -S . -B build -DGPTTOMBR_RELEASE_VERSION=v1.0.0
cmake --build build --target release
```
