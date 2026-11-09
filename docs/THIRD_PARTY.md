# Third-party code and references

Nothing is vendored. The FTP engine, the file-operation policy, and the UI are
written in this project. Two open-source projects shaped it, and one graphics
layer is adapted rather than invented.

## Code

| Project | License | Use | Status |
|---|---|---|---|
| COCKPIT — Copyright (c) 2026 Upakul Talukdar | MIT | `src/gfx/` is adapted from its `draw`, `font`, and `theme` | Adapted, with the notice in [`LICENSES/COCKPIT-MIT.txt`](../LICENSES/COCKPIT-MIT.txt) |
| [VitaShell](https://github.com/TheOfficialFloW/VitaShell) | GPLv3 | Behavioral reference for the file manager and server commands | Reference only; not vendored |
| [libftpvita](https://github.com/xerpi/libftpvita) | MIT | Behavioral reference for server commands | Reference only; not used |

The FTP server is written in `src/core/ftp/` so that both roles share one path
table, one reply parser, and one transfer planner (D40). VitaShell's copy engine
is bound to its own UI and would bypass this project's templed writes, risk table,
and rollback, so reusing it would cost more than it saves.

## When code is adapted

- record the upstream repository and commit, or the source project and decision;
- list the files and the modifications;
- keep the copyright and license notices;
- confirm the license is compatible with GPLv3.

The project stays GPLv3 by choice (D40), which keeps the door open to reuse
VitaShell code later without a relicense.

## Protocol references

| Document | Use |
|---|---|
| [RFC 959](https://www.rfc-editor.org/rfc/rfc959) | Commands, replies, and data connections |
| [RFC 3659](https://www.rfc-editor.org/rfc/rfc3659) | MLSD, MLST, SIZE, MDTM, REST |
| [RFC 2428](https://www.rfc-editor.org/rfc/rfc2428) | EPSV and EPRT; v1 answers 502 |

## Product references

- COCKPIT — Copyright (c) 2026 Upakul Talukdar — the design system that shapes
  the palette, type, spacing, and restraint rules. vitaFTPShell restates the
  rules it uses in [`DESIGN.md`](DESIGN.md); the derived graphics code keeps its
  MIT notice in [`LICENSES/COCKPIT-MIT.txt`](../LICENSES/COCKPIT-MIT.txt).
- [FileZilla](https://filezilla-project.org/) — behavior reference for saved
  sites and two-pane transfers; no code is reused.
