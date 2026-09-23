#pragma once
#include <windows.h>
#include <functional>
#include <commctrl.h>
#include <string>
#include <vector>
#include <memory>
#include "grouping.h"
#include "search_model.h"

namespace modern_playlist {
// Retains the panel's small LVM/WM_NOTIFY contract while owning pixel geometry.
// This is a custom virtual viewport, not a subclassed native list view.
struct viewport_style {
    int row_height = 30, header_height = 31, padding = 6;
    COLORREF row = 0, alternate = 0, text = 0, selection = 0, selected_text = 0, focus = 0;
    bool alternating = true, group_parity = false, extra_line = false, derived_extra_color = true;
    unsigned selection_alpha = 255, focus_alpha = 255, tooltip_delay = 650;
    bool tooltips = false, enqueue_default = false;
    COLORREF secondary = 0;

};
inline constexpr UINT viewport_enqueue_query = WM_APP + 208;
inline constexpr UINT viewport_row_info = 0x80001001, viewport_cell_info = 0x80001002, viewport_tooltip_info = 0x80001003;
struct viewport_row_request {
    NMHDR hdr{}; int row = 0;
    size_t global_index = 0, group_index = 0;
    bool playing = false, paused = false;
    std::wstring queue;
};
struct viewport_cell_request {
    NMHDR hdr{}; int row = 0, column = 0;
    bool state_column = false; std::wstring secondary;
};
struct viewport_tooltip_request { NMHDR hdr{}; int row = 0; std::wstring text; };
struct cover_pixels { unsigned width=0, height=0; std::vector<unsigned char> bgra; };
struct viewport_group {
    group_band band;
    std::wstring l1,r1,l2,r2;
    bool collapsed=false, cover=false;
};
inline constexpr UINT viewport_group_toggle=0x80001004, viewport_group_cover=0x80001005;
struct viewport_group_request { NMHDR hdr{}; int group=-1; bool load=true; std::shared_ptr<cover_pixels> pixels; };
struct viewport_search {
    std::vector<std::wstring> terms;
    std::wstring overlay;
    COLORREF color=0;
    bool found=true;
};
void set_playlist_search(HWND window,const viewport_search& search);
void set_playlist_groups(HWND window,const std::vector<viewport_group>& groups);
void set_playlist_playback(HWND window, int row, bool paused);
HWND create_playlist_viewport(HWND parent, HINSTANCE instance);
void configure_playlist_viewport(HWND window, const viewport_style& style);
void invalidate_playlist_row(HWND window, int row); // drops cached text and coalesces dirty rects
// The predicate is called synchronously, only for bounded cached rows.
void invalidate_playlist_rows(HWND window, const std::function<bool(int)>& affected);
void reset_playlist_scroll(HWND window);
void suspend_playlist_input(HWND window, bool suspended);
}
