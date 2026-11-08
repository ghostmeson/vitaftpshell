# vitaFTPShell

An FTP client, local file manager, and anonymous FTP server for the PS Vita,
drawn in the COCKPIT Cyberpunk HUD. The console is one side of the transfer, not
a remote endpoint: local files and a saved FTP site are two live panes, and
moving a file is one action.

**Status:** `building`. Strategy, requirements, the screen-by-screen design, and
the architecture plan are complete. Phase 1 is done: the build shape, packaging,
send script, and layering guard exist, and the first stub VPK,
`vitaftpshell-v01.00-b3.vpk`, builds. No protocol engine, renderer, screen, or
adapter exists yet, so the app has not run on hardware.

## What v1 is

- **Client:** saved sites with anonymous or login credentials, passive FTP,
  remote browsing, download, upload, create, rename, and delete.
- **File manager:** browse the mounted `ux0:`, `ur0:`, and `uma0:` volumes,
  multi-select, recursive folder copy, move, and delete, with protected-path
  rules and explicit confirmations.
- **Server:** anonymous read-write FTP on the mounted `ux0:`, `ur0:`, and `uma0:`
  volumes while the app is open. It is OFF at launch, shows its ON state and
  client count, and accepts one client at a time.
- **Interface:** 960×544 Cyberpunk HUD, 5×7 bitmap type, controller-first input,
  and a transfer dock that shows progress without blocking the workspace.

Not in v1: SFTP, FTPS, HTTP, WebDAV, resume, sync, background transfers,
multiple active transfers, multiple server clients, or a credential vault.

## Read first

| Document | Contents |
|---|---|
| [`STRATEGY.md`](STRATEGY.md) | Why the product exists, who it serves, boundaries, metrics |
| [`docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md`](docs/plans/2026-11-08-1103-feat-vitaftpshell-v1-plan.md) | The plan: requirements, key decisions, units, verification contract |
| [`PROJECTS.md`](PROJECTS.md) | Current phase, last job, next action |
| [`AGENTS.md`](AGENTS.md) | Project contract for agents |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Layers, ports, threading, and module rules |
| [`docs/README.md`](docs/README.md) | Documentation map |
| [`docs/DESIGN.md`](docs/DESIGN.md) | The settled layout, states, and input; the design sheets |

## Build

Targets a PS Vita on 3.65 Enso with VitaSDK 2026.08 and libvita2d.

```sh
export VITASDK="$PWD/../vitasdk"
export PATH="$VITASDK/bin:$PATH"

./build.sh                 # one UNSAFE eboot, one vitaftpshell-v01.00-b<n>.vpk
./tools/test/run.sh        # layering guard; host tests from Phase 2
./send.sh <vita-ip>        # uploads build/ to VitaShell's FTP server
```

`BUILD_NUMBER` lives in this directory, never in `build/`. A build reuses the
number when the VPK inputs are unchanged and increments otherwise. `build/`
keeps exactly one VPK and the unstripped ELF. `send.sh` refuses an artifact whose
number does not match `BUILD_NUMBER`, so a failed build cannot upload the
previous one. Installing requires "Enable Unsafe Homebrew" (D32).

## Safety

- The server is anonymous read-write and exposes the mounted `ux0:`, `ur0:`, and
  `uma0:` volumes. Start it only on a network you trust.
- The server denies all access to `ur0:tai/boot_config.txt` and the app's own
  `ux0:data` folder, and denies every write, create, rename, and delete inside
  `ur0:tai` and `ux0:tai`.
- The local manager may edit files inside `ur0:tai` and `ux0:tai`, but only after
  a high-risk confirmation; `boot_config.txt` remains denied for every role.
- Installing requires "Enable Unsafe Homebrew": the app needs the permissions the
  safe authid refuses in order to serve `ur0:` and `uma0:`.
- Downloads reject remote names containing a path separator, `:`, NUL, or a
  `..` segment before a local path is built.
- No kernel plugin, `*KERNEL` entry, or boot-time component is part of v1.
- Saved passwords are stored unencrypted. They are never written to the log.
- Volume roots, `ur0:tai`, and `ux0:tai` cannot be renamed, moved, or deleted.

## License

GPLv3. The local file manager builds on VitaShell, so vitaFTPShell is a GPLv3
island inside the MIT workspace. Its code must not be copied into the MIT
projects. See [`LICENSE`](LICENSE).
