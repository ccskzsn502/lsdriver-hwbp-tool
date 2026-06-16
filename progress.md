## 2026-06-15 - Task: Adjust hardware breakpoint register output formatting
### What was done
- Removed the leading `*` marker from resolved register values.
- Removed raw pointer dereference output for values that do not resolve to a module segment.
- Kept module offset output for resolved addresses, while continuing to show the full `X0-X29`, `LR`, `SP`, and `PC` register set.
- Rebuilt the Android arm64 CLI through GitHub Actions and deployed it to `/data/local/tmp/ls-hwbp`.

### Testing
- Ran local build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-test`; build completed successfully with one existing non-blocking unused-parameter warning.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27525060625
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-cleanfmt` with size 584056 bytes.
- Deployed with `install -m 755` to `/data/local/tmp/ls-hwbp` and verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`.

### Notes
- `app/src/main/cpp/main.cpp`: adjusted only the register/stack presentation helpers; no breakpoint, kernel, or shared protocol logic was changed.
- `progress.md`: appended this task record.
- Rollback: redeploy the previous artifact from GitHub Actions run `27507124104`, or revert commit `066ff46` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-15 - Task: Remove remaining register return marker
### What was done
- Removed the `[ret]` display marker from `X0` in hardware breakpoint hit output.
- Kept register values and module-offset resolution unchanged; only presentation markers were cleaned.
- Rebuilt and redeployed the Android arm64 CLI.

### Testing
- Verified `app/src/main/cpp/main.cpp` no longer contains `[ret]`, `read_target_u64`, raw dereference output, or leading-star output patterns.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27526016341
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-nomarker` with size 583880 bytes.
- Deployed to `/data/local/tmp/ls-hwbp` and `/data/local/tmp/ls-hwbp-cn`, then verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`.

### Notes
- `app/src/main/cpp/main.cpp`: removed the optional `ret` parameter from register output and changed `X0` printing to plain register output.
- `progress.md`: appended this task record.
- Rollback: redeploy the artifact from GitHub Actions run `27525060625`, or revert commit `441b4f2` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-15 - Task: Simplify hwbp workflow and add memory write
### What was done
- Removed user-facing multi-breakpoint and module-list commands from the CLI and interactive menu.
- Kept the internal kernel module enumeration used for `so+offset` hit output and module-offset breakpoint address resolution.
- Added session-only target reuse in interactive mode without writing any config or cache files.
- Added interactive memory read and memory write flows, and added the CLI `write` command.

### Testing
- Ran local build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-simplify-check`; build completed successfully with the existing unused `point` parameter warning.
- Verified source no longer exposes `cmd_modules`, `modules`, `multi`, `--point`, `--filter`, or `--regions` command/menu entry patterns.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27536415034
- Downloaded artifact to `/storage/emulated/0/Download/ls-hwbp-simplify` with size 581064 bytes.
- Deployed to `/data/local/tmp/ls-hwbp` and `/data/local/tmp/ls-hwbp-cn`; `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常`, and `/data/local/tmp/ls-hwbp info` returned BRP/WRP counts.
- Verified removed `modules` and `multi` commands now show usage instead of executing removed functionality; verified `write` command entry is present and rejects missing bytes safely. No real target memory write was performed without a safe address.

### Notes
- `app/src/main/cpp/main.cpp`: simplified menu/CLI flow, removed multi-point and module-list entry points, added session-only target reuse, and added memory write support using existing `op_w`.
- `progress.md`: appended this task record.
- Rollback: redeploy the artifact from GitHub Actions run `27526016341`, or revert commit `8fe2293` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-15 - Task: Add kernel-level hwbp stack records and MCP help menu
### What was done
- Added kernel-level stack recording to hardware breakpoint hit records with `stack_count` and up to 256 64-bit stack slots captured from `SP`.
- Updated the user-space tool protocol layout and stack output so the stack section displays only the kernel-returned stack slots, without using `PC` or `LR` as fake stack lines.
- Added an interactive `MCP 模式/接口说明` menu entry that only prints available CLI command formats and does not start a service, listen on a port, or write configuration files.
- Built the updated user-space tool and kernel module, deployed both as a matched protocol pair, and loaded the new module after the device reboot.

### Testing
- Ran local user-space build validation with `g++ -std=c++17 -O2 -Wall -Wextra app/src/main/cpp/main.cpp -o /tmp/ls-hwbp-stack-check`; build completed successfully.
- GitHub Actions user-space build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27539852299
- Kernel Actions run produced the module artifact successfully before failing only at the optional publish-to-dist step: https://github.com/ccskzsn502/lsdriver-android/actions/runs/27540326915
- Deployed `/storage/emulated/0/Download/ls-hwbp-stack` to `/data/local/tmp/ls-hwbp` and `/storage/emulated/0/Download/ls-hwbp-cn-stack` to `/data/local/tmp/ls-hwbp-cn`.
- Copied and loaded `/storage/emulated/0/Download/android13-5.15-lsdriver-stack.ko` as `/data/local/tmp/android13-5.15-lsdriver-stack.ko`; `lsmod` shows `lsdriver 77824 0`.
- Verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常` with shared memory size `12713168`.
- Verified `/data/local/tmp/ls-hwbp info` returned BRP `6`, WRP `4`, and protocol point limit `16`.
- Verified the interactive menu shows item `8) MCP 模式/接口说明` and selecting it prints the CLI command help without side effects.

### Notes
- `lsdriver/io_struct.h`: extended `hwbp_record` with `stack_count`, reserved padding, and `stack[256]`.
- `lsdriver/hwbp.h`: added stack capture from `SP` during breakpoint hits.
- `app/src/main/cpp/main.cpp`: synchronized the user-space record layout, changed stack printing to use kernel-returned stack slots, and added the MCP help menu entry.
- `progress.md`: appended this task record.

## 2026-06-15 - Task: Modularize ls-hwbp source and add stdio MCP server
### What was done
- Split the single `main.cpp` into layered modules: `protocol.h` (shared-memory protocol), `driver.h/.cpp` (driver connect/IO), `symbol.h/.cpp` (address-to-module resolution), `report.h/.cpp` (human-readable output), `cli.h/.cpp` (arg parsing, commands, interactive menu), `mcp.h/.cpp` (stdio MCP server), and a slim `main.cpp` dispatcher.
- Added a C++ built-in stdio MCP server (`ls-hwbp mcp`) implementing JSON-RPC over stdin/stdout with a minimal embedded JSON parser/serializer, so an AI client can fully take over the tool.
- Exposed eight MCP tools: `ping`, `info`, `find_process`, `read_memory`, `write_memory`, `set_breakpoint`, `read_hits`, `remove_breakpoint`. `read_hits` returns registers plus the kernel-recorded stack slots as structured JSON.
- Redirected driver-layer stdout prints to stderr inside MCP mode to keep the JSON-RPC stream clean.
- Updated `CMakeLists.txt` and `.github/workflows/build.yml` to compile all source files together.

### Testing
- Local multi-file build passed cleanly with `g++ -std=c++17 -O2 -Wall -Wextra -pthread` over all six source files, zero warnings after fixing two misleading-indentation warnings.
- Local MCP smoke test: piped `initialize` + `tools/list` into `ls-hwbp mcp`; received valid protocol handshake and full tool list.
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27552100873
- Downloaded artifact (649240 bytes) and deployed to `/data/local/tmp/ls-hwbp` and `/data/local/tmp/ls-hwbp-cn`.
- On-device verification: `ping` returned `驱动响应正常`; `info` returned BRP 6 / WRP 4 / max 16.
- On-device MCP verification: piped `initialize` + `tools/call ping` + `tools/call info`; received `驱动响应正常` and `{"num_brps":6,"num_wrps":4,"max_points":16}` over JSON-RPC.

### Notes
- New files: `app/src/main/cpp/protocol.h`, `driver.h`, `driver.cpp`, `symbol.h`, `symbol.cpp`, `report.h`, `report.cpp`, `cli.h`, `cli.cpp`, `mcp.h`, `mcp.cpp`.
- `app/src/main/cpp/main.cpp`: reduced to an entry dispatcher and added the `mcp` subcommand.
- `CMakeLists.txt` and `.github/workflows/build.yml`: switched to multi-file compilation with pthread linkage.
- No kernel module or shared-memory protocol change in this task; MCP reuses existing `op_*` primitives.
- Rollback: redeploy the previous artifact from GitHub Actions run `27539852299`, or revert commit `361cdeb` in `ccskzsn502/ls-hwbp-tool` and rebuild.

## 2026-06-16 - Task: Start MCP bridge from interactive menu
### What was done
- Changed interactive menu item 8 from a static MCP help page into an MCP bridge launcher.
- Menu item 8 now starts a localhost bridge on `127.0.0.1:37651` using Android `nc`, forwarding connections to `/data/local/tmp/ls-hwbp mcp`.
- Added pid-file reuse so repeated menu selections do not spawn duplicate bridge supervisors.
- Replaced the first one-shot `nc -L` attempt with a looped listener so the bridge supervisor stays alive after client disconnects.
- Cleared the previously stuck bridge processes before deploying the fixed build.

### Testing
- GitHub Actions Android arm64 build completed successfully: https://github.com/ccskzsn502/ls-hwbp-tool/actions/runs/27560371397
- Downloaded artifact (649664 bytes) and deployed to `/data/local/tmp/ls-hwbp`; deployed companion script to `/data/local/tmp/ls-hwbp-cn`.
- Verified `/data/local/tmp/ls-hwbp ping` returned `驱动响应正常` with shared memory size `12713168`.
- Verified selecting menu item `8` prints `MCP 桥接服务已启动: 127.0.0.1:37651`.
- Verified `/data/local/tmp/ls-hwbp-mcp-nc.pid` points to a live bridge supervisor process and `ps` shows an active `nc` listener on port `37651`.

### Notes
- `app/src/main/cpp/cli.cpp`: menu item 8 now starts the MCP bridge service instead of printing static help.
- `progress.md`: appended this task record.
- Rollback: redeploy the previous artifact from GitHub Actions run `27552100873`, or revert commits `f87863a` and `b15d062` in `ccskzsn502/ls-hwbp-tool` and rebuild.
- Rollback: unload the new module with `rmmod lsdriver`, reload the previous known-good `.ko`, redeploy the previous user-space artifact from GitHub Actions run `27536415034`, or revert tool commit `f9a5b43` and kernel commit `3ecdbe0` then rebuild and redeploy.