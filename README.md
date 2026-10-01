# PlayTera Toolbox

Toolbox distribution and installer tooling for PlayTera.

This repository contains only Toolbox updater configuration, updater source,
the Toolbox start wrapper and installer/build tooling. It does not contain the
d3d9 project, ExitLag integration sources, the ExitLag SDK, game DLLs, credentials,
private launcher sources, operational data or player data.

## Installer

A complete local installer candidate exists, but no public installer release is
available yet. It includes the Toolbox runtime and required Toolbox-side
components, not game DLLs or SDK keys. Self-updates remain disabled.

The intended release asset is `v1.0.0/PlayTeraToolboxSetup.exe`. The separate
PlayTera launcher must verify its final size and SHA-256 before execution.
Publishing this repository does not update the production launcher.

## Build and release status

These files are not the full Toolbox core. The local build requires a separately
provided Toolbox source tree, verified compatible Electron runtime, Visual
Studio C++ tools and verified NSIS compiler. Integration dependencies are kept
outside this public source repository.

The complete local candidate is based on
`tera-classic-toolbox/tera-toolbox-playtera`, commit
`0d020efba36050019f7a36d3ab512aa397a628e1`. Original Toolbox authors include
Caali, Pinkie Pie, meishu, JKQ and other component contributors. Existing third-
party licenses and notices must be preserved; no blanket third-party ownership
or redistribution permission is asserted here.

Player-release prerequisites remain open: evidence of redistribution rights
and required source/notice coverage for the upstream core, client-interface
DLLs and scanners; obsolete Electron runtime/native ABI review; complete-
installer game acceptance; final binary signing and launcher hash pinning.

No Actions workflows or automatic builds are included.

Support: https://playtera.to
