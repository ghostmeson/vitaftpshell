---
name: vitaFTPShell
last_updated: 2026-11-08
---

# vitaFTPShell Strategy

## Purpose

Homebrew users want to install, back up, and publish files without leaving the
console. Today they must boot a PC, run FileZilla, and remember paths by hand;
the Vita can serve FTP but has no maintained on-device client, so the protocol
and passive data connections must be made reliable on SceNet.

## Positioning

Treat the console as a peer in the transfer, not a remote endpoint: local and
remote are two live panes, and moving a file is one action on the Vita. Server
mode is a compatibility surface for VitaShell users, not the reason to install;
a desktop-client port and a server-only VitaShell clone are both ruled out.

## Users

**Primary:** The anonymous-first collector — they are hiring vitaFTPShell to
pull homebrew, patches, and media from saved public FTP sites on the Vita,
without a PC, while keeping logins for the sites that require them.

**Secondary:** The Vita-as-a-drive user — they keep VitaShell's FTP-server habit
and are hiring the same app to serve the console's files to another machine, so
they need one app instead of two.

## Boundaries

- No SFTP/FTPS in v1; plain FTP first, and secure transports are a later
  decision.
- No background daemon or taiHEN/system plugin; transfers live and die with the
  app.
- No PC-first workflow or desktop-client port; the Vita is the workspace.
- No VitaShell parity race; server mode is compatibility, not the feature list.

_Resist a change when:_ it moves the workspace back to a PC, needs a kernel or
system plugin, adds a secure transport before plain FTP is reliable, or chases
VitaShell parity instead of the client job.

## Key metrics

- **Transfer completion rate** — completed ÷ started downloads and uploads in a
  hardware session; measured in the on-device session log.
- **Crash-free hardware sessions** — sessions without a crash dump ÷ sessions
  started; measured from console crash dumps and the session log.
- **Connect-to-first-listing** — milliseconds from Cross on a saved site to the
  first remote row; measured in the session log.
- **Server-matrix pass** — distinct server and listing variants (passive mode,
  Unix `LIST`, `MLSD`, DOS) that complete connect/list/retr/stor; measured by the
  release checklist.

## Tracks

### FTP engine and compatibility

One protocol core with two roles: passive client and passive server, plus auth,
listing formats, timeouts, and recovery.

_Why it serves the approach:_ the console-first bet fails if the wire is
unreliable, and the server compatibility surface shares the same engine.

### Two-pane transfer workspace and site manager

Local and remote focus, one-press transfer, saved sites (anonymous and login),
and a queue with progress and cancel in the dock.

_Why it serves the approach:_ this is "console is a peer" made visible; a file
moves between panes rather than through an export/import ritual.

### Safe local file manager

Browse, create, rename, delete, and copy/move on the console, with guarded
system paths and explicit confirmation. VitaShell is the GPLv3 foundation.

_Why it serves the approach:_ a client without a local side is a downloader, not
a file tool, and destructive actions must be trustworthy on a device with no
Trash.

### HUD shell, input, and text entry

The Cyberpunk HUD visual system, a controller-first input grammar, system text
entry, and 960 × 544 readability.

_Why it serves the approach:_ on a 5-inch, keyboard-less console the interface
is the product; a desktop layout would break the bet before the protocol does.

## Brand

**One-liner:** The Vita on both sides of the transfer.

**Key message:** One console app for local files and remote servers: pull from a
site, push to a server, or serve the console's own files, without making the PC
the workspace.
