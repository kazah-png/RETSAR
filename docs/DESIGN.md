# Visual system

RETSAR's identity represents distinct desktop workspaces sharing one computer. The interface and repository use the same visual language: quiet surfaces, clear hierarchy and a small number of meaningful accents.

## Identity assets

| Asset | Size | Use |
| --- | --- | --- |
| [Symbol](../assets/retsar-mark.svg) | 256 × 256 | Application branding and compact project identity |
| [Monochrome symbol](../assets/retsar-mark-mono.svg) | 256 × 256 | Single-color treatments on dark surfaces |
| [Raster icon](../assets/retsar-icon.png) | 256 × 256 | Raster contexts using the canonical symbol |
| [Windows icon](../assets/retsar.ico) | 16, 24, 32, 48, 64, 128 and 256 px | Embedded application icon |
| [Wordmark](../assets/retsar-wordmark.svg) | 640 × 160 | Horizontal product identification on dark surfaces |
| [Repository banner](../assets/retsar-banner.svg) | 1440 × 480 | GitHub README and project introduction |

The original symbol contains two desktop frames with a deliberate separation at their intersection. The cyan and mint accents identify distinct workspaces. It is not an application, game or hardware-vendor logo.

Keep the symbol's proportions and separation intact. Allow clear space of at least one stroke width around it. Use the wordmark and banner at sizes where their text remains readable. The monochrome artwork uses a light stroke for dark surfaces; a dark single-color equivalent may be derived for a light background without changing the geometry.

All original identity artwork is included under the repository's [MIT license](../LICENSE). There are no embedded external images, fonts or network resources in these SVG files.

## Color

| Role | Value | Use |
| --- | --- | --- |
| Canvas | `#0B1020` | Main background |
| Surface | `#151E31` | Cards, navigation and grouped controls |
| Border | `#253249` | Low-emphasis separation |
| Primary text | `#EFF4FC` | Headings and important values |
| Secondary text | `#9BADCE` | Descriptions and supporting state |
| Mint | `#5DE4C7` | Primary actions and confirmed active state |
| Cyan | `#79CFF2` | Workspace identity and secondary emphasis |
| Amber | `#F2CB81` | Preview or attention state |
| Error | `#F28B98` | Failed operations requiring attention |

Color supports a written status; it never carries the entire meaning. Disabled controls must remain distinguishable from enabled actions. Focus indicators should remain visible on both the canvas and surface colors.

## Typography and layout

The Windows interface uses **Segoe UI**, with the system sans-serif fallback. Public SVG assets use `Segoe UI, Arial, sans-serif`; they do not redistribute fonts.

Use a small hierarchy: product identity, view title, section label, body copy and supporting detail. Align text and controls on a consistent spacing grid. Leave room for longer state messages, high DPI and window resizing. Prefer a concise explanation beside a blocked action over a dense technical warning panel.

The management console uses **Overview**, **Displays** and **Activity** as its primary views. Overview answers whether a desktop is available and what action is possible. Displays presents observed output information without pretending assignment is implemented. Activity records meaningful events and outcomes.

## Interaction and motion

Use native keyboard-operable controls with clear focus and sensible tab order. Refreshes should preserve the user's current view. Background observation should not block painting or input. Repaint when state or layout changes, rather than continuously animating an idle dashboard.

Do not add decorative progress loops that imply an operation is running. Samples, unknown values and pending capabilities must be labeled explicitly. Screenshots in documentation must identify whether they show the actual application or the design-preview fixture.

## Language

Public interface text, documentation and repository templates are in English. Use “desktop” for the everyday workspace, “viewer” for the window displaying it, and “seat” when discussing explicit display/input assignment. Distinguish “disconnect” from “sign out.”

Use concrete verbs such as **Refresh**, **Show desktop** and **Open desktop**. Keep internal API names, process IDs and implementation details in diagnostics unless they are necessary to resolve the user's current problem.
