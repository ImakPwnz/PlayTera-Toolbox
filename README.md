# PlayTera Toolbox

PlayTera-specific compatibility and distribution tools for the TERA Toolbox.

## Publication status

This repository currently contains the PlayTera-authored integration and build
sources only. It is **not yet a complete or installable Toolbox distribution**.
There is no public installer release and no active self-update feed.

The complete local candidate is based on
`tera-classic-toolbox/tera-toolbox-playtera`, commit
`0d020efba36050019f7a36d3ab512aa397a628e1`. Original Toolbox authors include
Caali, Pinkie Pie, meishu, JKQ and other component contributors. This repository
does not claim ownership or a new blanket license for their work.

## Included sources

- Routing support for the PlayTera client integration, which uses
  `cabalmain.exe` and the existing game-process ExitLag session.
- The native transport helper and its IPC data structure.
- The separate Toolbox launcher wrapper and empty-directory NSIS installer.
- The bounded, signature-verified core updater and offline manifest signer.

These are integration sources, not a replacement for the missing upstream core.
Build scripts require separately supplied compatible dependencies and tools.
No GitHub Actions workflows or automatic builds are enabled by these files.

## Not included

No credentials, private signing keys, ExitLag SDK, game DLLs, game/server files,
private launcher sources, player data or operational logs are published here.
Existing client-interface DLLs, scanner binaries and the full upstream Toolbox
are withheld pending evidence of redistribution rights and required source
coverage. Third-party dependency license texts do not establish a license for
the surrounding component.

## Release gates

Before a player-facing installer is published:

1. Establish redistribution rights and source/notice obligations for the
   upstream Toolbox, client-interface DLLs and scanner components.
2. Resolve the obsolete Electron 16.0.2 runtime and compatible native ABI.
3. Accept a private complete-installer game test with the matching separately
   distributed PlayTera client integration.
4. Sign the final installer, re-pin its final hash/size and sign the launcher
   and release metadata through the separate operator release process.

Self-updates remain disabled until a separately verified signed feed is ready.
Publishing this source repository does not activate any production changes.

Support: https://playtera.to
