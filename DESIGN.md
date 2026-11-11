---
name: Stellar
description: A calm neutral-black agent workspace for directing one lead, inspecting real delegation, and reviewing workspace knowledge.
colors:
  accent: "#E8794A"
  accent-text: "#101010"
  link: "#E88B5B"
  success: "#65C18C"
  warning: "#E5B567"
  danger: "#F07575"
  window: "#000000"
  panel: "#090909"
  input: "#111111"
  text: "#F2F2F0"
  text-disabled: "#8E8E8A"
  text-placeholder: "#9B9B97"
  border: "#252525"
  control: "#151515"
  control-hovered: "#1D1D1D"
  control-pressed: "#292927"
  control-border: "#343432"
  selection: "#262626"
  row-hovered: "#171717"
  track: "#262626"
  handle: "#737373"
  header: "#0A0A0A"
  tab-active: "#202020"
  popup: "#111111"
  popup-border: "#333333"
  section-label: "#92928E"
  muted-label: "#A3A39F"
  surface: "#070707"
  tabs: "#030303"
  section-header: "#090909"
  section-border: "#222222"
  tool-border: "#606060"
  composer-border: "#303030"
  activity: "#0C0C0C"
  activity-border: "#606060"
  status-bar: "#050505"
  status-border: "#202020"
  proposal-bar: "#15110E"
  proposal-border: "#4B3022"
  send: "#2B2B2B"
  send-hovered: "#3A3A3A"
  send-pressed: "#474747"
  send-text: "#F5F5F2"
  syntax-function: "#D8D8D4"
  syntax-strong: "#FFFFFF"
typography:
  body:
    fontFamily: "Roboto, sans-serif"
    fontSize: "13px"
    fontWeight: 400
  message:
    fontFamily: "Roboto, sans-serif"
    fontSize: "14px"
    fontWeight: 400
  title:
    fontFamily: "Roboto, sans-serif"
    fontSize: "22px"
    fontWeight: 400
  section-title:
    fontFamily: "Roboto, sans-serif"
    fontSize: "17px"
    fontWeight: 400
  activity-title:
    fontFamily: "Roboto, sans-serif"
    fontSize: "15px"
    fontWeight: 400
  header-brand:
    fontFamily: "Roboto, sans-serif"
    fontSize: "14px"
    fontWeight: 400
  subtitle:
    fontFamily: "Roboto, sans-serif"
    fontSize: "12px"
    fontWeight: 400
  label:
    fontFamily: "Roboto, sans-serif"
    fontSize: "11px"
    fontWeight: 400
  data:
    fontFamily: "Roboto Mono, monospace"
    fontSize: "11px"
    fontWeight: 400
  message-role:
    fontFamily: "Roboto, sans-serif"
    fontSize: "10px"
    fontWeight: 400
  code:
    fontFamily: "Roboto Mono, monospace"
    fontSize: "13px"
    fontWeight: 400
rounded:
  none: "0px"
  button: "8px"
  control: "10px"
  message: "12px"
  surface: "14px"
  proposal: "10px"
  tool: "10px"
  graph-node: "11px"
  circular: "17px"
spacing:
  hairline: "1px"
  compact: "3px"
  gap: "8px"
  row-gap: "8px"
  section-gap: "10px"
  control-x: "11px"
  card-gap: "12px"
  panel: "14px"
  region: "16px"
  transcript-gap: "18px"
  workspace: "24px"
  transcript-x: "26px"
  transcript-y: "30px"
  row-height: "36px"
  control-height: "34px"
  header-height: "44px"
  splitter: "3px"
  composer-editor: "88px"
  editor-inset: "0px 14px 14px 14px"
  graph-height: "360px"
  graph-grid: "24px"
  graph-row: "140px"
  content-max: "860px"
  user-message-max: "680px"
components:
  button-primary:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.accent-text}"
    typography: "{typography.body}"
    rounded: "{rounded.button}"
    padding: "7px 11px"
    height: "{spacing.control-height}"
  button-primary-hover:
    backgroundColor: "{colors.accent}"
    textColor: "{colors.accent-text}"
    typography: "{typography.body}"
    rounded: "{rounded.button}"
    padding: "7px 11px"
    height: "{spacing.control-height}"
  button-flat:
    backgroundColor: "transparent"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.button}"
    padding: "7px 11px"
    height: "{spacing.control-height}"
  button-flat-hover:
    backgroundColor: "{colors.control-hovered}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.button}"
    padding: "7px 11px"
    height: "{spacing.control-height}"
  button-send:
    backgroundColor: "{colors.send}"
    textColor: "{colors.send-text}"
    typography: "{typography.body}"
    rounded: "{rounded.circular}"
    padding: "0px"
    height: "34px"
    width: "36px"
  input-navigation:
    backgroundColor: "{colors.input}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "7px 11px"
    height: "{spacing.control-height}"
  input-composer:
    backgroundColor: "{colors.input}"
    textColor: "{colors.text}"
    typography: "{typography.message}"
    rounded: "{rounded.surface}"
    padding: "14px"
  card-message-user:
    backgroundColor: "{colors.control}"
    textColor: "{colors.text}"
    typography: "{typography.message}"
    rounded: "{rounded.message}"
    padding: "11px 14px"
  message-lead:
    backgroundColor: "{colors.window}"
    textColor: "{colors.text}"
    typography: "{typography.message}"
    rounded: "{rounded.none}"
    padding: "0px 4px"
  tab-active:
    backgroundColor: "{colors.tab-active}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.none}"
    padding: "7px 11px"
  list-row-selected:
    backgroundColor: "{colors.selection}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.none}"
    padding: "7px 11px"
    height: "{spacing.row-height}"
  tool-header:
    backgroundColor: "{colors.section-header}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.tool}"
    padding: "12px 14px 10px 14px"
  activity-card:
    backgroundColor: "{colors.activity}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.surface}"
    padding: "14px"
  proposal-ready:
    backgroundColor: "{colors.proposal-bar}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.proposal}"
    padding: "14px"
  graph-node:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.graph-node}"
    padding: "10px 13px"
  wiki-link:
    backgroundColor: "transparent"
    textColor: "{colors.link}"
    typography: "{typography.body}"
  knowledge-record:
    backgroundColor: "{colors.panel}"
    textColor: "{colors.text}"
    typography: "{typography.body}"
    rounded: "{rounded.none}"
    padding: "12px 16px"
---

# Design System: Stellar

## Overview

**Creative North Star: "The Quiet Agent Workspace"**

Stellar is a calm native command surface for directing one lead agent, reading a focused transcript, inspecting actual subagent state, and reviewing durable knowledge changes. Earth Dark establishes the default identity with forest-black planes, soft-white type, restrained green rules, and ocean-and-forest signals. The user-pinned Interstellar direction is translated into native Raven widgets rather than browser styling.

Navigation, conversation, and live delegation share one working screen without exposing three competing tab systems. The left navigator names user goals; the dominant center holds one task at a time; the right inspector appears only when activity, notes, knowledge, alerts, or a workflow step need it. Empty space remains dark. Only content-bearing tools such as the composer, activity summary, live map, and proposal notice receive rounded surfaces. Native graph motion communicates execution state, never decoration.

**Key Characteristics:**
- Forest-black structural planes with soft-white reading text and ocean-and-forest activity accents.
- Compact Roboto interface type, 14px transcript copy, and Roboto Mono for paths, code, and review data.
- A unified 60px header, one plain-language left navigation, a central task switcher, and a contextual right inspector that starts folded.
- An 860px-centered transcript and floating composer, compact user bubbles, and plain lead messages.
- Rounded content-bearing tools against square structural regions and one-pixel gray rules.
- Five Interstellar appearance families with fifteen Dark, Light, and Glass variants, native frosted and tinted materials, independent zoom controls, and system reduced-motion support.

## Colors

Earth Dark draws its identity from ocean and forest. Each planet uses its own accent for action while preserving readable, palette-matched neutral surfaces.

### Primary
- **Planet Accent** (`accent`): Primary commitments, keyboard focus, active-tab indicators, lead and subagent activity, proposal emphasis, and graph identity. Primary buttons pair it with **Primary Action Ink** (`accent-text`) for readable contrast.
- **Ready Green** (`success`): Success semantics and positive syntax/string content.

### Secondary
- **Ocean Link** (`link`): Data labels, paths, wiki and external links, syntax types, and delegation-related navigation.

### Tertiary
- **Amber Note** (`warning`): Warning semantics and numeric syntax.
- **Soft Alert Red** (`danger`): Errors and failed states, always accompanied by readable text.

### Neutral
- **Dark Window** (`window`): The center workspace, plain lead messages, graph field, and unoccupied space.
- **Deep Panel** (`panel`, `section-header`): Side-panel content, graph nodes, settings groups, and editor/review headers.
- **Inset Input** (`input`): Search fields, editors, popups, and the floating composer.
- **Soft White** (`text`, `syntax-strong`): Principal reading copy and strong Markdown emphasis.
- **Muted Botanical** (`muted-label`, `section-label`, `text-placeholder`, `text-disabled`): Secondary explanation, compact labels, placeholders, and disabled content respectively; the latter two are not for essential reading.
- **Rule Green** (`border`, `section-border`, `control-border`, `tool-border`, `composer-border`, `activity-border`, `status-border`): One-pixel separation, with stronger measured boundaries around floating tools.
- **Neutral State** (`control`, `control-hovered`, `control-pressed`, `selection`, `row-hovered`, `tab-active`): User bubbles and progressively explicit control, hover, pressed, selected, and active states.
- **Deep Structure** (`surface`, `tabs`, `header`, `status-bar`, `track`, `handle`): Navigation planes, tab strips, header/status chrome, and scrollbar anatomy.
- **Forest Proposal** (`proposal-bar`, `proposal-border`): A restrained review-ready notice, used only when a proposal exists.
- **Send Utility** (`send`, `send-hovered`, `send-pressed`, `send-text`): The circular neutral send control and its interaction ramp.

Tool headers and Activity share one stronger, planet-specific border treatment. The three structural Settings groups instead remove their outer frames while retaining section-header backgrounds with 10px rounded corners.

**The Planet Signal Rule.** Each planet's accent marks action, focus, links, or live execution. Palette-matched neutral values carry the workspace itself; never fill empty regions with the accent or use it as general decoration.

### Appearance families and variants

Five Interstellar appearance families provide fifteen variants. Earth Dark is the default and Earth appears first in the shared gallery, followed by Miller, Mann, Edmunds, and Gargantua. Ledger and High Contrast themes are disabled: their files are not loaded, their CLI aliases are rejected, and `select_theme` accepts only planet variants.

- **12 / 13 / 14 — Earth Dark / Light / Glass** (`earth-planet.xml`, `earth-planet-light.xml`, `earth-planet-glass.xml`): forest-black, pale botanical, or frosted forest-glass surfaces with ocean-blue links and green commitments.
- **3 / 8 / 15 — Miller's Planet Dark / Light / Glass** (`miller-planet.xml`, `miller-planet-light.xml`, `miller-planet-glass.xml`): ocean-navy, pale-cyan, or frosted ocean-glass surfaces with cyan commitments.
- **9 / 4 / 16 — Mann's Planet Dark / Light / Glass** (`mann-planet-dark.xml`, `mann-planet.xml`, `mann-planet-glass.xml`): deep blue-black, icy light, or frosted glacial-glass surfaces with glacial actions.
- **10 / 5 / 17 — Edmunds' Planet Dark / Light / Glass** (`edmunds-planet-dark.xml`, `edmunds-planet.xml`, `edmunds-planet-glass.xml`): dark umber, warm sand, or frosted umber-glass surfaces with earthen actions.
- **6 / 11 / 18 — Gargantua Dark / Light / Glass** (`gargantua.xml`, `gargantua-light.xml`, `gargantua-glass.xml`): near-black brown, warm ivory, or frosted amber-glass surfaces with accretion-disk amber.

The existing internal indices remain stable for the original variants; Glass uses 14–18. `appearance.toggle` switches partners 12↔13, 3↔8, 9↔4, 10↔5, and 6↔11; from Glass it opens the corresponding Light variant (14→13, 15→8, 16→4, 17→5, 18→11). The unified header action shows a sun in dark or glass mode and a moon in light mode, with tooltip and accessible name updated to the destination mode. Settings provides the same paired action and opens the shared five-world gallery instead of maintaining a separate theme ComboBox or duplicate preview cards.

The carousel marks the selected finish with its primary button style. Theme changes preserve the active session and immediately restyle existing transcript entries, proposal surfaces, Activity markers, agent-kind labels, status labels, and lead/subagent graph nodes, including active-state colors. Structural Border outlines linked to the theme through `BorderWidth` also resolve from the active variant on every paint. Explicit `BorderColor` values and colors set through `Rborder_set_outline` remain fixed.

At launch, `--theme earth|earth-light|earth-glass|miller|miller-light|miller-glass|mann|mann-dark|mann-glass|edmunds|edmunds-dark|edmunds-glass|gargantua|gargantua-light|gargantua-glass` selects a planet variant; non-planet aliases fail with an explanatory error. `--memory` opens Memory Universe directly and bypasses the opening splash and score, matching the existing `--endurance` launch behavior.

Every planet variant shares the refined header/component rules: transparent header actions, palette-matched header surfaces, 34px controls, 36px rows, 3px splitters, 11×7px control padding, 8px spacing, 8px buttons, 10px tool-header corners, 14px composer/Activity corners, transparent composer inputs, and themed send/Activity/proposal surfaces. Families retain their distinct palettes, not different component geometry. The carousel's 104px-wide finish buttons keep full Dark/Light/Glass labels readable at 200% zoom.

Glass variants inherit their family's dark palette and reveal a restrained planetary wallpaper with three atmospheric color fields over a dark base. Layout boxes, scrollers, lists, file trees, and editing canvases use transparent solid materials instead of inherited glass, eliminating artificial full-pane rectangles; sidebars retain an 85% family-tinted structural wash. Raven's native Vulkan frosted material is reserved for neutral composer and Activity surfaces, with tinted glass for actionable proposals. Refraction, rim, saturation, and shadow are deliberately reduced. Secondary text uses family-matched high-contrast foregrounds, and the composer editor suppresses its nested glass surface so only the outer boundary remains. Wiki and Knowledge headers and footers are transparent rails rather than framed boxes. The world gallery uses its own opaque `gallery-backdrop`, so the working interface never bleeds through its preview deck. Structural tab strips use transparent Header and TabActive colors, retain one Separator divider, show a 2px Accent underline, and reserve an 8% family-tinted HeaderHovered wash for hover; lead transcript messages remain solid and fully unboxed rather than acquiring a glass rim. Composer focus uses the outer surface's opacity instead of drawing a second accent ring around the writing area; the text caret remains visible. The window itself is not transparent, preserving a stable working backdrop and readable text.

## Typography

**Display Font:** Roboto (embedded regular, with native variants for Markdown emphasis)
**Body Font:** Roboto (embedded regular)
**Label/Mono Font:** Roboto and embedded Roboto Mono, according to content role

**Character:** Quiet, compact, and reading-led. Proportional Roboto carries controls and prose; monospace is reserved for paths, code, keyboard references, line-numbered material, and data that benefits from alignment.

### Hierarchy
- **Title** (regular, 22px): The Settings heading.
- **Section title** (regular, 17px): Agent, file, note, and proposal headings.
- **Activity title** (regular, 15px): The Orchestrator heading in the Activity card.
- **Header brand / run title** (regular, 14px): STELLAR and the current run in the unified header.
- **Message** (regular, 14px): Wrapped user, lead, pending, and error transcript content.
- **Body** (regular, 13px): Controls, lists, navigation, wiki reading/editing, and general settings content.
- **Subtitle** (regular, 12px): Secondary run context and expandable work-activity steps.
- **Label / data** (regular, 11px): Compact status, activity metadata, section labels, paths, shortcuts, tool-failure details, and graph summaries; data and keyboard material use Roboto Mono.
- **Message role** (regular, 10px): Short lead-side eyebrows such as READY, ORCHESTRATOR, STATUS, ERROR, and RUN ERROR, plus work-step state. User bubbles hide this heading.
- **Code** (Roboto Mono, regular, 13px): Editors, code, and exact review material.

**The Reading First Rule.** Use compact type to increase scan density, not to reduce legibility. Keep transcript prose at 14px, general interface copy at 13px, reserve 10px type for short role labels, and preserve wrapping for prose.

## Layout

The native window opens at 1600×960 with a minimum size of 1100×680 and starts at 115% interface scale. A unified 44px header contains navigation, STELLAR, a workspace selector that also opens workspaces, run context, New chat, Activity, the paired appearance toggle, conditional Retry/Stop, elapsed time, and run state. Workspace names and run titles are shortened with full tooltips so they cannot collide with run controls. Above 150% interface zoom, contextual text hides to preserve the actions. A compact status strip spans the bottom with textual feedback and the current-theme button. The world selector fills the window when open.

Between them, one splitter defines up to three resizable columns with 3px splitter targets. At startup, the left share is calculated from the framebuffer's logical width and current UI scale to fit navigation at its 240px minimum rather than expanding toward the 310px maximum on wide displays. The contextual right column starts folded, leaving the navigation and current task as the initial two-column composition.

- **Left navigation (240–310px):** Work contains Chat, Tesseract UI, and Workflows; Workspace contains Files, Agents, Review, Knowledge, and Worktrees; Settings sits at the bottom. Only the selected destination receives an 8px-corner Selection fill, with immediate RowHovered and native ControlPressed feedback and no outline; navigation hover does not use a spring transition. This is a fixed navigation rail, not a second inspector: Recent chats and their search appear only while Chat is active, and every other task collapses that list and leaves quiet space.
- **Center workspace (minimum 420px):** Native switchers select Agent, Editor, Endurance, secondary resources, Review, or Tesseract without drawing global tabs. Agent contains an independently scrolling transcript above the floating composer, with a conditional follow-up queue bar between them. Files places a 260px native file browser beside the source editor; Definitions places a 240px searchable agent list beside its definition editor. Both use the center rather than duplicating navigation in the left rail. Endurance retains task-local Canvas, YAML, Runs, and Live tabs; Review retains Code changes and Knowledge; Memory and Worktrees open through Knowledge and their own destination respectively.
- **Right inspector (300–380px):** A 17px title, concise subtitle, Alerts shortcut where relevant, and Close sit above one native switcher. Activity holds lead/subagent state, run usage, and conditional review notices; Notes holds a searchable 130px note list, New, Markdown editing, and preview in one place; Knowledge holds entities, relations, and the Universe action; Alerts holds persistent notification history; Workflow step holds the selected Endurance node inspector. No tab strip appears.

The transcript is not a sequence of full-width message cards. User entries align right within the reading column and are capped at 680px; lead-side entries use the available reading width without bubble chrome. The composer is centered beneath that column rather than docked across the entire center pane. Pasted images appear above the prompt as a 76×60px rounded thumbnail with dimensions, encoded size, and a Remove action; the collapsed state leaves no attachment chrome.

Both sidebars fold independently with `Ctrl+B` and `Ctrl+Shift+B`. Above 150% interface zoom they fold automatically so the left destinations remain reachable, then restore their previous visibility when zoom decreases. Collapsed panes take no layout space and the center reflows. Folding preserves each pane's measured logical-pixel width as its slot coefficient; the center absorbs all space from a collapsed pane, and reopening subtracts the same width again so sidebars do not jump between modules. The status strip confirms the result. There are no web breakpoints or mobile navigation variants; native resizing, pane minimums, zoom-sensitive density, and folding define adaptation.

## Elevation & Depth

Structural depth comes from adjacent neutral values, one-pixel rules, inset fields, and generous black negative space. Ordinary transcript content, tabs, lists, settings groups, and editors have no ambient shadows. The composer, tool headers, and Activity tools appear floating because their rounded silhouettes and stronger borders contrast with the square workspace, not because of decorative shadow or glow. The world selector uses a full-window fade and deck motion instead of a floating shadowed panel.

The native read-only graph remains the bounded exception: nodes may use resting separation, spring-driven hover/selection lift, and a running halo. These effects communicate execution state and remain inside the graph.

**The Floating Tools Rule.** Reserve rounded tonal lift for content-bearing composer, tool-header, activity, map, and proposal surfaces. The world selector instead fills the window with a quiet backdrop and an animated central preview deck. Do not add ambient shadows or decorative glow to ordinary panels or transcript messages; graph effects must remain stateful and textual.

## Shapes

All planet variants share square structural panels, tab strips, and lead messages (0px). List surfaces, selections, and hover rows use a 10px radius with a compact inset. Most section-header groups remain square; the three Settings groups instead use borderless section-header surfaces with 10px rounded corners. Editor, Definitions, and Review tool headers use the same surface as section-header groups but gain a 10px radius and stronger measured border. Standard buttons are gently rounded (8px), general controls use a 10px base radius, and user bubbles are compact rounded forms (12px). Composer and Activity/map surfaces share a clearly rounded 14px silhouette; proposal notices use 10px. Carousel previews stay rectangular: horizontal translation, scale, and dimming create depth without skewing the cards or their content. The 36×34px send control is circular in practice (17px radius).

Most divisions are one-pixel theme-colored strokes. Graph nodes retain an 11px native silhouette, small pill badges, circular pins, and curved delegation wires. Variants change border and surface colors with their palettes while preserving the shared component geometry.

## Components

### Control rhythm
Every control is 30px: buttons, text inputs, combo boxes, and icon buttons (`ControlHeight="30"`, 5px vertical padding in every theme), so a row of mixed controls shares one top and bottom edge. Buttons that end a stack of rows share one width so the fields beside them end on one line (Review: 116px). Tab views sit on the content edge (14px), and tabs are text with an accent underline on a single divider, with no strip or active fill.

### Pane limits
The navigation sidebar stays between 240 and 310px and the right panel between 300 and 380px, both while dragging and when the window grows; the center takes the rest and never drops below 420px. Stellar drags the dividers itself so the limits apply on the same frame (no shaking at a limit). Canvases never reframe while their size changes: Tesseract keeps its zoom and center, and the workflow graph reframes once the width has settled for 300ms. Fit frames Tesseract designs between the title and frame count (top 60px); the canvas shows no shortcut hint line, the shortcuts live in its accessible description and in DESIGN.md, and the prompt box (bottom 156px).

### Wormhole
The sixth world, after Gargantua: the neutral one. Graphite surfaces, one blue accent (#4C9AFF dark, #0A6CDB light), and no tinted chrome, in Dark, Light and Glass like every world. Its art in the gallery is the wormhole itself, rendered by the planet shader as a glass sphere that lenses the galaxy on its far side: the bend grows toward the rim, a thin Einstein ring wraps the edge, and the image swirls slowly around the darker throat. Wormhole Light keeps the lens darker than its surroundings so it still reads as glass.

### Platform conventions
The window follows its host. On macOS the close, minimize and zoom buttons are the round coloured buttons on the left of the header, and shortcuts are written with Apple's symbols in Apple's order (⇧⌘P, ⌘↩, ⌥⌘A), since Cmd acts as Ctrl. On Windows and Linux minimize, maximize and close sit on the right and shortcuts read Ctrl+Shift+P. Layout and code write shortcuts the portable way; `native_shortcut_text` converts every tooltip and shortcut label at startup and whenever a shortcut is shown.

### Copy answer
Lead answers end with the copy glyph in a 26px flat button whose icon lines up with the text above, at 62% opacity until hovered. After a copy it shows a check for 1.5 seconds and its tooltip reads Copied.

### Header
The unified header is 44px. Its icon buttons are 30×28px with 16px icons and its text actions 28px, so it stays a thin strip above the work.

### Sidebar grid
One left edge for everything in the sidebar: an 8px outer gutter and a 10px inner inset put section labels, destination icons, the list heading, the search text and chat titles all at 18px. Destinations are 32px rows with 2px gaps; the list heading's Archived toggle and archive icon are 28px, 11px text, right-aligned. The chat search is a plain 30px field with no external magnifier.

### Composer and status bar
The chat composer is compact: 12×10px shell padding, a 40px (two-line) editor that grows 20px per line up to 140px, 4px between the editor and its toolbar, and 30px icon buttons. The status bar's theme chip sizes to its content and sits flush with the right edge (8px), with a 16px planet.

### Files explorer
The tree and the agent list beside their editors fold away with the panel toggle to the left of the view title (same glyph as the navigation toggle), giving the editor the full width.

A code-editor tree directly on the workspace surface: no box, header, path field, or back/forward/up controls. Folders come first, then files, both in case-insensitive order; hidden entries are left out. Rows are 26px with a 14px indent, chevrons for folders, an accent folder glyph and a quieter file glyph (drawn at runtime in the theme's Accent and Text colours). One click opens a file or toggles a folder; folders load lazily and stay open across refreshes. Status messages show workspace-relative paths.

### Buttons
Every button is one of three variants, 30px tall with 7px corners and 12×5px padding (set once per theme on `Widget="Button"`).
- **Primary:** Accent fill with AccentText. At most one per view, for its main commitment: Save file/agent/note, Run workflow, Create and run, Apply to workspace, Accept and write, Settings Done.
- **Secondary (Default style):** Control fill with a 1px ControlBorder. Every other text action in content: New, Duplicate, Validate, Fit, Add step, Restore task, Open file, Refresh diff, Pin, Review knowledge. A button that reads as text only is never a content action.
- **Destructive:** A secondary button with `Class="danger"`, whose label takes the theme's Danger colour: Delete agent, Discard mission, Forget memory, Reject, Discard, Delete, Forget. Never Primary.
- **Flat:** Transparent until hovered, only for toolbars and chrome: header actions (New chat, Activity, Retry, Stop), icon buttons, zoom controls, Close/Cancel/Skip, the inline Copy under answers, attachment Remove/Clear, and segmented toggles (Archived, Dark/Light/Glass). Navigation and Appearance icon buttons use 17px authored icons inside 36×34px targets.
- **Send:** A 36×34px circular icon button inside the composer, with a 16px send icon and accessible name `Send to lead`. Every variant supplies its Control, ControlHovered, ControlPressed, and Text colors. Send is disabled while the lead runs; Stop appears in the unified header only during a run.
- **Save file:** Code files autosave 900ms after an edit, so Save file is enabled only while there are unsaved changes.
- **Confirmations:** Confirmation dialogs name the action on the button (Delete agent, Restore files, Apply changes, Discard mission) next to a flat Cancel; never Yes/No. Safe actions use the Primary button and Enter confirms. Destructive actions use the Danger colour on a default button and Enter cancels; Escape always cancels.
- **State:** Preserve native hover, pressed, disabled, and the active variant's keyboard-focus feedback. Do not communicate errors, completion, or availability by color alone.

### Navigation, Search, and Lists
- **Unified header:** Compact 14px identity/run text and quiet transparent actions sit on a palette-matched neutral header rather than an accent-colored run banner. The workspace name is itself the Open workspace action. Workspace/run text uses bounded lengths and full tooltips; it collapses above 150% zoom instead of overlapping actions. Activity has a visible text label rather than an ambiguous panels icon.
- **Destinations and local tabs:** Global destinations are plain-language left buttons led by a 16px authored icon (`ui/icons/nav-*.svg`, 1.8 stroke, tinted with the text colour) 10px before the label, with transparent inactive backgrounds, Selection fill only on the current destination, 8px corners, 12px horizontal padding, and no border. Native switchers preserve the underlying view indices and deep links. Tabs remain only within a task: Endurance Canvas/YAML/Runs/Live, Review Code changes/Knowledge, nested Diff/Verification, and Notes Edit/Preview. They use the established 2px Accent underline and native spring motion unless reduced motion is requested.
- **Navigation search:** A 16px search icon precedes a field shown only for Recent chats, Agents, or Notes. Changes rebuild those lists while preserving their original item-to-model mapping. Empty searches restore all entries; Files and task-only canvases do not show an ineffective search field.
- **Lists:** Threads, Agents, and Wiki navigation ListViews are borderless (`Metric.BorderWidth="0"`) with no list fill (`nav-list` style: the panel colour at zero opacity, so unfocused selections keep the theme hue) and 36px rows. One heading row names the current list (Recent chats, Agent definitions, or Workspace notes) with its actions, above the search field. Chat rows show a short title followed by `running`, `failed`, or the age of the last real change (`now`, `5m`, `3h`, `2d`). Selected and hovered rows use the same radius with a 4px horizontal and 2px vertical inset; themed fill and keyboard behavior remain intact. Selecting a thread restores its transcript and session; activating an agent opens its definition; selecting a note opens the Notes inspector; the file tree opens text in Editor.
- **Conversation threads:** New chat in the header or `Ctrl+N` saves the current conversation and opens an empty one without creating an empty history entry. `.stellar/threads.json` atomically stores titles, messages, harness, transcript, session ID, recency, and archive state. The Recent chats header provides an Archived/Active view toggle and a 17px archive icon for the selected row; archiving the current thread opens a fresh composer, while restoring returns its complete history to the active list. Startup restores the latest active thread; active runs must finish before creating, switching, archiving, or restoring threads.
- **Settings and workspace profile:** The bottom Settings destination or `Ctrl+,` opens Settings and `Esc` returns to the previous workspace. Appearance and Agent Workspace remain visible; Keyboard & Shortcuts uses a collapsed expander so advanced preferences do not dominate the surface. Groups are borderless (`BorderWidth="0"`) with 10px rounded geometry (`CornerRadius="10"`), concise copy, and a Done action. `.stellar/profile.json` format 2 atomically persists zoom, theme, harness, run mode, per-harness model/effort, sidebars, vector recall, watch checks, opening-sound preference, and shortcuts. Legacy profiles migrate with the right inspector folded once, then retain the new preference.

### Opening Splash
- **Composition:** A borderless near-black scene opens the application with Dylan Thomas's complete six-stanza `Do not go gentle into that good night`. The 19-line poem sits left in self-hosted Noto Serif Display Light, with the opening `gentle` in warm italic, while the real procedural Gargantua shader emerges on the right above a restrained deterministic star field. Portrait layouts move the black hole below the complete text rather than compressing or overlapping it. The fonts ship locally with their OFL license; there is no external imagery, network request, card, panel, or fictitious loading indicator.
- **Original score:** The locally generated instrumental `Do Not Go Gentle` uses sparse piano, organ, atmospheric pads, and strings without sampling or naming an existing artist or composition. Its prompt, fixed seed, model, and CC-BY-NC-4.0 weight-license provenance ship beside the WAV. SDL2 plays the 32 kHz stereo asset directly at a restrained gain with a 2.8-second fade-in and 0.65-second fade-out; no media subprocess, network service, or AI runtime ships with Stellar. A bottom-left speaker control with an authored matching-stroke icon toggles sound, persists the workspace preference, and supports keyboard activation without dismissing the opening. Missing audio disables the control and reports that the score is unavailable.
- **Sequence and control:** The stanzas reveal in reading order over approximately six seconds, followed by the attribution, and the complete poem remains until the reader explicitly leaves. A native Skip button stays anchored at the bottom-right with initial keyboard focus; click, Enter, Space, or Escape also dismisses the opening, and `--prompt` bypasses both the opening and its score so automated runs are never delayed. Reduced motion presents the complete settled composition without shader or spatial animation and still waits for Skip. Once the attribution settles, the shared scheduler stops all opening redraws and waits without GPU animation; dismissing fades the score and restores composer focus.

### Application Icon
- **Ranger identity:** The user-supplied three-quarter Interstellar Ranger pixel art stands alone on transparency, with no background tile or border. Its segmented dark cockpit, broad off-white wing, and landing gear preserve the spacecraft's recognizable silhouette. Residual near-transparent pixels are removed before trimming; small launcher sizes use a full-square composition so the ship never collapses into a narrow strip, while large sizes preserve its original proportions.
- **Desktop integration:** The supplied Ranger source provides PNG sizes from 16 through 512 pixels and the embedded 64px window icon. `StartupWMClass=raven` associates running windows with the Stellar desktop entry.

### Visual World Selector
- **Current-theme button:** A compact 210px native button contains a 26×26px shader-rendered planet artwork and the current theme name. Clicking it or pressing `Ctrl+Shift+T` opens the world gallery. Settings uses that same gallery.
- **Immersive carousel:** `Choose your world` fills the window with a quiet workspace-colored backdrop. One large rectangular central preview carries a thin accent outline while the previous and next worlds remain visible as smaller, dimmed rectangular side previews. Horizontal translation, scale, and opacity create depth without skewing the cards or their content. Every card is a high-fidelity Stellar mockup with readable navigation, conversations, transcript copy, a Memory Universe scene, Activity status, composer, and status bar, using the candidate's actual XML palette, including inherited Glass colors and native frosted material. The planet remains a small identity cue in one workspace area rather than replacing the interface preview. Earth, Miller, Mann, Edmunds, and Gargantua form a wraparound deck inspired by Omarchy's focused theme-switching experience. Above 150% interface zoom, outer padding and the preview minimum contract so all controls remain reachable through 200% zoom.
- **Living worlds and motion:** A native Vulkan fragment shader evaluates every planet at display resolution, avoiding pixelated texture scaling and per-frame CPU generation. Each world has a distinct animated phenomenon rather than rotation alone: Earth advects independent cloud systems with shadows, atmospheric breathing, and a satellite; Miller combines a traveling large swell with smaller ripples and evolving foam; Mann drifts snow clouds beneath animated polar aurora and carries a satellite; Edmunds moves dust storms and atmospheric haze with a satellite. Spherical lighting, terminators, specular glints, and atmospheric rims provide volume. Gargantua is a transparent procedural 3D black hole rendered at display resolution: bent light rays integrate a turbulent accretion volume around a dark event horizon, with Doppler-brightened emission, lensed disk structure, a subtle photon glow, and slow camera drift. It uses no photographic texture or rectangular overlay. Navigation interpolates the deck with an exponential ease-out at a 30 FPS application cap; reduced motion snaps directly to the selected world and freezes shader time and satellites. The status-bar planet remains static so the working interface does not redraw continuously. Pointer movement only updates hover, never navigation; side clicks require a tracked press and a laid-out card.
- **Input, comparison, and state:** Left/Right arrows, Previous/Next, or clicking a side preview changes the family; Up/Down arrows or the explicit Dark, Light, and Glass buttons change its finish. Wheel navigation accepts any nonzero vertical delta but enforces a 300 ms cooldown and consumes the event, preventing trackpad or wheel bursts from cascading through themes. Auto-repeat is ignored so one key press advances one step. Every choice applies and persists immediately, updates the central family name, position, current-finish button, and current-theme artwork/name, and leaves the gallery open for comparison.
- **Dismissal and focus:** Escape or Close dismisses the gallery and returns focus to the current-theme button. Opening focuses the accessible carousel; changing themes alone does not dismiss it.

### Transcript Messages
- **User:** Compact right-aligned bubbles use the active theme's Control fill, a one-pixel Border outline, 12px corners, 11×14px padding, and a 680px maximum width. The YOU eyebrow is hidden.
- **Lead / status:** Plain lead-side messages use the Window background, no outline, square corners, and only 4px horizontal padding. A 10px Accent-colored role eyebrow remains visible. Consecutive lead messages share one eyebrow, and each lead answer ends with a small flat Copy action that copies its Markdown, since transcript text cannot be selected. Streaming lead text updates in place and the transcript follows the newest content.
- **Pending / reasoning:** A visible pending message is created before workspace preparation and process launch, then advances through STARTING, CONNECTED, and THINKING. It remains present while the model is reasoning and is replaced only by visible answer content or tool activity. Kimi ACP thought chunks update this pending state and status strip without exposing private reasoning. Answer chunks stream into ORCHESTRATOR, and Claude uses its native partial-message stream.
- **Work activity:** All tool calls and delegated work for one run share a single native Expander, collapsed by default. Its title shows the current action and short target while working, then a compact completion/failure summary. Expanded rows pair a consistent 16px authored action icon with a 12px action label, an 11px second-line path/command/query, and right-aligned running/done/failed/stopped text. Existing workspace files use Link-colored Markdown targets that open in Editor, with optional `path:line` cursor navigation; outside or missing paths never become file links. Kimi's late ACP `rawInput` updates the existing row rather than duplicating it. Raw tool output is never inserted into the main conversation; tool failures add a bounded detail inside the expandable region. A new run creates a new group without rewriting earlier history.
- **Coding contract:** Lead prompts require inspection, implementation, and relevant build/test verification for coding tasks rather than chat-only instructions. Final responses report the outcome, key changed files, and checks actually run without inventing edits or verification.
- **Arrival:** New entries use Raven's short native fade, subject to reduced motion. Do not add shadows or restore full-width card outlines.

### Composer
- **Structure:** A centered 860px-maximum floating surface with a themed inset fill, stronger border, 14px corners, and 14px internal padding. Its wrapping editor starts at 48px, grows to 68px for two lines and 88px for three or more before scrolling, and uses transparent input, border, focus, and control-border colors, leaving no separate visible field boundary. The complete empty composer occupies 120px before attachments, queue, selection, or activity rows.
- **Controls:** One 36×34px setup icon, the Recall history icon, and Send are the only persistent composer controls. Setup or `Ctrl+Shift+E` opens a 390px bottom-centered Run setup popover with four labeled rows in dependency order: Harness, Model, Effort, and Mode. Model and effort persist independently for Claude and Kimi, and all four selectors lock during runs. Setup closes through Done, Escape, submission, or another workspace overlay.
- **Behavior:** `Ctrl+L` focuses the composer. `Enter` sends or queues from either composer, `Shift+Enter` inserts a newline, and completion popups retain Enter-to-accept. `Ctrl+Enter` remains available as the global Send shortcut. Empty submissions keep focus and explain the issue in the status strip. Harness or mode selection starts a fresh session without deleting visible transcript content. Model and effort changes apply to the next run while preserving the resumable session. Build enables implementation, verification, checkpoints, and memory curation. Ask and Review use read-only instructions and harness restrictions, with no checkpoints, episodes, or wiki proposals. Slash commands require Build. `--mode build|ask|review` selects the same contract at launch. The placeholder advertises `@ files` for both harnesses and `/ skills` only when Kimi Code is selected; `@` file completion works with either harness. On macOS, Cmd works wherever Ctrl is shown, and editors follow macOS text navigation (Cmd+arrows for line and document ends, Option+arrows by word, Cmd+Delete to line start).
- **Queued follow-up:** During lead or knowledge work, the Send action becomes Queue a follow-up and one prompt appears in a 10px-rounded surface with a specific waiting state and Cancel. The state distinguishes active lead work, active knowledge review, lead failure, and a pending Wiki decision; a Review Wiki action appears when a proposal blocks progress. A second prompt cannot replace the first. The queued prompt starts only after successful work and knowledge review; pending proposals pause it until accepted or discarded. Stop cancels the queue, and lead failure leaves it paused. Header and Activity elapsed time continues throughout knowledge review.
- **Skills and commands:** With Kimi selected, `/` opens the native completion popup with common commands and indexed project/user Skills. `/skill:name` and the `/name` shorthand are supported, with descriptions beside filtered results. Slash input is delivered without the normal prompt prefix so Kimi executes it as a command.
- **Workspace files:** `@` filters indexed files relative to the workspace root and continues through nested directories such as `@src/`. Build output, VCS metadata, dependency directories, and symlinks are excluded. Enter, Tab, arrows, and mouse selection use native completion behavior; exact matches dismiss the popup. Popup opening is deferred one frame to avoid the editor's automatic post-change close.
- **Resource delivery and context budget:** Mentions are validated against the workspace index. A file icon, attachment selector, and per-file/total approximate token counts appear only when valid mentions exist. Remove deletes only the selected mention; Clear removes all valid mentions without damaging other prompt text. Compact retains the original task and up to six recent messages for the next fresh session while preserving the complete visible thread. Text files up to 512 KiB are embedded as ACP `resource` content; other files use `resource_link`. Claude receives explicit workspace-file references instead. Files outside the workspace are never attached.
- **Recall:** A 17px history icon beside Send or `Ctrl+R` opens a 680px bottom-centered prompt library with search, a native recommendation list, Pin/Unpin, and confirmed Forget. Existing chat prompts seed the first library; successful launches update exact-text reuse counts. Pinned prompts rank first, then frequency and recency. Enter restores the exact prompt into the composer without submitting, and Escape returns focus. The overlay narrows to 500px with a 170px list at high zoom. Data remains local in `.stellar/recall.json`; no model or network recommendation is implied.

### Command Palette and Global Search
- **Overlay:** A 640px rounded search overlay opens below the header with a search icon, contextual title, Close, one input, a result list, and concise keyboard guidance. Above 150% zoom it narrows to 500px and the result region to 170px. Escape restores focus to the originating tool; Down enters the result list and Enter activates the selected result.
- **Commands and files:** `Ctrl+Shift+P` filters application commands and Build/Ask/Review modes; `Ctrl+Shift+O` filters workspace-relative file paths case-insensitively. Commands reuse the existing application actions rather than duplicating behavior.
- **Content results:** `Ctrl+Shift+F` searches text with a 250 ms debounce and returns up to 80 `path:line` matches with readable previews. Only indexed workspace text files up to 2 MiB are scanned; Stellar state, VCS metadata, build output, dependencies, binaries, and symlinks are excluded. Activation opens the exact source line in Editor.

### Run Controls
- **Current action and time:** Activity carries a readable action and elapsed duration alongside harness/session context. The header repeats compact elapsed time and conditionally shows Retry after lead failure. Retry reuses the last prompt and resumable session. Timer wakeups occur four times per second and only while the lead runs; text changes once per displayed second.
- **Run receipt:** Activity reserves one compact data line for per-run cost and token usage. Claude's reported `total_cost_usd` appears with input tokens including cache creation/read and output tokens; Build runs aggregate the lead and knowledge curator reports into the same total and Alerts entry. No price is estimated locally. Kimi's undocumented per-run cost is labeled `not reported`. The identical receipt accompanies the run's Alerts entry for persistent history.
- **Run results and Alerts:** Lead completion and failure, verification failures, knowledge review readiness, mission applies, and autosave failures stay in the in-app Alerts history without creating desktop notifications. Autosave failures are throttled to one per minute, and Stop does not add an event. The newest 50 events persist in `.stellar/notifications.json` and appear newest-first in Alerts, with details and Open actions for the transcript, verification, proposal, mission, or editor. Run results do not alter transcript layout or steal focus.

### Isolated Worktrees
- **Workspace:** The Worktrees destination pairs a rounded tool header with compact task/repository controls, a 140px mission history, Refresh diff, Discard, and Apply actions, and a read-only full-diff editor. With no selected mission, creation controls and a concise empty/selection message remain while history, hunk, apply, and diff chrome collapses. Return to workspace appears only while a worktree is active. The command palette exposes the same destination.
- **Isolation:** Create and run is Build-only and starts the lead in a Git worktree under `.stellar/missions/`. Root and direct child repositories are supported. Shared agent/wiki context seeds the worktree but is excluded from patches; Stellar state and untracked binary artifacts never return.
- **Review and apply:** Mission diffs include tracked and untracked text files and truncate only the 2 MiB display. Apply requires confirmation, uses the complete patch plus `git apply --check`, and creates no commit or merge. A hunk selector applies individual changes while full Apply completes the remaining review. Applied hunk IDs persist and disappear from subsequent selections. Conflicts stop before writing, applied state persists, and a second full apply is blocked. Return preserves isolation; Discard permanently removes the worktree and unapplied changes only after a destructive confirmation.

### Activity and Live Map
- **Activity card:** A 14px-rounded surface with the same stronger one-pixel border as tool headers shows Orchestrator, lead Ready/Running/Stopped/Complete/Error state, harness context, and current action. Lineage and live-map controls stay collapsed until delegated work exists.
- **Real subagent rows:** Native subagent events add a 7px accent status marker, wrapped task title, agent name, and readable running/complete/failed/stopped state. The summary reports running count or total subagents. A single empty message explains that delegated work appears here. Demo content is explicitly labeled as demonstration data.
- **Central mission control:** Endurance → Live keeps a full-height read-only NodeGraph below a compact tool header with live state and Fit graph. Once subagents exist, Activity's `Open mission control` action opens and frames this canvas; the right panel never duplicates the graph.
- **Graph anatomy:** An accent Orchestrator lead delegates through curved wires to accent subagent nodes. Nodes and badges carry run/write/done/! plus running/complete/failed text. Flow is visible only during execution. Frame-all runs after graph changes and through Fit; the graph is not an editing canvas.
- **Motion:** Native springs control graph entrance, badges, selection, and hover; a running halo breathes only while active. Textual state parity remains equivalent for every animated signal.

### Endurance Orchestration
- **Workflow header:** A compact tool header pairs the workflow name and relative saved path with workflow selection, New, Duplicate, task input, Recurrence, Validate, and Run workflow. Controls use the existing 8px button geometry and theme palette. No decorative orchestration banner, hero metric, or separate card system is introduced.
- **Editable canvas:** Agent and approval nodes expose named dependency pins, agent/mode context, and textual status. Selection opens the compact Step inspector with ID, agent, harness, mode, isolation, dependencies, prompt, timeout, and retries. Move/connect/delete operations update the same strict YAML model; invalid cycles and parallel Build connections are rejected before insertion. The canvas becomes read-only during execution and flow appears only on active steps.
- **Adaptive DAG:** Wide workspaces use saved horizontal positions. Below 1100 logical units, steps reflow into a vertical dependency chain with tighter level spacing for readability; saved YAML coordinates are not rewritten by adaptation. Fit uses actual node bounds and the current viewport rather than forcing a fixed zoom.
- **YAML:** A line-numbered editor shares the workflow model with the canvas. Valid input applies and atomically saves; invalid input identifies the problem and preserves the last valid canvas. `Ctrl+S` validates immediately, while ordinary edits validate after 900 ms without stealing focus or rewriting on every keystroke.
- **Runs:** History and the selected immutable run snapshot expose textual queued/running/waiting/succeeded/failed/stopped/blocked states, attempts, outputs, errors, and checkpoints. Stop/Resume and Continue/Reject appear only in valid states. Below 650px, the history rail becomes a run selector so output and approval actions keep their full width. Alerts entries deep-link to the corresponding run.
- **Recurrence:** An inline editor controls schedule enablement, hourly/daily/weekday/Monday/custom cron, time, local/UTC timezone, overlap, and missed-run policy. Save updates the YAML trigger and persistent next-run state. The interface never implies concurrent instances of the same workflow by default and offers one queued run only when explicitly selected.
- **Execution and recovery:** Runs use real harness processes, bounded outputs, immutable snapshots, serialized Build steps, read-only concurrency, approval persistence, retry budgets, and checkpoint provenance. Shutdown marks interrupted steps stopped and offers Resume. Successful workflows create one human-reviewed memory proposal. Motion is limited to active execution and stops for reduced motion or hidden views.

### Tesseract Agent Canvas
- **Composition:** Tesseract keeps one full-size native canvas with a quiet dotted field, no tool rail, no layer list, no inspector, and no empty cards. An unboxed zoom/Fit control sits at the top right; one 860px-maximum, 160px-high composer floats 22px above the bottom center. Like Agent, it uses an 88px transparent writing area above a 34px action row with only the setup icon on the left and the circular Send icon on the right, all inside one outer border. Recall stays exclusive to Agent and its shortcut never leaves Tesseract. The empty state explains the single prompt → watch → select → refine loop in three short lines. A submitted Tesseract run adds a 60px live-activity region inside that same composer rather than opening a transcript or another panel: state and elapsed time share the first line, the real current/latest action follows, and the submitted prompt remains visible in one bounded line with its full text in a tooltip. Ready documents report their live frame count; error and stopped states remain visible instead of flashing away.
- **Empty state:** A central native four-dimensional hypercube gives Tesseract its own identity before the first run, distinct from Memory's black hole. Sixteen vertices and 32 edges rotate through the xw, yw, zw, and xy planes before 4D→3D→2D perspective projection; depth controls edge weight, color, and opacity above deterministic stars. The title and two concise instructions sit beneath it. The scene uses no raster assets or external dependencies, advances at most 30 FPS only while empty and visible, and becomes a static complete composition under reduced motion.
- **Generated content:** The agent writes `.stellar/tesseract.json`; Raven draws frame, text, and rect nodes directly with native rectangles, outlines, and Roboto text. Document fills, strokes, variables, and typography stay independent of the application theme, while the field, captions, hover, selection, and error states derive from the active planet palette. Horizontal/vertical layouts, fit/fill sizing, padding, gaps, and alignment carry the UI without a browser or embedded web runtime.
- **Live agent loop:** Build prompts include the complete JSON contract and a small typed example. The agent may read the current document first, then must write a complete first version before unrelated workspace, wiki, or source exploration so visible progress is not held hostage to reconnaissance. The tab polls only while visible, applies valid changes every 350 ms, and preserves the last valid scene with a readable error when the document is incomplete or invalid. Unambiguous omitted types are inferred for agent tolerance; unknown types still fail. A queued refinement shows its pending text and Cancel inside Tesseract, and palette/Run setup overlays restore focus to the composer that opened them. Late harness details update both the Work row and the current action, including Tesseract's inline activity. No MCP, manual drawing, browser node, slide system, or code-generation side effect is introduced.
- **Selection and direct editing:** Clicking a root frame adds a thin Accent outline and one compact selection row with the element name and Clear; the full ID and manipulation hints remain in its tooltip. Ctrl+click selects the deepest node, Enter selects a container's first child, Shift+Enter selects its parent, and double-click or Enter on text opens a native inline editor where Enter saves and Escape cancels. Dragging moves any node with pixel snapping; the bottom-right handle resizes it. Nested dragged nodes gain absolute x/y coordinates within their parent while remaining siblings stay in automatic layout. Every commit writes the same JSON document and preserves unrelated properties. Selection context is captured when a prompt is sent, so queues and retries cannot retarget another element after the user changes selection. Escape or Clear removes the target. There is no properties inspector, layer panel, or drawing toolbar; selection adds 26px and queue adds 38px only while present.
- **Navigation:** Drag on empty canvas pans, a trackpad swipe pans and pinch zooms, the wheel zooms at the pointer, arrows pan when nothing is selected, plus/minus zoom, and 0 or Fit frames all roots using the settled viewport.
- **Selecting:** As in design tools, a click selects the root frame's direct child under the pointer, a click on another element at that level moves the selection there, a double-click goes one level in (and edits text), empty space in a root frame or its name selects the frame, and Ctrl/Cmd+click picks the deepest element. Hover previews what a click would select.
- **Direct manipulation:** The selection is a 1.5px Accent hairline on the element's exact bounds with 8px white square handles on its corners (on its left and right sides for text) and its size (W × H) in an Accent pill underneath. Any side or corner resizes within 6px of it, with the matching resize cursor: the opposite side stays put, Shift keeps the proportions on a corner, Alt resizes around the center, and text only resizes sideways, switching to fixed-width wrapping. Root frames snap their edges and centers to the other frames within 6px, with blue guides; Ctrl/Cmd while dragging turns snapping off. A child of an automatic layout is reordered rather than placed: a ghost follows the pointer and an Accent line shows where it lands, and the arrows along the layout's axis move it one place. Free elements nudge 1px with the arrows (10px with Shift). Ctrl/Cmd+D duplicates with fresh ids, Delete removes, Escape cancels a drag in progress, and every edit is one Ctrl/Cmd+Z step.

### Editors, Wiki, Knowledge, and Review
- **View headers:** Every tool header (Editor, Workflows, Definitions, Review, Worktrees) is flat: no fill, border, or divider. The title (17px) and its monospace path or subtitle sit on the workspace surface, left-aligned with the controls below, with the view's actions on the right; spacing alone separates them from the content.
- **Wiki:** The right column combines note title/path, Save note, Edit/Preview tabs, wrapped Markdown, and an outgoing/backlinks footer. The structural Markdown editor is borderless and square, with no floating-card frame or outer inset. Non-caret lines natively present headings, bold, italic, inline code, links, and list markers without changing the `.md` source; clicking a formatted line places the caret at its end and reveals raw Markdown there, while any selection temporarily restores raw text for accurate geometry. `[[wiki links]]` use the active Link color and support percent-decoded, case-insensitive title matching; external links open externally.
- **Knowledge:** A search field filters serialized entity/relation records, including accepted episode memories. The results list and detail pane expose type, source, confidence, raw JSON, and symbolic context; entity, relation, episode, and staged counts remain textual. Graph writes still require human review. Forget selected record is disabled until a record is open and requires confirmation before deleting it and its touching relations.
- **Proposal notice:** A warm dark rounded notice appears in Activity only when a proposal exists, with an accent title and primary Review changes action. A separate neutral Activity notice exposes persisted code changes without interrupting the transcript.
- **Code Review:** The center Review tab contains Code changes and Knowledge sub-tabs. Code changes keeps task history and changed-file selectors in compact toolbar rows, with Open file and Restore task actions beside them. A remaining-hunk selector restores individual text changes with confirmation, validates each affected file against its post-run state, and persists applied hunk IDs so later restores complete only the remaining review. Binary changes never appear as editable text. Nested Diff and Verification tabs separate source changes from recognized build/test checks. Verification presents a compact result list, bounded command output, a diagnostic selector, and Open diagnostic navigation to the reported source line. Rerun executes the recorded command asynchronously and updates that check in place. Watch mode uses a one-second autosave debounce with no overlapping runs and can be enabled by button or `--watch`; results and failures persist and notify. With no checkpoint, only first-run guidance remains; history, file, hunk, and content controls collapse. Verification-only checkpoints remain available without fabricated diffs, while missing checks and diagnostics collapse their selectors/actions rather than displaying inactive chrome. Checkpoints persist added/modified/deleted content and check results under `.stellar/checkpoints/`; partial snapshots are labeled, and Restore task requires confirmation plus an exact match to every file's post-run state before writing. `Ctrl+Shift+D` opens this view.
- **Knowledge Review:** The Knowledge sub-tab shows summary, affected files/operations, and a read-only line-numbered diff. Staged episodes join the same human-reviewed graph proposal as other knowledge changes. Accept validates current files before writing; Discard clears without writing. A pending proposal must be reviewed or discarded before another run starts.

### Episodic Agent Memory
- **Staging and privacy:** Successful lead runs stage a typed `episode` entity with `memory=episodic`, task, session ID, harness, status, run provenance, and a transcript bounded to 16,000 characters. Memory stays local and inspectable in the workspace's review queue until accepted; staging alone never writes `wiki/graph.jsonl`.
- **Review and identity:** Accepted episodes upsert as `run:<session>`, so follow-ups replace that session's memory rather than duplicate it. The full bounded record persists in `wiki/graph.jsonl`; discarded or unaccepted episodes do not enter it.
- **Recall:** `knowledge_context` combines lexical ranking with optional deterministic local vector recall. Hashed token and character-trigram features use cosine similarity offline; this supports morphological paraphrases without claiming neural embeddings or model-grade semantics. The Settings switch disables vectors for lexical-only matching. Accepted episodes can inform later lead prompts, with transcript output capped around 3,000 characters while the full bounded record remains in storage. Automatic consolidation is not performed.
- **Inspection and CLI:** Knowledge stats includes an episode count alongside entity, relation, and staged counts. `raven-ide-knowledge episodes` lists accepted memories as JSONL, `stats` reports `episodes`, and `staged` remains the separate view of writes awaiting human review. `forget ID` immediately removes an accepted record and its touching relations; new records still enter only through acceptance.

### Memory Universe
- **Structure:** Knowledge → Universe or `Ctrl+M` opens Memory; `Ctrl+K` opens the Knowledge inspector. A 22px title, explanatory copy, episode/entity/relation counts, Fit universe, and Open Knowledge sit above the native canvas. Selection reveals a rounded details surface with a 220px scrolling region and an enabled Forget memory action; confirmation deletes the accepted record and touching relations, writes the graph atomically, and refreshes the universe.
- **Reviewed data only:** Accepted episodes are inner planets and other accepted entities form outer systems. Only real graph relations become links; staged or unreviewed operations never appear as accepted memories. The empty state says `No reviewed memories yet` and directs the user to complete a run and accept its episode rather than fabricating content.
- **Rendering:** A theme-aware WORKSPACE black hole and event horizon anchor deterministic stars and orbital systems. Native Raven drawing resolves Window from the root content rather than the transparent Glass workspace override, uses Text Placeholder for secondary reading instead of Disabled, and derives accents, links, success, and borders from the active theme. Full-opacity labels and strengthened orbital marks preserve legibility across Dark, Light, and Glass; theme switches invalidate and restyle the canvas. No external assets, new dependencies, neural embeddings, or vector-search claims are involved.
- **Motion:** While Memory is visible, `MemoryUniverse::advance(delta, reduce_motion)` caps native animation at 30 FPS. Episodes and entities use slow differential orbital speeds, a subtle radial gravitational tug, rotating accretion particles, and a lensing pulse. The shared scheduler wakes the main loop only while the opening splash, Memory, or the world gallery is active and requests paint with `Rwindow_request_redraw`, avoiding per-frame layout invalidation. Resume frames start with zero elapsed delta to prevent jumps. Reduced motion returns without advancing or redrawing, hidden views stop, and graph data remains unchanged.
- **Navigation:** Fit universe or 0 frames the canvas. Drag pans, the wheel zooms at the pointer, arrow keys pan, and plus/minus—including numeric keypad keys—zooms. Hover and selection use Accent-colored outlines and readable labels; selecting a planet updates the details panel.
- **Details:** The selected record exposes its type, ID, source, confidence, non-transcript metadata, incoming/outgoing relations, and remembered transcript. Transcript display is capped at 5,000 characters in this panel, separate from the roughly 3,000-character prompt-recall cap and the full 16,000-character stored bound.

### Accessibility Guidance
- Preserve visible green keyboard focus in Earth Dark and every other planet variant's own high-visibility focus treatment.
- Keep labels, badges, summaries, and status messages synchronized with colored or animated state. Activity and graph already duplicate execution state in words.
- Maintain soft-white body text and established muted-text contrast on black. Use near-black AccentText on Earth Dark primary green buttons; do not use disabled or placeholder colors for essential reading. Every planet variant keeps body, muted, and placeholder text at or above 4.5:1 on Window, Panel, Input, Header, and section-header surfaces.
- Interface zoom starts at 115%, changes in 10% steps within 50–200%, and resets to 115%. Above 150%, compact header behavior and automatic sidebar folding keep every navigation destination reachable; the world gallery narrows and scrolls. Editor zoom starts at 100%, changes in 10% steps within 50–250%, and resets to 100%. `Ctrl+Shift` plus/minus controls interface scale; `Ctrl` plus/minus controls editors, including numeric keypads. Settings also provides labeled buttons.
- `Ctrl+S` immediately saves the active source/agent/note, which otherwise autosaves; `Ctrl+Alt+A` creates an agent and `Ctrl+Shift+N` creates a note; `Ctrl+Shift+R` opens a knowledge proposal; `Ctrl+Shift+D` opens Code Review; `Ctrl+Shift+Enter` accepts a knowledge proposal; `Ctrl+P` opens Wiki Preview; `Ctrl+Shift+P/O/F` open commands, files, and global content search; `Ctrl+R` opens Recall; `Ctrl+Shift+E` opens Run setup; `Ctrl+K` opens Knowledge; `Ctrl+M` opens Memory Universe; `Ctrl+Shift+T` opens the world gallery. On the focused canvas, arrow keys pan, plus/minus zooms, and 0 fits. Escape closes overlays and returns focus. Keep visible labels, accessible canvas instructions, and textual status alongside shortcuts.
- Read the operating system's reduced-motion preference at startup and apply it through `Raccessibility_set_reduce_motion`. Raven then makes springs, slides, fades, and breathing immediate while preserving textual state. The same preference stops Memory Universe animation; hidden Memory tabs do not continuously redraw.

## Do's and Don'ts

### Do:
- **Do** keep Earth Dark forest-black with sparse ocean-and-forest signals and near-black text on primary green buttons.
- **Do** preserve one plain-language navigation, the task-only center, the contextual right inspector, and the full graph in Endurance → Live.
- **Do** keep transcript and composer within the 860px reading column, with compact right user bubbles and plain lead messages.
- **Do** show navigation search only for chats, agent definitions, and notes, leaving the Files tree and task-only canvases unfiltered.
- **Do** pair every animated or colored execution state with readable text and honor system reduced motion.
- **Do** preserve five Interstellar appearance families with fifteen variants, Dark/Light/Glass gallery choices, full labels through 200% zoom, and independent interface/editor zoom.

### Don't:
- **Don't** reintroduce non-Interstellar themes or impose one planet's palette on another.
- **Don't** wrap lead messages in full-width cards or add decorative shadows/glow to the composer, transcript, or ordinary panels.
- **Don't** restore global center/right tab bars, open the inspector without relevant content, or duplicate the live map inside it.
- **Don't** introduce web breakpoints, mobile navigation, or browser-style controls into this native desktop surface.
- **Don't** make the live map a freeform editing canvas or rely on its halo/color alone to communicate state.
- **Don't** hide proposal review in a modal, write proposed changes before acceptance, or use placeholder/disabled colors for essential reading.
