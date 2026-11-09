# GPTtoMBR

GPTtoMBR is a small C++17 Windows console program that reads GPT partition
entries from a selected physical drive and writes a new MBR partition table to
that drive's first sector.

> **Warning:** This program writes directly to a physical disk. The current
> implementation replaces the protective MBR in LBA 0, does not update or
> remove the GPT metadata, and does not preserve the old MBR boot code. It
> should be treated as experimental and can make a disk unbootable or
> inaccessible. Back up the disk and test only on disposable media or a disk
> image. The program's "lossless" banner is not a guarantee of safe conversion.

## What the current code does

- Accepts a physical drive number on Windows (for example, `1` for
  `\\.\PhysicalDrive1`) and requests an explicit `YES` confirmation.
- Reads the GPT header at LBA 1 and its partition-entry array, then refuses
  disks with more than four used GPT entries or entries that exceed the
  32-bit MBR LBA limit.
- Writes one MBR sector at LBA 0. Every created partition is marked inactive
  and assigned MBR type `0x07`; the GPT entry's type, boot flag, and other
  metadata are not translated.
- Assumes 512-byte sectors and does not check GPT CRCs, backup GPT metadata,
  overlapping partitions, or all malformed GPT ranges.
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
`\\.\PhysicalDrive1`. The executable path can also be supplied directly:

```cmd
"C:\path\to\GptToMbr.exe" 1
```

Check the drive number carefully before proceeding. When prompted, type
`YES` in all caps and press Enter to confirm the disk write. Any other input
aborts without writing. Review the warning above and back up the disk first.

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
