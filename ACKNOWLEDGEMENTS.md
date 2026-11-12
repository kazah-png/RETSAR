# Acknowledgements and provenance

RETSAR is an independent project maintained by [kazah-png](https://github.com/kazah-png). Its scope is general-purpose Windows desktop workspaces. Its original code and visual assets are covered by the [MIT license in this repository](LICENSE).

## Tools and assistance

| Project or assistance | Acknowledgement | Source and license |
| --- | --- | --- |
| **Ghidra** | Used during technical research and analysis of native interfaces and behavior. | [NationalSecurityAgency/ghidra](https://github.com/NationalSecurityAgency/ghidra), [Apache-2.0 license](https://github.com/NationalSecurityAgency/ghidra/blob/master/LICENSE) |
| **REA — Reverse Engineer Anything** | Agent-assisted research tools and a reference for the analysis workflow. | [morluto/rea](https://github.com/morluto/rea), [MIT license](https://github.com/morluto/rea/blob/main/LICENSE) |
| **GPT-6 Astra ULTRA** | Assistance with analysis, design, implementation, review and documentation. This model/configuration name is attributed to the maintainer's description; the repository does not independently verify that identifier. | AI assistance, not an executable dependency or an organizational endorsement. |

The Ghidra and REA licenses were checked in their original repositories. Those projects may contain components under additional licenses; the links identify their main project licenses.

## Scope of these credits

Ghidra and REA are research tools, not components redistributed by RETSAR or prerequisites for its build. This repository does not incorporate their source code, binaries or decompiled third-party software. RETSAR's MIT license does not purport to relicense anyone else's material.

The names and links recognize contributions to the development process. They do not imply affiliation, sponsorship or validation of RETSAR. Reviewing changes and describing their limits remain the project's responsibility.

## Technical documentation

The architecture cites primary Microsoft documentation for the APIs used. References support review of specific decisions; they do not replace Windows testing or independently demonstrate the viability of the pending multiseat provider.

The [Anode child-client implementation](https://github.com/skulitom/Anode/blob/main/src/Anode/Daemon/RdpViewer.cs) was consulted to diagnose the explicit CredSSP requirement. RETSAR's OLE host and laboratory client are original code against Microsoft's documented interfaces; no Anode source or binary is redistributed.

## Original visual identity

The two-desktop symbol, wordmark, repository banner and native interface visual system are original RETSAR work. Their editable vector sources are in [assets](assets), with usage and color definitions in the [visual system](docs/DESIGN.md). They do not incorporate the logos of research tools, operating-system vendors or applications used during validation.
