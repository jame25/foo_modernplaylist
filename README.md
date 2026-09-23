# foo_modernplaylist

**Modern Playlist** is a native Windows playlist component for foobar2000, with
configurable title-format columns, album grouping and artwork, search, and an
optional horizontal playlist manager. The same DLL provides a **Default User
Interface (DUI)** element and a **Columns UI (CUI)** panel.

## Contents

- [Requirements and installation](#requirements-and-installation)
- [Adding the panel](#adding-the-panel)
- [Playlist manager](#playlist-manager)
- [Columns and appearance](#columns-and-appearance)
- [Groups and artwork](#groups-and-artwork)
- [Search](#search)
- [Track operations and shortcuts](#track-operations-and-shortcuts)
- [Saved settings](#saved-settings)
- [Building and validation](#building-and-validation)
- [Credits](#credits)

## Requirements and installation

- **foobar2000 2.0 or newer for Windows.** The component uses playlist GUIDs for
  persistent playlist identity.
- **x64 or Win32**, matching your foobar2000 installation. ARM builds are not
  supported by the current build configuration.
- Default UI, or Columns UI if you want to use the CUI panel. Columns UI is not
  required when using Default UI.

Install a matching `foo_modernplaylist.fb2k-component` package through
**File → Preferences → Components → Install…**, apply the change, and restart
foobar2000. Confirm that **Modern Playlist** appears in the component list.

## Adding the panel

### Default UI

1. Enable **View → Layout → Enable layout editing mode**.
2. Add or replace a UI element with **Playlist renderers → Modern Playlist**.
3. Leave layout editing mode when finished.

The panel follows Default UI's playlist, tab, and standard fonts and its colors,
including light/dark changes.

### Columns UI

In **Preferences → Display → Columns UI → Layout**, insert
**Playlist views → Modern Playlist** into a splitter and apply the layout.
The CUI panel uses the component's light/dark palette and Segoe UI typography,
rather than Columns UI's common font and color settings.

### Enabling the built-in tabs

The component's playlist tabs are **off by default**. Right-click a column
header and enable **Header Bar → Show playlist tabs**. Enable **Header Bar →
Playlist manager below playlist** to move the strip to the bottom; the search
row stays above the tracks.

If you already have host-provided tabs, either keep using them or remove their
container to use the component's manager:

- **Default UI:** in layout editing mode, copy the Modern Playlist element and
  paste it over the outer Playlist Tabs element. Replacing that container with
  a new Modern Playlist element is another option, but starts with fresh panel
  settings.
- **Columns UI:** change the Playlist tabs container to a **Vertical splitter**
  in Layout preferences, retaining its child panel.

Panel settings are in the header and track context menus. **Preferences → Tools
→ Modern Playlist** currently contains an empty **General** tab.

## Playlist manager

- Click a tab to activate its playlist. The playing playlist shows a speaker
  indicator in place of the close button.
- Click **+** or press **Ctrl+N** to create and activate an automatically named
  playlist without a name dialog.
- Scroll overflowing tabs with the mouse wheel/trackpad or arrow buttons.
  The **+** button remains accessible.
- Drag tabs to reorder them, with an insertion marker and edge scrolling.
  Escape cancels the drag.
- Use the close button or **Remove** to remove ordinary playlists through
  foobar2000's removal/confirmation flow.

Right-click a tab, the **+** button, or a scroll button for the manager menu:

| Command | Behavior |
| --- | --- |
| **Insert… / Add…** | Create a normal playlist or autoplaylist before the clicked tab or at the end. |
| **Load a Playlist…** | Open the host's playlist-loading dialog. |
| **Save this Playlist…** | Activate and save the clicked playlist using the host dialog. |
| **Duplicate** | Copy all items, including repeated tracks, into a normal playlist. An autoplaylist becomes a snapshot. |
| **Rename… / Remove** | Rename or remove the clicked playlist, subject to locks and special-playlist restrictions. |
| **Move left / Move right** | Move the clicked playlist; the pinned Media Library playlist remains first. |
| **Autoplaylist properties…** | Open the host's properties UI when supported. |
| **Add files… / Add folder…** | Activate the clicked playlist and open the host's add dialog. |
| **Sort playlists by name A–Z / Z–A** | Sort names case-insensitively, retaining the order of equal names and keeping Media Library first. |

### Autoplaylists

**New Autoplaylist…** accepts a name, foobar2000 search query, sort title format,
and **Keep sorted** option. Keeping it sorted prevents manual track reordering.
foobar2000 owns and persists the autoplaylist definition.

**Pre-defined Autoplaylist** offers never played, played in the last five days,
unrated, rated 3–5, rated 4, rated 5, and loved tracks. These queries use
`%play_count%`, `%last_played%`, `%rating%`, and `%mood%`. Results depend on the
metadata/statistics available in your library; Modern Playlist does not collect
play counts or write ratings.

### Special playlists

The manager's **Special playlists** submenu enables three optional features.
All start disabled and are shared globally across panel instances.

| Playlist | Behavior |
| --- | --- |
| **Media Library** | An `ALL` autoplaylist, kept sorted and pinned first. |
| **Historic** | Appends each newly started track, including repeated plays. Logging runs once globally, even with no panel visible. |
| **Queue Content** | A read-only mirror of playback queue order, including duplicate entries. Its lock blocks content edits and renaming; synchronization does not modify the actual queue. |

These playlists are tracked by GUID. Enabling a feature creates its own playlist
rather than adopting an existing playlist with the same name. Disable a feature
through its toggle to remove its playlist; cancelling host removal keeps the
feature enabled. Removing its playlist elsewhere disables the feature.

## Columns and appearance

The default visible columns are **State, #, Title, Artist, Album, Time**.
The full built-in catalog also includes **Cover, Index, Year, Genre, Mood,
Rating, Plays, Bitrate**.

Right-click a header for **Columns** controls to show/hide, add, edit, delete,
or reset columns. At least one column must remain visible. The editor supports:

- Primary and extra-line title-format expressions.
- A separate sort expression and semantic reference, such as Text, State,
  Cover, or Index.
- Left, right, or center alignment and a proportional width weight.

Drag headers to reorder columns and their edges to resize them. Click a header
to sort ascending/descending using its sort expression, falling back to the
primary title format. Sorting changes the playlist order and respects playlist
locks. With a search filter, hidden tracks keep their slots during sorting.

**Header Bar → Fit to Window** is enabled by default. It distributes widths using
saved proportions while enforcing minimum widths, so a narrow panel can still
scroll horizontally. With fitting disabled, columns use preferred widths and
the last column fills spare space. Headers are centered by default;
**Headers follow content alignment** changes this.

**Show Row Extra-Line Infos** enables two-line track rows. **Panel Settings…**
controls alternating backgrounds, global or in-group row parity, selection and
focus opacity, double-click behavior, extra-line color, and selected-track hover
tooltips with a custom title format and delay.

The **State** column displays animated playback, pause, and one-based queue
positions, including multiple queue entries for the same occurrence.
**Show Now Playing** switches to the playing playlist and reveals the track when
it passes the filter, expanding its group if needed.

The custom virtual viewport uses Direct2D/DirectWrite with a buffered GDI
fallback, bounded text caches, smooth pixel scrolling, touch pan/inertia, and
MSAA accessibility. Fonts and geometry respond to DPI changes; **Ctrl+wheel**
adjusts per-panel zoom from **50% to 250%**.

## Groups and artwork

Grouping starts disabled. Use **Groups → Enable Groups** from the track or header
menu to turn it on. The default Album pattern groups by album artist (falling
back to artist), album, and disc number.

- Click a group header to collapse or expand it. The menu also offers
  **Collapse All**, **Expand All**, collapse by default, and auto-collapse to the
  playing group.
- Add, edit, delete, or select patterns with a group key, four header text
  formats, sort expression, and playlist-name filter. Selecting a pattern
  enables grouping and applies its sort order where the playlist permits it.
- **Apply Group Sorting** explicitly sorts matching tracks by the pattern.
- **Enable Playlist Filter** chooses patterns by semicolon-separated playlist
  names, with `*` as fallback. Explicit names take precedence.
  **Use current pattern for this playlist** assigns the current playlist name.
- Two-line headers include track count and total duration. Minimum and extra
  row settings provide group padding.
- Enable the **Cover** column to display front-cover artwork in groups. Artwork
  loads asynchronously and uses a bounded cache.

## Search

The search row has an edit box plus **field** and **scope** dropdowns.
**Ctrl+F** shows and focuses it. Middle-click the track area or search box, or
use **Search → Show search row**, to toggle visibility. Hiding it keeps the
current query and filter active.

Text changes apply after **500 ms** without further typing.

| Field | Matching behavior |
| --- | --- |
| **All fields** | Uses foobar2000's search query parser, including field queries and Boolean expressions. |
| **Artist / Title / Album** | Case-insensitive literal substring matching in the selected field. Quotes and operators are treated as ordinary text. |

For example, use `artist HAS radiohead` or `%rating% GREATER 3` in **All fields**;
use `radiohead` directly in **Artist** mode.

### Current playlist

The default mode filters the track list. **Search → Search box locates tracks**
keeps the list and selects/reveals the first match instead, temporarily expanding
a collapsed group if necessary. No match leaves selection unchanged.

Invalid queries display an inline notice. In filter mode an invalid query shows
no rows. Escape clears the search and returns focus to the playlist.

### Media library

**Media library** scope searches the library and replaces the contents of a
fixed-name **Media Library Search** playlist, then activates it. This is a reusable
snapshot, separate from the optional Media Library autoplaylist.

An existing playlist named **Media Library Search** is reused and its contents
replaced, with an undo backup. Locked or special reserved playlists are left
unchanged with an error. A valid query with no matches empties the results;
clearing the query or entering an invalid query retains the previous results.
The library is searched again on the next query or field/scope change.

### Type to locate and highlights

With the track list focused, type to locate the first matching **artist** among
tracks passing the current filter, including collapsed groups. Choose
**Search → Typing searches group key** to search the current grouping key instead.
A large-text overlay shows the typed string and whether a match was found.
Backspace removes a character; Escape clears the string. It also clears after
**one second** of inactivity or when the list loses focus. This search is separate
from the search-box query. Space retains its selection action until typing search
is underway.

Literal matches are highlighted in cells, extra lines, and group headers.
**Search → Highlight color…** changes the highlight background, with automatic
text contrast; **Reset highlight color** restores the default. Structured
foobar2000 queries filter/locate tracks but do not infer highlights from query
operators, comparisons, or negated conditions.

## Track operations and shortcuts

Ctrl/Shift selection, in-playlist drag reordering, clipboard operations, and
foobar2000's undo/redo are supported. Double-click plays by default, or enqueues
when configured in **Panel Settings…**. The track context menu includes native
foobar2000 commands for the selected visible tracks; available commands depend
on your installed components.

Duplicate track occurrences remain distinct by playlist index. Copy, cut,
remove, and queue actions operate on selected visible occurrences; hidden host
selection is retained. Filtered reordering preserves hidden slots. Removing
tracks naturally shifts subsequent indices, but does not remove hidden tracks.
Playlist locks prevent prohibited edits; copying remains available.

These shortcuts apply when the relevant Modern Playlist control receives the
input. Host/global shortcuts can take precedence.

| Shortcut or gesture | Context | Action |
| --- | --- | --- |
| **Ctrl+F** | Panel | Show/focus search and select its text. |
| **Ctrl+N** | Panel | Create and activate a new playlist. |
| **Ctrl+T** | Panel | Toggle column headers. |
| **Ctrl+wheel** | Panel | Adjust zoom in 10% steps, within 50–250%. |
| **Middle-click** | Track list or search box | Show/hide the search row. |
| **Escape** | Search box or track list | Clear search; in the list, clear an active typing search first. |
| **Enter** | Track list | Play the focused track. |
| **Delete** | Track list | Remove selected visible tracks from the playlist, not from disk. |
| **Ctrl+A** | Track list | Select all visible tracks. |
| **Ctrl+C / Ctrl+X** | Track list | Copy/cut selected visible tracks. |
| **Ctrl+V** | Track list | Append tracks copied from foobar2000. |
| **Ctrl+Z / Ctrl+Y** | Track list | Undo/redo playlist changes. |
| **Alt+Up / Alt+Down** | Track list | Nudge selected tracks within the visible order. |
| **Arrow keys / Page Up / Page Down / Home / End** | Track list | Navigate tracks; Ctrl/Shift modify selection behavior. |
| **Left / Right / Home / End** | Playlist manager | Activate adjacent, first, or last playlist. |
| **F2** | Playlist manager | Rename the active ordinary playlist. |
| **Escape** | Tab drag | Cancel reordering. |

If the column header is hidden, restore it with **Ctrl+T** or **Show column
headers** in the track context menu.

## Saved settings

Each panel saves its own configuration, including column layouts per playlist
GUID, grouping patterns, appearance, zoom, tab visibility/placement, and search
settings. Columns UI layout export/import includes the panel configuration.
Configuration **version 11** reads older versions 1–10 with defaults for newer
settings.

Current-playlist query text follows playlist creation, removal, and reordering
for the lifetime of the panel. Query text and incremental typing strings are
**not saved across restarts**. Special-playlist flags and identities are saved
globally, separately from panel settings.

## Building and validation

### Windows: MSBuild and packaging

Install Microsoft C++ build tools with **MSVC v145**, **MSBuild**, and a
**Windows SDK**. Run from a Visual Studio Developer Command Prompt or PowerShell
in the repository root:

```powershell
msbuild foo_modernplaylist.sln /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145 /v:minimal
```

Output: `build/x64/Release/foo_modernplaylist.dll`. `Debug` and `Win32`
configurations are also available. The included VS Code **Ctrl+Shift+B** task
runs the same Release x64 command when MSBuild is available in its environment.
CMake and Python are not required for the MSBuild component build.

Dependencies are supplied under `lib/columns_ui`: the foobar2000 SDK, PFC,
Columns UI SDK, and architecture-specific shared libraries. `msbuild/compat`
provides compatibility/forwarding headers. The projects use **C++20** and link
Direct2D, DirectWrite, Windows Imaging Component, and other Windows libraries.

### Windows: CMake alternative

CMake **3.24 or newer** and the Microsoft C++ toolchain are required:

```powershell
cmake -S . -B build-cmake-x64 -A x64 -T v145
cmake --build build-cmake-x64 --config Release --parallel
```

## Credits

Built with the foobar2000 SDK, PFC, Columns UI SDK, and Windows APIs.
This component is provided as-is for educational and personal use.
