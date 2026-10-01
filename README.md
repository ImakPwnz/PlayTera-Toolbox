# PlayTera Toolbox

Toolbox distribution and installer tooling for PlayTera (client 46.05).

## Download

[Download PlayTeraToolboxSetup.exe](https://github.com/ImakPwnz/PlayTera-Toolbox/releases/download/v1.0.0/PlayTeraToolboxSetup.exe)

[Release notes and checksums](https://github.com/ImakPwnz/PlayTera-Toolbox/releases/tag/v1.0.0)

Version 1.0.0 is a **pre-release**, not a completed production rollout.
Installer: 65,383,247 bytes; SHA-256:
`a66f485bd504809694061abd20e08c49935bbb4efb4e54e53f8566191d22de01`.

Install into a new, empty normal folder, preferably
`<game folder>\PlayTera Toolbox`. Existing Toolbox installations and player mods
are not overwritten. Start `PlayTeraToolbox.exe` before launching the game.

The installer contains Toolbox runtime files and dependencies. It does not
contain game DLLs, d3d9/ExitLag project sources, an ExitLag SDK, private keys,
private launcher sources, operational data or player data. The matching game
client integration is supplied separately by PlayTera.

## Validation and limitations

- Twelve isolated installation tests passed; 222 installed files verified.
- The installer is not Authenticode-signed. Do not disable Windows security
  checks to install it.
- The bundled Electron 16.0.2 runtime is obsolete. A supported runtime and
  compatible native components remain follow-up work.
- Complete-installer game acceptance remains pending; isolated tests do not
  establish compatibility with game protection or anti-cheat systems.
- Toolbox core self-updates remain disabled. No unsigned stable feed is enabled.
  Module updates are separate; modules execute code.
- This GitHub release does not distribute or activate a new production launcher.
  The prepared launcher verifies the fixed URL, final installer hash and size.

## Sources and attribution

Current source branch contains only Toolbox distribution/updater/build tools.
The complete installer includes the Toolbox JavaScript sources and existing
component notices. The release also provides the exact bundled Toolbox network
proxy JavaScript sources and GPL-3.0 license as `tera-network-proxy-source.zip`.
Excluded integration sources are not reintroduced into the current branch.
Earlier commits have not been rewritten.

Basis: `tera-classic-toolbox/tera-toolbox-playtera`, commit
`0d020efba36050019f7a36d3ab512aa397a628e1`. Original authors include Caali,
Pinkie Pie, meishu, JKQ and other component contributors. Existing component
licenses/notices are retained; no blanket third-party ownership is claimed.

Build tools require a separate Toolbox source tree, compatible verified runtime,
Visual Studio C++ tools and a verified NSIS compiler. No Actions workflows or
automatic builds are included.

Support: https://playtera.to
