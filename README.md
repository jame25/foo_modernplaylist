# foo_modernplaylist

A sleek, high-performance playlist manager component for **foobar2000**, featuring integrated playlist tabs, real-time live search, customizable title-formatting columns, and seamless support for both **Default User Interface (DUI)** and **Columns UI (CUI)**.

---

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
  - [Integrated Playlist Tabs](#integrated-playlist-tabs)
  - [Real-Time Live Search & Filtering](#real-time-live-search--filtering)
  - [High-Performance Virtual Track List](#high-performance-virtual-track-list)
  - [Customizable Columns & Proportional Sizing](#customizable-columns--proportional-sizing)
  - [Dynamic Theming & High-DPI Support](#dynamic-theming--high-dpi-support)
- [Compatibility & Requirements](#compatibility--requirements)
- [Installation](#installation)
  - [Standard Installation (.fb2k-component)](#standard-installation-fb2k-component)
  - [Manual Installation](#manual-installation)
- [User Interface Setup Guide](#user-interface-setup-guide)
  - [Setting Up in Default UI (DUI)](#setting-up-in-default-ui-dui)
  - [Replacing Default UI Tabs (Clean Look)](#replacing-default-ui-tabs-clean-look)
  - [Setting Up in Columns UI (CUI)](#setting-up-in-columns-ui-cui)
- [User Guide & Functionality](#user-guide--functionality)
  - [Managing Playlists & Tabs](#managing-playlists--tabs)
  - [Using Search & Search Syntax](#using-search--search-syntax)
  - [Customizing & Reordering Columns](#customizing--reordering-columns)
  - [Track Operations & Playback](#track-operations--playback)
  - [Column Sizing Modes: Fit to Window](#column-sizing-modes-fit-to-window)
- [Keyboard Shortcuts Reference](#keyboard-shortcuts-reference)
- [Building from Source](#building-from-source)
- [Frequently Asked Questions (FAQ)](#frequently-asked-questions-faq)
- [License & Credits](#license--credits)

---

## Overview

**Modern Playlist** brings a clean, contemporary playlist experience to foobar2000. It provides fluid interaction, instant search filtering, and complete aesthetic control without heavy external UI dependencies or third-party frameworks.

All core playback, media metadata, playlist locking, playback queues, and undo history remain strictly owned by foobar2000—ensuring rock-solid stability and zero audio compromises.

---

## Key Features

### Integrated Playlist Tabs
- **Embedded Styled Tabs**: Clean, modern tab strip rendered directly above the search bar.
- **One-Click Playlist Creation**: Hit the **`+`** button (or press `Ctrl+N` / `Ctrl+T`) to instantly create and switch to a new playlist without disruptive name prompt dialogs.
- **Smart Adaptive `+` Placement**: The `+` button neatly docks beside the rightmost tab; when tabs fill the available window width, the tab strip enables scrolling while the `+` button stays pinned and accessible at the far right.
- **Fluid Tab Scrolling**: Overflow navigation arrows and **mouse wheel scrolling** support over the tab strip.
- **Drag-and-Drop Reordering**: Drag tabs left or right to reorder them on the fly.
- **Active Playback Indicator**: The playlist currently playing music automatically displays an animated speaker wave icon in its tab in place of the close button.
- **Convenient Tab Controls**: Quick **`×`** button to close playlists (with confirmation) and right-click context menu to rename, remove, or shift tabs.
- **Optional Display**: The tab strip can be toggled on or off (`Show playlist tabs`) per panel instance, ensuring you never have duplicate tabs if your host layout already provides them.

### Real-Time Live Search & Filtering
- **Instant Search Debounce**: Fast 180ms debounced search that filters tracks as you type.
- **Full foobar2000 Query Syntax**: Supports field queries (`artist HAS Beatles`, `album IS "Abbey Road"`), numerical comparisons (`%rating% GREATER 3`, `%bitrate% GREATER 320`), and Boolean operators (`AND`, `OR`, `NOT`).
- **Inline Syntax Feedback**: Non-intrusive notification displayed directly below the search bar when an incomplete or invalid query is entered; vanishes immediately once corrected.
- **Per-Playlist Search Memory**: Each playlist remembers its own active search query even when switching between tabs.
- **Quick Shortcuts**: Press `Ctrl+F` while the Modern Playlist panel has focus to jump to and highlight search; press `Escape` to immediately clear search and restore focus to the track list.

### High-Performance Virtual Track List
- **Handles Massive Libraries**: Virtual list-view architecture handles tens of thousands of tracks with zero stutter or memory bloat.
- **Independent Index Mapping**: Tracks are indexed by playlist slot, not file path, keeping duplicate occurrences of tracks properly distinct and independently selectable.
- **Non-Destructive Filtered Operations**: When filtering with search, actions like deleting, moving, copying, or enqueuing apply **only** to the visible filtered rows. Hidden tracks and their original order remain completely safe and untouched.
- **Full Undo / Redo**: Seamless integration with foobar2000's native undo system (`Ctrl+Z` / `Ctrl+Y`).
- **Track Reordering**: Drag-and-drop tracks to insert before any target row or at the bottom. Use `Alt+Up` and `Alt+Down` to nudge selected tracks up or down.
- **Native Clipboard & Context Menu**: Full support for `Ctrl+C` (copy), `Ctrl+X` (cut), and `Ctrl+V` (paste foobar2000 tracks), plus the complete foobar2000 context menu (Properties, ReplayGain, Convert, Quick Tagger, etc.).

### Customizable Columns & Proportional Sizing
- **Default Columns Out of the Box**: `#` (Track Number), `Title`, `Artist`, `Album`, `Length`, and `Format` (Codec).
- **Custom Title-Formatting Expressions**: Add or modify any column using standard foobar2000 title formatting syntax (e.g., `%bitrate% kbps`, `[%replaygain_track_gain%]`, `$if2(%album artist%,%artist%)`).
- **Flexible Alignment**: Left, Center, or Right alignment per column.
- **Header Drag-and-Drop**: Drag column headers horizontally to rearrange column order.
- **Fit to Window (Proportional Sizing)**: Enabled by default. Proportional auto-sizing adjusts visible columns to span 100% of the viewport without unwanted horizontal scrollbars. Dragging a column border resizes it and automatically recalculates relative proportions.
- **Standard Width Mode**: Toggle off *Fit to Window* to use fixed column widths with the last column expanding to fill empty space, or horizontal scrolling if columns exceed the window width.
- **Per-Playlist Column Configurations**: Columns, widths, and visibility can be saved independently for each playlist.

### Dynamic Theming & High-DPI Support
- **Default UI Color & Font Integration**: Dynamically inherits playlist fonts (`ui_font_playlists`), tab fonts (`ui_font_tabs`), and standard fonts (`ui_font_default`).
- **Automatic Dark Mode**: Seamlessly follows foobar2000 Preferences → Default User Interface → Colors and Fonts (supports **Dark**, **Light**, and dynamic **Use system setting**).
- **Columns UI Palette**: Renders a clean charcoal/dark-slate or crisp light theme with refined Segoe UI typography, smooth hover transitions, and cyan selection highlights.
- **High-DPI Awareness**: All row heights, header heights, search boxes, tab buttons, padding, and icons scale sharply across high-resolution displays (100%, 125%, 150%, 175%, 200%+).

---

## Compatibility & Requirements

- **Operating System**: Windows 10 or Windows 11 (64-bit or 32-bit).
- **foobar2000 Version**: foobar2000 **v2.0** or newer (v2.1+ recommended; 64-bit and 32-bit versions supported).
- **User Interfaces**:
  - **Default User Interface (DUI)**: Built-in, fully supported.
  - **Columns UI (CUI)**: Optional; fully supported when Columns UI is installed.

---

## Installation

### Standard Installation (`.fb2k-component`)

1. Download the latest `foo_modernplaylist.fb2k-component`.
2. Launch foobar2000.
3. Open the preferences dialog: **File → Preferences** (or press `Ctrl+P`).
4. In the left panel, select **Components**.
5. Click the **Install...** button at the bottom of the page.
6. Browse to and select `foo_modernplaylist.fb2k-component`, then click **Open**.
7. Click **Apply** (foobar2000 will prompt to restart).
8. Once restarted, verify that **Modern Playlist** is listed under **Preferences → Components**.

### Manual Installation

For portable installations:
1. Extract or place `foo_modernplaylist.dll` into the `components/foo_modernplaylist` directory inside your foobar2000 installation folder (or `%APPDATA%\foobar2000-v2\user-components\foo_modernplaylist`).
2. Restart foobar2000.

---

## User Interface Setup Guide

### Setting Up in Default UI (DUI)

1. Open foobar2000.
2. From the main menu, select **View → Layout → Enable layout editing mode**.
3. Right-click an existing panel area (such as the standard playlist or an empty splitter) and choose **Add New UI Element...** (or **Replace UI Element...**).
4. In the selection dialog, expand **Playlist renderers** and select **Modern Playlist**.
5. Click **OK**.
6. When finished organizing your layout, go to **View → Layout → Enable layout editing mode** to turn off editing mode.

### Replacing Default UI Tabs (Clean Look)

If you want to use Modern Playlist's built-in styled tabs without having duplicate playlist tabs from foobar2000:

1. Enable **View → Layout → Enable layout editing mode**.
2. Right-click the **Modern Playlist** element and select **Copy UI Element**.
3. Right-click the outer **Playlist Tabs** container above it and select **Paste UI Element**.
   *(This cleanly replaces the host's tab container with Modern Playlist while keeping your configuration intact).*
4. Turn off layout editing mode (**View → Layout → Enable layout editing mode**).
5. Right-click the column header or the track list in Modern Playlist and check **Show playlist tabs**.

> [!TIP]
> If *Copy/Paste UI Element* is not visible, right-click the outer native tabs container in layout editing mode, choose **Replace UI Element... → Modern Playlist**, and enable **Show playlist tabs**.

### Setting Up in Columns UI (CUI)

1. Open **File → Preferences** (`Ctrl+P`) and navigate to **Display → Columns UI**.
2. Click the **Layout** tab.
3. Select the splitter where you want the playlist to appear.
4. Click **Insert Panel** (or right-click → **Insert panel**).
5. Choose **Playlist views → Modern Playlist**.
6. Click **Apply** or **OK**.
7. If your layout already contains a *Playlist tabs* toolbar or container and you wish to use Modern Playlist's integrated tabs instead, right-click the host *Playlist tabs* container in the Layout tree and choose **Change container type → Vertical splitter** (or remove the separate tabs item). Then right-click Modern Playlist's header and enable **Show playlist tabs**.

---

## User Guide & Functionality

### Managing Playlists & Tabs

| Action | How to Do It |
| :--- | :--- |
| **Toggle Tabs** | Right-click any column header or track area → click **Show playlist tabs**. |
| **New Playlist** | Click the **`+`** button next to the tabs, or press `Ctrl+N` / `Ctrl+T`. |
| **Switch Playlist** | Click any tab in the tab strip. |
| **Reorder Tabs** | Click and drag any tab horizontally to its new position, or right-click the tab and choose **Move left** / **Move right**. |
| **Scroll Tabs** | Use the left/right arrow buttons when tabs overflow, or scroll your **mouse wheel** over the tab strip. |
| **Rename Playlist** | Right-click the tab and select **Rename...**. |
| **Close Playlist** | Click the **`×`** button on the tab (unless it is currently playing), or right-click the tab and select **Remove playlist**. |
| **Playing Playlist** | The tab currently playing music automatically displays a **speaker wave icon** instead of the close button. |

### Using Search & Search Syntax

The rounded search bar filters tracks in real time.

- **Focus Search**: Press `Ctrl+F` to jump straight to the search box with all text selected.
- **Clear Search**: Press `Escape` while search is focused to clear the filter and return focus to the tracks.
- **Search Scope**: Search checks metadata across titles, artists, albums, and file paths.
- **Advanced Query Examples**:
  - `queen` — Finds tracks containing "queen" in common fields.
  - `artist HAS radiohead` — Tracks where the artist contains "radiohead".
  - `genre IS rock AND date AFTER 2000` — Rock tracks released after the year 2000.
  - `album HAS "greatest hits" OR %codec% IS FLAC` — Multi-condition query.
  - `%rating% GREATER 3` — Tracks with a rating of 4 or 5.
  - `NOT %genre% IS classical` — Exclude classical music.
- **Syntax Alerts**: If an invalid query or unmatched quote is entered, a helpful notice appears directly under the search box explaining the issue.

### Customizing & Reordering Columns

Right-click any column header to open the column configuration menu:

- **Show / Hide Columns**: Click any column name in the menu to toggle its checkmark. (At least one column always remains visible).
- **Add Column**: Click **Add column...** to specify a Title, a foobar2000 Title Formatting pattern, and text Alignment (Left, Right, Center).
- **Edit Column**: Click **Edit column...** on an existing column to change its expression, title, or alignment.
- **Delete Column**: Click **Delete column** to remove the selected custom column.
- **Reset Columns**: Click **Reset columns** to instantly restore standard default columns (`#`, `Title`, `Artist`, `Album`, `Length`, `Format`).
- **Reorder Columns**: Click and hold any header, then drag it left or right to rearrange the visual order.

#### Popular Title Formatting Expressions

| Column Title | Pattern Expression | Alignment |
| :--- | :--- | :--- |
| **Bitrate** | `%bitrate% kbps` | Right |
| **Year / Date** | `[%date%]` | Center |
| **Track / Total** | `[%tracknumber%/[%totaltracks%]]` | Right |
| **Genre** | `[%genre%]` | Left |
| **File Type** | `[%codec%[ %codec_profile%]]` | Left |
| **ReplayGain** | `[%replaygain_track_gain%]` | Right |

### Track Operations & Playback

- **Play Track**: Double-click any row or press `Enter` to play the focused track.
- **Select All**: Press `Ctrl+A` to select all visible tracks.
- **Remove Tracks**: Press `Delete` to remove selected tracks from the playlist (this does **not** delete files from your disk).
- **Undo / Redo**: Press `Ctrl+Z` to undo playlist alterations; press `Ctrl+Y` to redo.
- **Reorder by Dragging**: Click and drag selected tracks up or down. A blue insertion marker highlights the destination row. Dropping on empty space moves them to the bottom.
- **Nudge Selection**: Press `Alt+Up` or `Alt+Down` to shift selected tracks up or down by one row.
- **Clipboard Operations**:
  - `Ctrl+C`: Copy selected foobar2000 tracks to clipboard.
  - `Ctrl+X`: Cut selected tracks.
  - `Ctrl+V`: Pastes tracks copied from foobar2000.
- **Queue Tracks**: Right-click track(s) → **Add to playback queue**.
- **Track Context Menu**: Right-click selected tracks to access file management, tag editing, ReplayGain scanning, audio converter presets, and file properties.

### Column Sizing Modes: Fit to Window

Right-click any column header or empty track area to toggle **Fit to Window**:

- **Checked (Enabled - Default)**:
  All visible columns dynamically scale to fill the entire horizontal width of the window. No horizontal scrollbar will appear. When you resize a column, its width relative to the others adjusts proportionally.
- **Unchecked (Standard Mode)**:
  Columns use their configured pixel widths. The last column stretches to fill remaining space. If the total width exceeds the window, standard horizontal scrolling is activated.

---

## Keyboard Shortcuts Reference

| Shortcut | Context | Description |
| :--- | :--- | :--- |
| **`Ctrl+F`** | Global / List | Focuses search bar and selects existing query text. |
| **`Escape`** | Search Bar | Clears search filter and returns focus to the track list. |
| **`Enter`** | Track List | Plays the focused / selected track. |
| **`Delete`** | Track List | Removes selected visible tracks from the playlist. |
| **`Ctrl+A`** | Track List | Selects all visible tracks in the active view. |
| **`Ctrl+N`** / **`Ctrl+T`** | Global / List | Creates and activates a new playlist immediately. |
| **`Alt+Up`** | Track List | Nudges selected tracks up one row. |
| **`Alt+Down`** | Track List | Nudges selected tracks down one row. |
| **`Ctrl+C`** | Track List | Copies selected tracks to the clipboard. |
| **`Ctrl+X`** | Track List | Cuts selected tracks to the clipboard. |
| **`Ctrl+V`** | Track List | Pastes copied foobar2000 tracks into the playlist. |
| **`Ctrl+Z`** | Track List | Restores previous playlist state (Undo). |
| **`Ctrl+Y`** | Track List | Reapplies undone playlist modification (Redo). |

---

## Building from Source

### Prerequisites

- Windows 10 or 11
- Visual Studio 2022 (or Build Tools for Visual Studio 2022) with:
  - C++ Desktop Development Workload
  - MSVC v145 or v143 toolset
  - Windows 10 / 11 SDK

### MSBuild Build

```powershell
msbuild foo_modernplaylist.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal
```

The compiled DLL will be located at `build/x64/Release/foo_modernplaylist.dll`.

### VS Code Integration

Open the workspace in VS Code and press **`Ctrl+Shift+B`**. This executes the preconfigured MSBuild build task.

---

## Frequently Asked Questions (FAQ)

#### Q: Why do I see two rows of playlist tabs?
**A:** If your foobar2000 layout already includes a native tab container (in Default UI or Columns UI), you may see both host tabs and Modern Playlist tabs. You can either:
1. Right-click Modern Playlist's header and uncheck **Show playlist tabs** to use your host's tabs.
2. Or remove the host's native tab container in layout editing mode and keep Modern Playlist's integrated tabs enabled for a unified look.

#### Q: If I filter with search and press Delete, does it delete my whole playlist?
**A:** No. Deletions, copies, cuts, moves, and queue actions apply **strictly to the visible, filtered tracks**. All hidden tracks remain safely in their exact playlist locations.

#### Q: Does pressing Delete remove the actual audio files from my hard drive?
**A:** No. Pressing `Delete` removes the tracks from the active foobar2000 playlist only. To delete files from your drive, right-click the tracks and choose **File Operations → Delete file(s)**.

#### Q: Are my custom column setups saved if I restart foobar2000?
**A:** Yes. foobar2000 saves panel configurations in your profile. In addition, each playlist independently retains its own column definitions, widths, and visibility.

#### Q: Can I use Modern Playlist in both Default UI and Columns UI?
**A:** Yes. The same DLL provides native support for both UI hosts. You do not need different builds.

---

## License & Credits

- This component is provided as-is for educational and personal use.
- Built using the official **foobar2000 SDK**, **PFC**, and the **Columns UI SDK**.
