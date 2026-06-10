# lsdriver-hwbp-tool

Android ARM64 userspace CLI for the `lsdriver` kernel module hardware breakpoint/watchpoint interface.

Repository: https://github.com/ccskzsn502/lsdriver-hwbp-tool

## Build

Use GitHub Actions:

```text
Actions -> Build Android arm64 CLI -> Run workflow
```

Artifact:

```text
ls-hwbp-tool-arm64/ls-hwbp
```

## Install

```bash
adb push ls-hwbp /data/local/tmp/
adb shell chmod 755 /data/local/tmp/ls-hwbp
```

Load `lsdriver.ko` first:

```bash
adb shell su -c 'insmod /data/local/tmp/android13-5.15-lsdriver-nohide.ko'
adb shell su -c 'cat /proc/modules | grep lsdriver'
```

## Usage

```bash
adb shell su -c '/data/local/tmp/ls-hwbp ping'
adb shell su -c '/data/local/tmp/ls-hwbp info'
adb shell su -c '/data/local/tmp/ls-hwbp remove'
adb shell su -c '/data/local/tmp/ls-hwbp monitor --pid 12345 --type x --addr 0x12345678 --len 4 --scope all'
adb shell su -c '/data/local/tmp/ls-hwbp monitor --pid 12345 --type rw --addr 0x12345678 --len 8 --scope all --interval 500'
```

Valid types: `x`, `r`, `w`, `rw`.

Valid scopes: `main`, `other`, `all`.

The tool renames itself to `LS` and maps shared memory at `0x2025827000`, matching the kernel module protocol.
