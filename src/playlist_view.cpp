#include <SDK/foobar2000.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
#include <memory>
#include <numeric>
#include <string>
#include <vector>
#include <map>
#include <future>
#include <chrono>
#include <wincodec.h>
#include <wrl/client.h>
#include <shlwapi.h>
#include "grouping.h"
#include "search_model.h"
#include <commdlg.h>
#include "manager_model.h"
#include "special_playlists.h"
#include <SDK/autoplaylist.h>
#include "model.h"
#include "playlist_viewport.h"
#include "viewport_accessibility.h"
#include "playlist_core.h"
#include "playlist_view.h"
#include "resource.h"

namespace {
constexpr auto element_id = modern_playlist::element_id;
constexpr UINT refresh_message = WM_APP + 71;
constexpr UINT search_timer = 1, incremental_timer = 6;
constexpr UINT state_timer = 3, state_message = WM_APP + 74;
std::vector<HWND> queue_windows;
class queue_notifications : public playback_queue_callback {
    void on_changed(t_change_origin) override { for (auto window : queue_windows) PostMessageW(window,state_message,0,0); }
};
service_factory_single_t<queue_notifications> queue_factory;
constexpr UINT fit_columns_message = WM_APP + 72, header_order_message = WM_APP + 75;
constexpr wchar_t search_placeholder[] = L"Search…";
struct palette_colors {
    COLORREF surface;
    COLORREF row;
    COLORREF alternate;
    COLORREF search_bg;
    COLORREF text;
    COLORREF selected_text;
    COLORREF muted;
    COLORREF header;
    COLORREF divider;
    COLORREF selection;
    COLORREF border;
    COLORREF highlight = 0;
};
namespace palette {
constexpr palette_colors dark = {
    RGB(32,35,37),    // surface
    RGB(29,31,32),    // row
    RGB(20,22,23),    // alternate
    RGB(20,22,23),    // search_bg
    RGB(238,238,238), // text
    RGB(238,238,238), // selected_text
    RGB(156,164,170), // muted
    RGB(44,68,82),    // header
    RGB(77,96,108),   // divider
    RGB(52,173,225),  // selection
    RGB(64,76,83)     // border
};
constexpr palette_colors light = {
    RGB(242,244,246), // surface
    RGB(255,255,255), // row
    RGB(246,248,250), // alternate
    RGB(255,255,255), // search_bg
    RGB(32,35,38),    // text
    RGB(255,255,255), // selected_text
    RGB(120,130,140), // muted
    RGB(224,232,238), // header
    RGB(198,208,218), // divider
    RGB(48,150,206),  // selection
    RGB(205,212,218)  // border
};
}
void fill(HDC dc, const RECT& r, COLORREF color) {
    SetDCBrushColor(dc,color); FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}
std::wstring wide(const char* s) { return pfc::stringcvt::string_wide_from_utf8(s).get_ptr(); }
std::string utf8(const std::wstring& s) { return pfc::stringcvt::string_utf8_from_wide(s.c_str()).get_ptr(); }
std::wstring window_text(HWND w) {
    std::wstring s(GetWindowTextLengthW(w) + 1, L'\0');
    s.resize(GetWindowTextW(w, s.data(), static_cast<int>(s.size()))); return s;
}
struct column {
    std::string title, pattern;
    int width = 150, align = LVCFMT_LEFT;
    bool visible = true;
    titleformat_object::ptr script;
    std::string secondary_pattern;
    titleformat_object::ptr secondary_script;
    bool state = false;
    int percent = 0; // Visible width share in basis points.
    std::string ref = "Text", sort_pattern;
    titleformat_object::ptr sort_script;
};
column state_column() { column c{"State","",65,LVCFMT_CENTER}; c.state=true; c.ref="State"; return c; }
std::vector<column> defaults() {
    std::vector<column> result={
        {"Cover","",75}, state_column(), {"Index","",60,LVCFMT_RIGHT},
        {"#","$if2(%tracknumber%,-)",55,LVCFMT_RIGHT}, {"Title","$if2(%title%,%filename_ext%)",240},
        {"Year","$if(%date%,$year($replace(%date%,/,-,.,-)),'-')",65,LVCFMT_RIGHT},
        {"Artist","$if(%length%,%artist%,'Stream')",170},
        {"Album","$if2(%album%,$if(%length%,'Single','Web radios'))",170},
        {"Genre","$if2(%genre%,'Other')",120}, {"Mood","$if(%FEEDBACK%,1,0)",65,LVCFMT_CENTER},
        {"Rating","$if2(%rating%,0)",65,LVCFMT_CENTER},
        {"Plays","$if2(%play_counter%,$if2(%play_count%,0))",65,LVCFMT_RIGHT},
        {"Bitrate","%__bitrate% kbps",85,LVCFMT_RIGHT}, {"Time","$if2(%length%,'00:00')",80,LVCFMT_RIGHT}};    const char* refs[]={"Cover","State","Index","Tracknumber","Title","Date","Artist","Album",
                        "Genre","Mood","Rating","Playcount","Bitrate","Duration"};
    for (size_t i=0;i<result.size();++i) result[i].ref=refs[i];
    for (size_t i : {0,2,5,8,9,10,11,12}) result[i].visible=false;
    result[4].secondary_pattern="$if(%length%,%artist%,)";
    result[8].secondary_pattern="$if2(%genre%,'Other')";
    result[13].secondary_pattern="%__bitrate% kbps";
    const char* orders[]={
        "", "", "",
        "%tracknumber% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %title%",
        "%title% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber%",
        "%date% | %album artist% | %album% | %discnumber% | %tracknumber% | %title%",
        "%artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%album%,%artist%) | $if(%album%,%date%,'9999') | %album artist% | %discnumber% | %tracknumber% | %title%",
        "%genre% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%FEEDBACK% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%rating% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%play_counter%,$if2(%play_count%,0)) | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "%__bitrate% | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%",
        "$if2(%length%,' 0:00') | %album artist% | $if(%album%,%date%,'9999') | %album% | %discnumber% | %tracknumber% | %title%"};
    for (size_t i=3;i<result.size();++i) result[i].sort_pattern=orders[i];
    int total=0; for (const auto& c : result) if (c.visible) total+=c.width;
    for (auto& c : result) if (c.visible) c.percent=c.width*10000/total;
    return result;
}
struct dialog_data { std::wstring value; column edited; bool is_column = false; };
INT_PTR CALLBACK dialog_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* d = reinterpret_cast<dialog_data*>(GetWindowLongPtrW(wnd, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        d = reinterpret_cast<dialog_data*>(lp); SetWindowLongPtrW(wnd, DWLP_USER, lp);
        if (d->is_column) {
            SetDlgItemTextW(wnd, IDC_TITLE, wide(d->edited.title.c_str()).c_str());
            SetDlgItemTextW(wnd, IDC_PATTERN, wide(d->edited.pattern.c_str()).c_str());
            SetDlgItemTextW(wnd, IDC_SECONDARY, wide(d->edited.secondary_pattern.c_str()).c_str());
            SetDlgItemTextW(wnd, IDC_SORT_PATTERN, wide(d->edited.sort_pattern.c_str()).c_str());
            SetDlgItemTextW(wnd, IDC_REF, wide(d->edited.ref.c_str()).c_str());
            SetDlgItemInt(wnd,IDC_PERCENT,d->edited.percent/100,FALSE);
            for (auto* name : {L"Left", L"Right", L"Center"}) SendDlgItemMessageW(wnd, IDC_ALIGN, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
            SendDlgItemMessageW(wnd, IDC_ALIGN, CB_SETCURSEL, d->edited.align, 0);
        } else SetDlgItemTextW(wnd, IDC_VALUE, d->value.c_str());
        return TRUE;
    }
    if (msg == WM_COMMAND && LOWORD(wp) == IDCANCEL) { EndDialog(wnd, IDCANCEL); return TRUE; }
    if (msg == WM_COMMAND && LOWORD(wp) == IDOK && d) {
        if (d->is_column) {
            const auto title=utf8(window_text(GetDlgItem(wnd,IDC_TITLE)));
            const auto pattern=utf8(window_text(GetDlgItem(wnd,IDC_PATTERN)));
            const auto secondary=utf8(window_text(GetDlgItem(wnd,IDC_SECONDARY)));
            const auto sort=utf8(window_text(GetDlgItem(wnd,IDC_SORT_PATTERN)));
            auto ref=utf8(window_text(GetDlgItem(wnd,IDC_REF))); if (ref.empty()) ref="Text";
            BOOL valid_weight=FALSE; const auto weight=GetDlgItemInt(wnd,IDC_PERCENT,&valid_weight,FALSE);
            titleformat_object::ptr script;
            const bool generated=ref=="State" || ref=="Cover" || ref=="Index";
            if (title.empty() || ref.size()>64 || !valid_weight || weight>100 ||
                (!generated && (pattern.empty() || !titleformat_compiler::get()->compile(script,pattern.c_str()))) ||
                (!secondary.empty() && !titleformat_compiler::get()->compile(script,secondary.c_str())) ||
                (!sort.empty() && !titleformat_compiler::get()->compile(script,sort.c_str()))) {
                MessageBoxW(wnd,L"Enter a title, valid title formats, a semantic ref, and a width weight from 0 to 100.",
                    L"Playlist column",MB_OK|MB_ICONINFORMATION); return TRUE;
            }
            d->edited.title=title; d->edited.pattern=pattern; d->edited.secondary_pattern=secondary;
            d->edited.sort_pattern=sort; d->edited.ref=ref; d->edited.state=ref=="State";
            d->edited.percent=static_cast<int>(weight)*100;
            d->edited.align=static_cast<int>(SendDlgItemMessageW(wnd,IDC_ALIGN,CB_GETCURSEL,0,0));
        } else {
            d->value = window_text(GetDlgItem(wnd, IDC_VALUE));
            if (d->value.empty()) return TRUE;
        }
        EndDialog(wnd, IDOK); return TRUE;
    }
    return FALSE;
}

INT_PTR CALLBACK core_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* settings=reinterpret_cast<modern_playlist::core_settings*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if (msg==WM_INITDIALOG) {
        settings=reinterpret_cast<modern_playlist::core_settings*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        for (auto value : {L"Play",L"Add to playback queue"}) SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_SETCURSEL,settings->enqueue_on_double_click,0);
        for (auto value : {L"Global track index",L"Within album group"}) SendDlgItemMessageW(wnd,IDC_PARITY,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(value));
        SendDlgItemMessageW(wnd,IDC_PARITY,CB_SETCURSEL,settings->group_parity,0);
        SetDlgItemInt(wnd,IDC_SELECTION_ALPHA,settings->selection_alpha,FALSE);
        SetDlgItemInt(wnd,IDC_FOCUS_ALPHA,settings->focus_alpha,FALSE);
        SetDlgItemInt(wnd,IDC_TOOLTIP_DELAY,settings->tooltip_delay,FALSE);
        CheckDlgButton(wnd,IDC_ALTERNATING,settings->alternating?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_EXTRA_LINE,settings->extra_line?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_DERIVED_EXTRA,settings->derived_extra_color?BST_CHECKED:BST_UNCHECKED);
        CheckDlgButton(wnd,IDC_TOOLTIPS,settings->tooltips?BST_CHECKED:BST_UNCHECKED);
        SetDlgItemTextW(wnd,IDC_TOOLTIP_PATTERN,wide(settings->tooltip_pattern.c_str()).c_str()); return TRUE;
    }
    if (msg==WM_COMMAND && LOWORD(wp)==IDCANCEL) { EndDialog(wnd,IDCANCEL); return TRUE; }
    if (msg==WM_COMMAND && LOWORD(wp)==IDOK && settings) {
        BOOL a=FALSE,b=FALSE,c=FALSE;
        const auto selection=GetDlgItemInt(wnd,IDC_SELECTION_ALPHA,&a,FALSE), focus=GetDlgItemInt(wnd,IDC_FOCUS_ALPHA,&b,FALSE), delay=GetDlgItemInt(wnd,IDC_TOOLTIP_DELAY,&c,FALSE);
        const auto pattern=utf8(window_text(GetDlgItem(wnd,IDC_TOOLTIP_PATTERN)));
        titleformat_object::ptr compiled;
        if (!a || !b || !c || selection>255 || focus>255 || delay<100 || delay>5000 || pattern.size()>16384 ||
            (!pattern.empty() && !titleformat_compiler::get()->compile(compiled,pattern.c_str()))) {
            MessageBoxW(wnd,L"Use opacity 0-255, dwell 100-5000 ms, and a valid tooltip title format.",L"Playlist settings",MB_OK|MB_ICONINFORMATION); return TRUE;
        }
        settings->selection_alpha=selection; settings->focus_alpha=focus; settings->tooltip_delay=delay; settings->tooltip_pattern=pattern;
        settings->enqueue_on_double_click=SendDlgItemMessageW(wnd,IDC_DOUBLE_CLICK,CB_GETCURSEL,0,0)==1;
        settings->group_parity=SendDlgItemMessageW(wnd,IDC_PARITY,CB_GETCURSEL,0,0)==1;
        settings->alternating=IsDlgButtonChecked(wnd,IDC_ALTERNATING)==BST_CHECKED;
        settings->extra_line=IsDlgButtonChecked(wnd,IDC_EXTRA_LINE)==BST_CHECKED;
        settings->derived_extra_color=IsDlgButtonChecked(wnd,IDC_DERIVED_EXTRA)==BST_CHECKED;
        settings->tooltips=IsDlgButtonChecked(wnd,IDC_TOOLTIPS)==BST_CHECKED;
        EndDialog(wnd,IDOK); return TRUE;
    }
    return FALSE;
}

struct group_dialog_data { modern_playlist::group_pattern pattern; unsigned minimum=0, extra=0; };
INT_PTR CALLBACK group_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* d=reinterpret_cast<group_dialog_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        d=reinterpret_cast<group_dialog_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        const std::string* fields[]={&d->pattern.label,&d->pattern.key,&d->pattern.l1,&d->pattern.r1,&d->pattern.l2,&d->pattern.r2,&d->pattern.sort_order,&d->pattern.playlist_filter};
        for(int i=0;i<8;++i) { SetDlgItemTextW(wnd,1300+i,wide(fields[i]->c_str()).c_str()); SendDlgItemMessageW(wnd,1300+i,EM_SETLIMITTEXT,16384,0); }
        SetDlgItemInt(wnd,1308,d->minimum,FALSE); SetDlgItemInt(wnd,1309,d->extra,FALSE); return TRUE;
    }
    if(msg==WM_COMMAND && d) {
        if(LOWORD(wp)==IDCANCEL) { EndDialog(wnd,IDCANCEL); return TRUE; }
        if(LOWORD(wp)==IDOK) {
            auto edited=*d;
            std::string* fields[]={&edited.pattern.label,&edited.pattern.key,&edited.pattern.l1,&edited.pattern.r1,&edited.pattern.l2,&edited.pattern.r2,&edited.pattern.sort_order,&edited.pattern.playlist_filter};
            bool valid=true;
            for(int i=0;i<8;++i) {
                *fields[i]=utf8(window_text(GetDlgItem(wnd,1300+i)));
                titleformat_object::ptr script;
                if(i>=1 && i<=6 && !fields[i]->empty() && !titleformat_compiler::get()->compile(script,fields[i]->c_str())) valid=false;
            }
            BOOL a=FALSE,b=FALSE; edited.minimum=GetDlgItemInt(wnd,1308,&a,FALSE); edited.extra=GetDlgItemInt(wnd,1309,&b,FALSE);
            if(!valid || edited.pattern.label.empty() || edited.pattern.key.empty() || !a || !b || edited.minimum>100 || edited.extra>100) {
                MessageBoxW(wnd,L"Enter a label, a valid group key and title formats, and row counts from 0 to 100.",L"Group pattern",MB_OK|MB_ICONWARNING); return TRUE;
            }
            *d=std::move(edited); EndDialog(wnd,IDOK); return TRUE;
        }
    }
    return FALSE;
}
struct autoplaylist_data {
    std::string name="New Autoplaylist",query="ALL",sort="%album artist% | %album% | %discnumber% | %tracknumber%";
    bool force=false;
};
INT_PTR CALLBACK autoplaylist_dialog(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
    auto* data=reinterpret_cast<autoplaylist_data*>(GetWindowLongPtrW(wnd,DWLP_USER));
    if(msg==WM_INITDIALOG) {
        data=reinterpret_cast<autoplaylist_data*>(lp); SetWindowLongPtrW(wnd,DWLP_USER,lp);
        SetDlgItemTextW(wnd,IDC_AUTO_NAME,wide(data->name.c_str()).c_str());
        SetDlgItemTextW(wnd,IDC_AUTO_QUERY,wide(data->query.c_str()).c_str());
        SetDlgItemTextW(wnd,IDC_AUTO_SORT,wide(data->sort.c_str()).c_str());
        for(int id:{IDC_AUTO_NAME,IDC_AUTO_QUERY,IDC_AUTO_SORT}) SendDlgItemMessageW(wnd,id,EM_SETLIMITTEXT,16384,0);
        CheckDlgButton(wnd,IDC_AUTO_FORCE,data->force?BST_CHECKED:BST_UNCHECKED); return TRUE;
    }
    if(msg==WM_COMMAND && data) {
        if(LOWORD(wp)==IDCANCEL) { EndDialog(wnd,IDCANCEL); return TRUE; }
        if(LOWORD(wp)==IDOK) {
            auto edited=*data;
            edited.name=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_NAME)));
            edited.query=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_QUERY)));
            edited.sort=utf8(window_text(GetDlgItem(wnd,IDC_AUTO_SORT)));
            edited.force=IsDlgButtonChecked(wnd,IDC_AUTO_FORCE)==BST_CHECKED;
            try {
                if(edited.name.empty() || edited.query.empty()) throw std::runtime_error("Enter a name and query.");
                search_filter_manager::get()->create(edited.query.c_str());
                titleformat_object::ptr script;
                if(!edited.sort.empty() && !titleformat_compiler::get()->compile(script,edited.sort.c_str())) throw std::runtime_error("Invalid sort title format.");
                *data=std::move(edited); EndDialog(wnd,IDOK);
            } catch(const std::exception& e) { MessageBoxW(wnd,wide(e.what()).c_str(),L"Autoplaylist",MB_OK|MB_ICONWARNING); }
            return TRUE;
        }
    }
    return FALSE;
}
class playlist_view : public ui_element_instance, private playlist_callback_impl_base, private play_callback_impl_base, private ui_config_callback_impl {
public:
    playlist_view(HWND parent, ui_element_config::ptr config, ui_element_instance_callback_ptr callback, bool visible = true)
        : playlist_callback_impl_base(0), play_callback_impl_base(0), callback_(callback) {
        read_config(config);
        INITCOMMONCONTROLSEX cc{sizeof(cc), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&cc);
        WNDCLASSW wc{}; wc.hInstance = core_api::get_my_instance(); wc.lpfnWndProc = window_proc;
        wc.lpszClassName = L"foo_modernplaylist.view"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, wc.lpszClassName, L"Modern Playlist",
            WS_CHILD | (visible ? WS_VISIBLE : 0) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0,0,0,0,parent,nullptr,wc.hInstance,this);
        if (!hwnd_) throw std::runtime_error("Cannot create Modern Playlist window");
        set_callback_flags(static_cast<t_uint32>(playlist_callback::flag_all));
        play_callback_reregister(play_callback::flag_on_playback_new_track | play_callback::flag_on_playback_stop | play_callback::flag_on_playback_pause);
        queue_windows.push_back(hwnd_);
    }
    ~playlist_view() {
        begin_destroy();
        if (hwnd_) DestroyWindow(hwnd_);
        if (background_) DeleteObject(background_);
        if (edit_background_) DeleteObject(edit_background_);
        if (font_) DeleteObject(font_);
        if (bold_font_) DeleteObject(bold_font_);
        if (tabs_font_) DeleteObject(tabs_font_);
        if (default_font_) DeleteObject(default_font_);
        if (close_font_) DeleteObject(close_font_);
    }
    HWND get_wnd() override { return hwnd_; }
    GUID get_guid() override { return element_id; }
    GUID get_subclass() override { return ui_element_subclass_playlist_renderers; }
    void set_default_focus() override { SetFocus(list_); }
    void set_configuration(ui_element_config::ptr config) override {
        read_config(config); theme(); make_columns(); refresh(); layout();
    }
    ui_element_config::ptr get_configuration() override {
        save_playlist_columns();
        ui_element_config_builder b;
        b << t_uint32(11);
        write_columns(b,defaults());
        b << t_uint32(show_tabs_) << t_uint32(0) << t_uint32(fit_to_window_);
        b << t_uint32(playlist_layouts_.size());
        for (const auto& entry : playlist_layouts_) {
            b << entry.id;
            write_columns(b,entry.columns);
        }
        b << t_uint32(zoom_percent_);
        b << t_uint32(core_.enqueue_on_double_click) << t_uint32(core_.alternating) << t_uint32(core_.group_parity)
          << t_uint32(core_.extra_line) << t_uint32(core_.derived_extra_color) << t_uint32(core_.tooltips)
          << t_uint32(core_.selection_alpha) << t_uint32(core_.focus_alpha) << t_uint32(core_.tooltip_delay)
          << pfc::string8(core_.tooltip_pattern.c_str()) << t_uint32(show_header_) << t_uint32(headers_follow_alignment_);
        write_groups(b,grouping_);
        b << t_uint32(manager_bottom_);
        write_search(b,search_settings_);
        return b.finish(element_id);
    }
    void notify(const GUID&, t_size, const void*, t_size) override { if (hwnd_ && !destroying_) { theme(); layout(); invalidate_all(); } }
private:
    HWND hwnd_ = nullptr, tabs_ = nullptr, search_ = nullptr, list_ = nullptr, header_ = nullptr, notice_ = nullptr, add_ = nullptr, tab_left_ = nullptr, tab_right_ = nullptr;
    HWND search_field_=nullptr, search_scope_=nullptr;
    modern_playlist::search_settings search_settings_;
    modern_playlist::incremental_search incremental_;
    std::vector<std::wstring> highlight_terms_;
    std::wstring applied_query_;
    t_size search_reveal_=pfc::infinite_size;
    titleformat_object::ptr artist_search_;
    ui_element_instance_callback_ptr callback_;
    HBRUSH background_ = nullptr, edit_background_ = nullptr;
    HFONT font_ = nullptr, bold_font_ = nullptr, tabs_font_ = nullptr, default_font_ = nullptr;
    palette_colors colors_ = palette::dark;
    int row_pixels_ = 30, tab_pixels_ = 30, text_pixels_ = 19, header_pixels_ = 31;
    UINT dpi_ = 96;
    int zoom_percent_ = 100, zoom_wheel_ = 0;
    bool destroying_ = false;
    bool tabs_dirty_ = true;
    bool manager_bottom_=false;
    modern_playlist::manager_geometry manager_;
    std::vector<std::wstring> tab_names_;
    int drag_before_=-1;
    size_t drag_epoch_=0;
    HFONT close_font_=nullptr;
    Microsoft::WRL::ComPtr<modern_playlist::viewport_accessibility> manager_accessible_;
    bool show_tabs_ = false, show_header_ = true, headers_follow_alignment_ = false;
    bool fit_to_window_ = true;
    bool is_dark_mode() const noexcept {
        if (callback_.is_valid()) return callback_->is_dark_mode();
        return ui_config_manager::g_is_dark_mode();
    }
    const palette_colors& current_palette() const noexcept { return colors_; }
    std::wstring notice_text_;
    struct playlist_layout {
        GUID id{};
        std::vector<column> columns;
    };
    std::vector<playlist_layout> playlist_layouts_;
    GUID layout_id_{};
    bool have_layout_ = false;
    std::vector<column> columns_;
    std::vector<int> visible_columns_;
    std::vector<t_size> rows_;
    std::vector<modern_playlist::playlist_row<metadb_handle_ptr>> row_data_;
    modern_playlist::core_settings core_;
    modern_playlist::grouping_settings grouping_;
    std::vector<t_size> filtered_rows_;
    std::vector<modern_playlist::viewport_group> groups_;
    std::vector<std::vector<t_size>> group_members_;
    std::vector<std::string> group_ids_;
    std::map<std::string,bool> collapsed_;
    titleformat_object::ptr group_labels_[4], group_sort_;
    t_size auto_item_=pfc::infinite_size;
    bool apply_filter_next_=true;
    std::map<std::string,std::shared_ptr<modern_playlist::cover_pixels>> covers_;
    std::future<std::shared_ptr<modern_playlist::cover_pixels>> cover_job_;
    std::string cover_job_key_;
    abort_callback_impl cover_abort_;
    titleformat_object::ptr group_script_, tooltip_script_;
    metadb_handle_list items_;
    std::vector<std::wstring> queries_;
    t_size active_ = pfc::infinite_size;
    bool rebuilding_ = false, pending_ = false, dragging_tracks_ = false;
    size_t playlist_epoch_ = 0, content_epoch_ = 0;
    int stretched_column_ = -1, stretched_base_width_ = 0;
    bool fitting_columns_ = false;
    int sort_column_ = -1, sort_direction_ = 1, drag_tab_ = -1;
    HWND drag_ghost_ = nullptr; POINT drag_tab_start_{}, drag_grab_{}; bool drag_tab_moved_ = false;
    std::wstring cell_;
    enum class hover_area { none, search, playlist };
    hover_area hover_ = hover_area::none;
    int hovered_tab_ = -1;
    bool hovered_add_ = false;
    int tab_wheel_accumulator_ = 0;
    int scale(int n) const { return MulDiv(n, static_cast<int>(dpi_) * zoom_percent_, 9600); }
    void update_dpi() {
        using get_dpi_t = UINT(WINAPI*)(HWND);
        const auto get_dpi = reinterpret_cast<get_dpi_t>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (get_dpi) dpi_ = std::max(96U, get_dpi(hwnd_));
        else { HDC dc=GetDC(hwnd_); dpi_=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(hwnd_,dc); }
    }
    void zoom(short delta) {
        zoom_wheel_ += delta;
        const int steps=zoom_wheel_/WHEEL_DELTA; zoom_wheel_%=WHEEL_DELTA;
        const int next=std::clamp(zoom_percent_+steps*10,50,250);
        if (next==zoom_percent_) return;
        capture_columns(); zoom_percent_=next;
        const int sorted=sort_column_;
        theme(); make_columns(); sort_column_=sorted; layout(); invalidate_all();
    }
    void forget_child(HWND wnd) noexcept {
        if(wnd==tabs_ && manager_accessible_) manager_accessible_->disconnect();
        for (HWND* child : {&tabs_,&search_,&list_,&header_,&notice_,&add_,&tab_left_,&tab_right_,&search_field_,&search_scope_})
            if (*child==wnd) *child=nullptr;
    }
    void begin_destroy() noexcept {
        if (destroying_) return;
        destroying_=true;
        if(manager_accessible_) manager_accessible_->disconnect();
        cover_abort_.abort();
        if(cover_job_.valid()) cover_job_.wait();
        if(hwnd_) { KillTimer(hwnd_,4); KillTimer(hwnd_,5); KillTimer(hwnd_,incremental_timer); }
        queue_windows.erase(std::remove(queue_windows.begin(),queue_windows.end(),hwnd_),queue_windows.end());
        if (hwnd_) KillTimer(hwnd_,state_timer);
        // Default UI can destroy the HWND before releasing this service object.
        // Capture while the controls still exist; later get_configuration() uses
        // the saved column data without sending messages to dead/reused HWNDs.
        try { save_playlist_columns(); } catch (...) { /* Keep the last saved layout. */ }
        set_callback_flags(0);
        play_callback_reregister(0);
        pending_=false;
        dragging_tracks_=false;
        drag_tab_=-1;
        if (drag_ghost_) { DestroyWindow(drag_ghost_); drag_ghost_=nullptr; }
        if (hwnd_) KillTimer(hwnd_,search_timer);
        for (HWND child : {tabs_,search_,list_,header_,add_,tab_left_,tab_right_,search_field_,search_scope_})
            if (child) RemoveWindowSubclass(child,child_proc,1);
        if ((list_ && GetCapture()==list_) || (tabs_ && GetCapture()==tabs_)) ReleaseCapture();
    }
    static void write_search(ui_element_config_builder& b,const modern_playlist::search_settings& settings) {
        b << t_uint32(settings.visible) << t_uint32(settings.group_key) << t_uint32(settings.locate)
          << t_uint32(settings.field) << t_uint32(settings.scope) << t_uint32(settings.color);
    }
    static modern_playlist::search_settings read_search(ui_element_config_parser& p,t_uint32 version) {
        modern_playlist::search_settings settings;
        if(version<11) return settings;
        t_uint32 visible,group,locate,field,scope,color;
        p >> visible >> group >> locate >> field >> scope >> color;
        if(visible>1 || group>1 || locate>1 || field>3 || scope>1 || color>0xffffff)
            throw std::runtime_error("Invalid search settings");
        settings.visible=visible!=0; settings.group_key=group!=0; settings.locate=locate!=0;
        settings.field=field; settings.scope=scope; settings.color=color; return settings;
    }
    static void write_groups(ui_element_config_builder& b,const modern_playlist::grouping_settings& settings) {
        b << t_uint32(settings.enabled) << t_uint32(settings.playlist_filter) << t_uint32(settings.collapse_default)
          << t_uint32(settings.autocollapse) << t_uint32(settings.minimum_rows) << t_uint32(settings.extra_rows)
          << t_uint32(settings.pattern) << t_uint32(settings.patterns.size());
        for(const auto& p:settings.patterns)
            b << pfc::string8(p.label.c_str()) << pfc::string8(p.key.c_str()) << pfc::string8(p.l1.c_str()) << pfc::string8(p.r1.c_str())
              << pfc::string8(p.l2.c_str()) << pfc::string8(p.r2.c_str()) << pfc::string8(p.sort_order.c_str()) << pfc::string8(p.playlist_filter.c_str());
    }
    static modern_playlist::grouping_settings read_groups(ui_element_config_parser& p) {
        modern_playlist::grouping_settings settings;
        t_uint32 enabled,filter,collapsed,automatic,minimum,extra,index,count;
        p >> enabled >> filter >> collapsed >> automatic >> minimum >> extra >> index >> count;
        if(enabled>1 || filter>1 || collapsed>1 || automatic>1 || minimum>100 || extra>100 || count<1 || count>64 || index>=count)
            throw std::runtime_error("Invalid grouping settings");
        settings.enabled=enabled!=0; settings.playlist_filter=filter!=0; settings.collapse_default=collapsed!=0; settings.autocollapse=automatic!=0;
        settings.minimum_rows=minimum; settings.extra_rows=extra; settings.pattern=index; settings.patterns.clear();
        for(t_uint32 i=0;i<count;++i) {
            modern_playlist::group_pattern pattern;
            std::string* fields[]={&pattern.label,&pattern.key,&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2,&pattern.sort_order,&pattern.playlist_filter};
            for(auto field:fields) { pfc::string8 text; p >> text; if(text.length()>16384) throw std::runtime_error("Group pattern too long"); *field=text.c_str(); }
            if(pattern.label.empty() || pattern.key.empty()) throw std::runtime_error("Empty group pattern");
            settings.patterns.push_back(std::move(pattern));
        }
        return settings;
    }
    static bool read_manager_position(ui_element_config_parser& p,t_uint32 version) {
        if(version<10) return false;
        t_uint32 bottom; p >> bottom;
        if(bottom>1) throw std::runtime_error("Invalid manager position");
        return bottom!=0;
    }
    static void write_columns(ui_element_config_builder& b, const std::vector<column>& columns) {
        b << t_uint32(columns.size());
        for (const auto& c : columns)
            b << pfc::string8(c.title.c_str()) << pfc::string8(c.pattern.c_str())
              << t_uint32(c.width) << t_uint32(c.align) << t_uint32(c.visible)
              << pfc::string8(c.secondary_pattern.c_str()) << t_uint32(c.state)
              << t_uint32(c.percent) << pfc::string8(c.ref.c_str()) << pfc::string8(c.sort_pattern.c_str());
    }
    static std::vector<column> read_columns(ui_element_config_parser& p,t_uint32 version) {
        t_uint32 count; p >> count;
        if (count == 0 || count > 64) throw std::runtime_error("Invalid column count");
        std::vector<column> loaded;
        for (t_uint32 i=0;i<count;++i) {
            pfc::string8 title, pattern; t_uint32 width, align, visible;
            p >> title >> pattern >> width >> align >> visible;
            if (width < 20 || width > 4000 || align > 2)
                throw std::runtime_error("Invalid column dimensions");
            loaded.push_back({title.c_str(),pattern.c_str(),static_cast<int>(width),static_cast<int>(align),visible != 0});
            if (version>=7) {
                pfc::string8 secondary; t_uint32 state; p >> secondary >> state;
                if (state>1) throw std::runtime_error("Invalid column type");
                loaded.back().secondary_pattern=secondary.c_str(); loaded.back().state=state!=0;
            } else if (pattern=="%title%") loaded.back().secondary_pattern="[%artist%]";
            if (version>=8) {
                t_uint32 percent; pfc::string8 ref, sort; p >> percent >> ref >> sort;
                if (percent>100000 || ref.length()>64 || sort.length()>16384) throw std::runtime_error("Invalid column metadata");
                loaded.back().percent=static_cast<int>(percent); loaded.back().ref=ref.c_str(); loaded.back().sort_pattern=sort.c_str();
            } else loaded.back().ref=loaded.back().state?"State":"Text";
        }
        if (std::none_of(loaded.begin(),loaded.end(),[](const auto& c){return c.visible;}))
            loaded[0].visible=true;
        if (version<7 && loaded.size()<64) loaded.insert(loaded.begin(),state_column());
        if (version<8) {
            int total=0; for (const auto& c:loaded) if (c.visible) total+=c.width;
            for (auto& c:loaded) if (c.visible) c.percent=std::max(1,c.width*10000/total);
        }
        return loaded;
    }
    void save_playlist_columns() {
        capture_columns();
        if (!have_layout_) return;
        for (auto& entry : playlist_layouts_) if (entry.id == layout_id_) {
            entry.columns=columns_;
            return;
        }
        playlist_layouts_.push_back({layout_id_,columns_});
    }
    void load_playlist_columns(t_size playlist) {
        auto pm=playlist_manager_v5::get();
        const bool valid=playlist < pm->get_playlist_count();
        const GUID id=valid ? pm->playlist_get_guid(playlist) : GUID{};
        if (have_layout_ == valid && (!valid || layout_id_ == id)) return;
        save_playlist_columns();
        have_layout_=valid;
        layout_id_=id;
        columns_=defaults();
        if (valid) for (const auto& entry : playlist_layouts_) if (entry.id == id) {
            columns_=entry.columns;
            break;
        }
        sort_column_=-1; sort_direction_=1;
        compile_columns();
        make_columns();
    }
    void read_config(ui_element_config::ptr config) {
        core_=modern_playlist::core_settings{};
        search_settings_={}; incremental_.clear(); applied_query_.clear(); highlight_terms_.clear();
        grouping_={}; collapsed_.clear(); apply_filter_next_=true;
        columns_=defaults();
        fit_to_window_=true;
        zoom_percent_=100;
        manager_bottom_=false; show_tabs_=false; show_header_=true; headers_follow_alignment_=false;
        have_layout_=false;
        playlist_layouts_.clear();
        if (config.is_valid() && config->get_data_size()) try {
            ui_element_config_parser p(config); t_uint32 version; p >> version;
            if (version < 1 || version > 11) throw std::runtime_error("Invalid column configuration");
            auto loaded=read_columns(p,version);
            t_uint32 tabs=0, fit=1;
            if (version >= 2) p >> tabs;
            if (version >= 3) { t_uint32 reserved; p >> reserved; }
            if (version >= 4) p >> fit;
            std::vector<playlist_layout> layouts;
            if (version >= 5) {
                t_uint32 count; p >> count;
                if (count > 65536) throw std::runtime_error("Invalid playlist layout count");
                for (t_uint32 i=0;i<count;++i) {
                    GUID id; p >> id;
                    layouts.push_back({id,read_columns(p,version)});
                }
            } else {
                // Migrate the shared layout into independent copies for existing playlists.
                auto pm=playlist_manager_v5::get();
                for (t_size i=0;i<pm->get_playlist_count();++i)
                    layouts.push_back({pm->playlist_get_guid(i),loaded});
            }
            t_uint32 zoom=100;
            if (version >= 6) { p >> zoom; if (zoom < 50 || zoom > 250) throw std::runtime_error("Invalid zoom"); }
            if (version>=7) {
                t_uint32 enqueue, alternating, parity, extra, derived, tips, selection, focus, delay; pfc::string8 pattern;
                p >> enqueue >> alternating >> parity >> extra >> derived >> tips >> selection >> focus >> delay >> pattern;
                if (enqueue>1 || alternating>1 || parity>1 || extra>1 || derived>1 || tips>1 || selection>255 || focus>255 || delay<100 || delay>5000 || pattern.length()>16384)
                    throw std::runtime_error("Invalid playlist settings");
                core_.enqueue_on_double_click=enqueue!=0; core_.alternating=alternating!=0; core_.group_parity=parity!=0;
                core_.extra_line=extra!=0; core_.derived_extra_color=derived!=0; core_.tooltips=tips!=0;
                core_.selection_alpha=selection; core_.focus_alpha=focus; core_.tooltip_delay=delay; core_.tooltip_pattern=pattern.c_str();
                if (version>=8) {
                    t_uint32 header, alignment; p >> header >> alignment;
                    if (header>1 || alignment>1) throw std::runtime_error("Invalid header settings");
                    show_header_=header!=0; headers_follow_alignment_=alignment!=0;
                }
            }
            if(version>=9) grouping_=read_groups(p);
            manager_bottom_=read_manager_position(p,version);
            search_settings_=read_search(p,version);
            zoom_percent_=static_cast<int>(zoom);
            columns_=std::move(loaded);
            fit_to_window_=fit != 0;
            show_tabs_=tabs != 0;
            playlist_layouts_=std::move(layouts);
        } catch (const std::exception&) {
            console::print("Modern Playlist: invalid saved layout; using default columns.");
        }
        compile_columns();
    }
    void compile_columns() {
        for (auto& c: columns_) {
            titleformat_compiler::get()->compile_safe(c.script,c.pattern.c_str());
            titleformat_compiler::get()->compile_safe(c.secondary_script,c.secondary_pattern.c_str());
            titleformat_compiler::get()->compile_safe(c.sort_script,c.sort_pattern.empty()?c.pattern.c_str():c.sort_pattern.c_str());
        }
        titleformat_compiler::get()->compile_safe(artist_search_,"[%artist%]");
        const auto& pattern=grouping_.patterns[grouping_.pattern];
        titleformat_compiler::get()->compile_safe(group_script_,pattern.key.c_str());
        const std::string* labels[]={&pattern.l1,&pattern.r1,&pattern.l2,&pattern.r2};
        for(int i=0;i<4;++i) titleformat_compiler::get()->compile_safe(group_labels_[i],labels[i]->c_str());
        titleformat_compiler::get()->compile_safe(group_sort_,pattern.sort_order.c_str());
        titleformat_compiler::get()->compile_safe(tooltip_script_,core_.tooltip_pattern.c_str());
    }
    void capture_columns() {
        if (!list_ || !header_ || visible_columns_.empty()) return;
        std::vector<int> order(visible_columns_.size());
        if (!ListView_GetColumnOrderArray(list_,static_cast<int>(order.size()),order.data())) return;
        std::vector<column> result;
        for (int i : order) {
            auto c = columns_[visible_columns_[i]]; if (!fit_to_window_) c.width = std::clamp(MulDiv(i==stretched_column_?stretched_base_width_:ListView_GetColumnWidth(list_,i),96,scale(96)),20,4000); result.push_back(c);
        }
        for (auto& c : columns_) if (!c.visible) result.push_back(c);
        std::vector<int> column_mapping(columns_.size(),-1);
        for (size_t i=0;i<order.size();++i) column_mapping[visible_columns_[order[i]]]=static_cast<int>(i);
        columns_ = std::move(result);
        if (sort_column_>=0 && static_cast<size_t>(sort_column_)<column_mapping.size())
            sort_column_=column_mapping[sort_column_];
        // The old control's logical indices still describe its original insertion order.
        std::vector<int> old_mapping(order.size());
        for (size_t i=0;i<order.size();++i) old_mapping[order[i]] = static_cast<int>(i);
        visible_columns_ = std::move(old_mapping);
        if (!fit_to_window_) normalize_percents();
    }
    void normalize_percents() {
        int total=0; for (const auto& c:columns_) if (c.visible) total+=c.width;
        for (auto& c:columns_) c.percent=c.visible && total ? std::max(1,c.width*10000/total) : 0;
    }
    void make_columns() {
        stretched_column_ = -1;
        rebuilding_ = true;
        while (ListView_DeleteColumn(list_,0)) {}
        visible_columns_.clear();
        for (size_t i=0;i<columns_.size();++i) if (columns_[i].visible) {
            auto& c = columns_[i]; auto title = wide(c.title.c_str());
            LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            col.pszText = title.data(); col.cx = scale(c.width); col.fmt = c.align;
            ListView_InsertColumn(list_,static_cast<int>(visible_columns_.size()),&col);
            visible_columns_.push_back(static_cast<int>(i));
        }
        rebuilding_ = false;
    }
    void fit_columns() {
        if (destroying_ || !list_ || !header_ || fitting_columns_ || visible_columns_.empty()) return;
        fitting_columns_ = true;
        const int count=static_cast<int>(visible_columns_.size());
        if (fit_to_window_) {
            // Persisted ratios determine widths; a narrow viewport can scroll
            // horizontally instead of shrinking columns below their minimum.
            const int minimum=scale(32);
            std::vector<int> logicals(count), weights(count), widths(count);
            for (int pass=0;pass<2;++pass) {
                RECT client{}; GetClientRect(list_,&client);
                const int available=std::max(int(client.right-client.left),count*minimum);
                int remaining=available, remaining_weight=0;
                for (int order=0;order<count;++order) {
                    const int logical=static_cast<int>(SendMessageW(header_,HDM_ORDERTOINDEX,order,0));
                    logicals[order]=logical;
                    const auto& col=columns_[visible_columns_[logical]];
                    weights[order]=std::max(1,col.percent>0?col.percent:col.width);
                    remaining_weight+=weights[order];
                    widths[order]=0;
                }
                // Reserve minimum widths first, then proportionally share the rest.
                bool changed;
                do {
                    changed=false;
                    for (int order=0;order<count;++order) if (!widths[order] &&
                        static_cast<long long>(weights[order])*remaining<static_cast<long long>(minimum)*remaining_weight) {
                        widths[order]=minimum;
                        remaining-=minimum; remaining_weight-=weights[order]; changed=true;
                    }
                } while (changed && remaining_weight>0);
                int assigned=0; long long cumulative=0;
                for (int order=0;order<count;++order) if (!widths[order]) {
                    cumulative+=weights[order];
                    const int edge=static_cast<int>(cumulative*remaining/remaining_weight);
                    widths[order]=edge-assigned; assigned=edge;
                }
                for (int order=0;order<count;++order) if (ListView_GetColumnWidth(list_,logicals[order])!=widths[order])
                    ListView_SetColumnWidth(list_,logicals[order],widths[order]);
            }            const int offset = GetScrollPos(list_,SB_HORZ);
            if (offset) ListView_Scroll(list_,-offset,0);
            fitting_columns_=false;
            InvalidateRect(list_,nullptr,FALSE);
            InvalidateRect(header_,nullptr,FALSE);
            return;
        }
        const int last=static_cast<int>(SendMessageW(header_,HDM_ORDERTOINDEX,count-1,0));
        if (last<0 || last>=count) { fitting_columns_=false; return; }
        if (last!=stretched_column_) {
            if (stretched_column_>=0) ListView_SetColumnWidth(list_,stretched_column_,stretched_base_width_);
            stretched_column_=last;
            stretched_base_width_=ListView_GetColumnWidth(list_,last);
        }
        RECT client{}; GetClientRect(list_,&client);
        int occupied=0;
        for (int i=0;i<count;++i) if(i!=last) occupied+=ListView_GetColumnWidth(list_,i);
        const int width=std::max(stretched_base_width_,int(client.right)-occupied);
        if (ListView_GetColumnWidth(list_,last)!=width) ListView_SetColumnWidth(list_,last,width);
        fitting_columns_=false;
        InvalidateRect(list_,nullptr,FALSE);
        InvalidateRect(header_,nullptr,FALSE);
    }
    void ui_fonts_changed() override { if (hwnd_ && !destroying_) { theme(); layout(); invalidate_all(); } }
    void ui_colors_changed() override {
        if (hwnd_ && !destroying_) {
            theme();
            layout();
            invalidate_all();
        }
    }
    void invalidate_all() {
        if (!hwnd_ || destroying_) return;
        InvalidateRect(hwnd_, nullptr, TRUE);
        if (list_) InvalidateRect(list_, nullptr, TRUE);
        if (header_) InvalidateRect(header_, nullptr, TRUE);
        if (tabs_) InvalidateRect(tabs_, nullptr, TRUE);
        if (search_) InvalidateRect(search_, nullptr, TRUE);
        if (notice_) InvalidateRect(notice_, nullptr, TRUE);
        if (add_) InvalidateRect(add_, nullptr, TRUE);
        if (tab_left_) InvalidateRect(tab_left_, nullptr, TRUE);
        if (tab_right_) InvalidateRect(tab_right_, nullptr, TRUE);
    }
    static COLORREF blend(COLORREF a, COLORREF b, int percent) {
        return RGB((GetRValue(a)*(100-percent)+GetRValue(b)*percent)/100,
                   (GetGValue(a)*(100-percent)+GetGValue(b)*percent)/100,
                   (GetBValue(a)*(100-percent)+GetBValue(b)*percent)/100);
    }
    HFONT copy_ui_font(const GUID& role, bool bold = false) {
        LOGFONTW lf{};
        const HFONT host=callback_.is_valid()?callback_->query_font_ex(role):nullptr;
        if (!host || !GetObjectW(host,sizeof(lf),&lf)) {
            lf.lfHeight=-scale(13); lf.lfWeight=bold?FW_SEMIBOLD:FW_NORMAL;
            lf.lfQuality=CLEARTYPE_QUALITY; lstrcpyW(lf.lfFaceName,L"Segoe UI");
        }
        else lf.lfHeight=MulDiv(lf.lfHeight,zoom_percent_,100);
        // Host fonts are borrowed; create our own copy so replacement/destruction is safe.
        return CreateFontIndirectW(&lf);
    }
    int font_height(HFONT font) const {
        HDC dc=GetDC(hwnd_); const auto old=SelectObject(dc,font);
        TEXTMETRICW metrics{}; GetTextMetricsW(dc,&metrics);
        SelectObject(dc,old); ReleaseDC(hwnd_,dc);
        return metrics.tmHeight;
    }
    void theme() {
        const bool dark=is_dark_mode();
        colors_=dark?palette::dark:palette::light;
        colors_.highlight=colors_.selection;
        if (callback_.is_valid()) {
            colors_.text=callback_->query_std_color(ui_color_text);
            colors_.row=callback_->query_std_color(ui_color_background);
            colors_.surface=colors_.search_bg=colors_.alternate=colors_.row;
            colors_.selection=callback_->query_std_color(ui_color_selection);
            colors_.highlight=callback_->query_std_color(ui_color_highlight);
            colors_.header=blend(colors_.row,colors_.text,8);
            colors_.border=colors_.divider=blend(colors_.row,colors_.text,25);
            colors_.muted=blend(colors_.row,colors_.text,65);
            // Default UI exposes no selected-text role; choose readable contrast.
            const auto c=colors_.selection;
            colors_.selected_text=(299*GetRValue(c)+587*GetGValue(c)+114*GetBValue(c)>=128000)
                ? RGB(0,0,0) : RGB(255,255,255);
        }
        const auto& pal=current_palette();
        const HFONT old_font=font_, old_header=bold_font_, old_tabs=tabs_font_, old_default=default_font_;
        font_=copy_ui_font(ui_font_playlists);
        bold_font_=copy_ui_font(ui_font_playlists,true);
        tabs_font_=copy_ui_font(ui_font_tabs);
        default_font_=copy_ui_font(ui_font_default);
        row_pixels_=core_.extra_line ? std::max(scale(48),font_height(font_)*2+scale(10)) : std::max(scale(30),font_height(font_)+scale(10));
        header_pixels_=std::max(scale(31),font_height(bold_font_)+scale(10));
        tab_pixels_=std::max(scale(30),font_height(tabs_font_)+scale(10));
        text_pixels_=std::max(scale(19),font_height(default_font_));
        for (HWND child : {search_,notice_,search_field_,search_scope_})
            if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(default_font_),TRUE);
        for (HWND child : {tabs_,add_,tab_left_,tab_right_})
            if (child) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(tabs_font_),TRUE);
        SendMessageW(list_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
        SendMessageW(header_,WM_SETFONT,reinterpret_cast<WPARAM>(bold_font_),TRUE);
        for (HFONT old : {old_font,old_header,old_tabs,old_default}) if (old) DeleteObject(old);
        if (background_) DeleteObject(background_);
        if (edit_background_) DeleteObject(edit_background_);
        background_=CreateSolidBrush(pal.surface);
        edit_background_=CreateSolidBrush(pal.search_bg);
        modern_playlist::viewport_style style{row_pixels_,header_pixels_,scale(6),
            pal.row,blend(pal.row,pal.text,4),pal.text,pal.selection,pal.selected_text,pal.highlight};
        style.header_height=show_header_?header_pixels_:0;
        ShowWindow(header_,show_header_?SW_SHOWNA:SW_HIDE);
        style.alternating=core_.alternating; style.group_parity=core_.group_parity; style.extra_line=core_.extra_line;
        style.derived_extra_color=core_.derived_extra_color; style.secondary=pal.muted;
        style.selection_alpha=core_.selection_alpha; style.focus_alpha=core_.focus_alpha;
        style.tooltips=core_.tooltips; style.tooltip_delay=core_.tooltip_delay;
        style.enqueue_default=core_.enqueue_on_double_click;
        modern_playlist::configure_playlist_viewport(list_,style);
        if(close_font_) DeleteObject(close_font_);
        close_font_=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe Fluent Icons");
        update_search_controls(); update_search_visuals();
        measure_tabs();
        // Segoe Fluent Icons is absent on older Windows; MDL2 has the same close glyph.
        HDC icon_dc=GetDC(hwnd_); auto old_icon=SelectObject(icon_dc,close_font_); WORD glyph=0;
        const DWORD glyph_result=GetGlyphIndicesW(icon_dc,L"\uE711",1,&glyph,GGI_MARK_NONEXISTING_GLYPHS);
        SelectObject(icon_dc,old_icon); ReleaseDC(hwnd_,icon_dc);
        if(glyph_result==GDI_ERROR || glyph==0xffff) {
            DeleteObject(close_font_);
            close_font_=CreateFontW(-scale(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe MDL2 Assets");
        }
        SetWindowTheme(list_,dark?L"DarkMode_Explorer":L"Explorer",nullptr);
        SetWindowTheme(search_,dark?L"DarkMode_CFD":nullptr,nullptr);
        SetWindowTheme(header_,L"",L"");
    }
    void measure_tabs() {
        manager_.widths.clear();
        HDC dc=GetDC(hwnd_); auto old=SelectObject(dc,tabs_font_);
        for(const auto& name:tab_names_) { SIZE size{}; GetTextExtentPoint32W(dc,name.c_str(),int(name.size()),&size);
            manager_.widths.push_back(std::clamp(int(size.cx)+scale(40),scale(90),scale(240))); }
        SelectObject(dc,old); ReleaseDC(hwnd_,dc); manager_.clamp();
    }
    bool tab_rect(int index,RECT* rect) const {
        if(index<0 || size_t(index)>=manager_.widths.size()) return false;
        const int x=manager_.left(index); *rect={x,0,x+manager_.widths[index],tab_pixels_}; return true;
    }
    int tab_hit(POINT pt) const { return pt.y>=0 && pt.y<tab_pixels_?manager_.hit(pt.x):-1; }
    bool can_scroll_left() const { return manager_.offset>0; }
    bool can_scroll_right() const { return manager_.offset+manager_.viewport<manager_.total(); }
    void update_arrows() const {
        for(auto button:{tab_left_,tab_right_}) if(button) {
            const bool enabled=button==tab_left_?can_scroll_left():can_scroll_right();
            if((IsWindowEnabled(button)!=FALSE)!=enabled) EnableWindow(button,enabled);
            InvalidateRect(button,nullptr,FALSE);
        }
    }
    void scroll_tabs(int delta) {
        if(!show_tabs_) return;
        manager_.offset+=delta*scale(120); manager_.clamp();
        InvalidateRect(tabs_,nullptr,FALSE); update_arrows();
    }
    void on_tabs_wheel(short delta) {
        if (!show_tabs_ || !tabs_) return;
        tab_wheel_accumulator_ += delta;
        int steps = tab_wheel_accumulator_ / WHEEL_DELTA;
        tab_wheel_accumulator_ %= WHEEL_DELTA;
        if (steps != 0) {
            scroll_tabs(-steps);
        }
    }
    void layout() {
        RECT r{}; GetClientRect(hwnd_,&r);
        const int width=std::max(0,int(r.right)), height=std::max(0,int(r.bottom));
        const int strip=show_tabs_?std::min(height,tab_pixels_+scale(2)):0;
        const int manager_y=manager_bottom_?height-strip:0;
        const int search_y=manager_bottom_?0:strip;
        const int add_w=std::min(width,scale(28)), gap=scale(4), arrow_w=scale(22);
        const bool overflow=manager_.total()+gap+add_w>width;
        const int arrows=overflow?std::min(arrow_w,std::max(0,(width-add_w-3*gap)/2)):0;
        const int tabs_x=arrows?arrows+gap:0;
        const int available=std::max(0,width-add_w-gap-tabs_x-(arrows?arrows+gap:0));
        manager_.viewport=std::min(manager_.total(),available); manager_.clamp();
        // All children have disjoint rectangles, even when the splitter is narrower than +.
        const int add_x=overflow?width-add_w:std::min(width-add_w,tabs_x+manager_.viewport+gap);
        auto position=[](HWND child,int x,int y,int w,int h,bool visible) {
            SetWindowPos(child,nullptr,x,y,std::max(0,w),std::max(0,h),SWP_NOZORDER|SWP_NOACTIVATE|(visible?SWP_SHOWWINDOW:SWP_HIDEWINDOW));
        };
        position(tabs_,tabs_x,manager_y,manager_.viewport,tab_pixels_,show_tabs_);
        position(add_,add_x,manager_y,add_w,tab_pixels_,show_tabs_);
        position(tab_left_,0,manager_y,arrows,tab_pixels_,show_tabs_ && arrows>0);
        position(tab_right_,std::max(0,add_x-gap-arrows),manager_y,arrows,tab_pixels_,show_tabs_ && arrows>0);
        update_arrows();
        const int row_height=text_pixels_+scale(13);
        const int field_width=std::min(scale(112),width/3), scope_width=std::min(scale(150),width/3);
        const int edit_width=std::max(0,width-field_width-scope_width-scale(20));
        position(search_,scale(8),search_y+scale(7),edit_width,text_pixels_,search_settings_.visible);
        position(search_field_,width-field_width-scope_width,search_y+scale(2),field_width,row_height+scale(160),search_settings_.visible);
        position(search_scope_,width-scope_width,search_y+scale(2),scope_width,row_height+scale(120),search_settings_.visible);
        int y=search_y+(search_settings_.visible?row_height:0);
        position(notice_,0,y,width,text_pixels_+scale(3),!notice_text_.empty());
        if(!notice_text_.empty()) y+=text_pixels_+scale(7);
        position(list_,0,y,width,height-y-(manager_bottom_?strip:0),true);
        fit_columns(); InvalidateRect(hwnd_,nullptr,FALSE);
    }
    void notice(const std::wstring& text) {
        if (notice_text_==text) return;
        notice_text_=text; SetWindowTextW(notice_,text.c_str()); layout();
    }
    void toggle_fit_to_window() {
        capture_columns();
        fit_to_window_=!fit_to_window_;
        // Restore preferred widths before switching back to last-column fill.
        fitting_columns_=true;
        stretched_column_=-1;
        for (size_t i=0;i<visible_columns_.size();++i)
            ListView_SetColumnWidth(list_,static_cast<int>(i),scale(columns_[visible_columns_[i]].width));
        fitting_columns_=false;
        fit_columns();
    }
    void toggle_tabs() { show_tabs_=!show_tabs_; layout(); }
    void toggle_header() { show_header_=!show_header_; theme(); layout(); invalidate_all(); }
    COLORREF hover_background() const noexcept {
        const auto& pal=current_palette();
        return RGB((3*GetRValue(pal.header)+GetRValue(pal.selection))/4,
                   (3*GetGValue(pal.header)+GetGValue(pal.selection))/4,
                   (3*GetBValue(pal.header)+GetBValue(pal.selection))/4);
    }
    void update_hover() {
        if (destroying_ || !hwnd_) return;
        POINT pt{};
        GetCursorPos(&pt);
        HWND target = WindowFromPoint(pt);
        int tab=-1;
        if (target==tabs_ && IsWindowEnabled(tabs_)) {
            TCHITTESTINFO hit{}; hit.pt=pt;
            ScreenToClient(tabs_,&hit.pt);
            tab=tab_hit(hit.pt);
        }
        if (tab!=hovered_tab_) {
            hovered_tab_=tab;
            if (tabs_) InvalidateRect(tabs_,nullptr,FALSE);
        }
        const bool add=target==add_ && add_ && IsWindowEnabled(add_);
        if (add!=hovered_add_) {
            hovered_add_=add;
            if (add_) InvalidateRect(add_,nullptr,FALSE);
        }
    }
    void paint_frame() {
        const auto& pal=current_palette(); PAINTSTRUCT ps{}; const auto dc=BeginPaint(hwnd_,&ps);
        RECT r{}; GetClientRect(hwnd_,&r); fill(dc,r,pal.surface);
        const int top=show_tabs_ && !manager_bottom_?tab_pixels_+scale(2):0;
        RECT search_rect{0,top,r.right,top+text_pixels_+scale(13)};
        if(search_settings_.visible) fill(dc,search_rect,pal.search_bg); EndPaint(hwnd_,&ps);
    }
    LRESULT draw_header(NMCUSTOMDRAW* draw) {
        const auto& pal = current_palette();
        if (draw->dwDrawStage==CDDS_PREPAINT) {
            RECT r{}; GetClientRect(header_,&r); fill(draw->hdc,r,pal.header);
            return CDRF_NOTIFYITEMDRAW | CDRF_NOTIFYPOSTPAINT;
        }
        if (draw->dwDrawStage==CDDS_POSTPAINT) {
            // Native headers paint a raised trailing face after the last item.
            // Erase it even during live resizing, before the deferred fit runs.
            HWND header=header_; RECT tail{}; GetClientRect(header,&tail);
            int end=0;
            for (int i=0;i<Header_GetItemCount(header);++i) { RECT item{}; Header_GetItemRect(header,i,&item); end=std::max(end,int(item.right)); }
            tail.left=end; if(tail.left<tail.right) fill(draw->hdc,tail,pal.header);
            return CDRF_DODEFAULT;
        }
        if (draw->dwDrawStage!=CDDS_ITEMPREPAINT) return CDRF_DODEFAULT;
        int logical=static_cast<int>(draw->dwItemSpec);
        if (logical<0 || static_cast<size_t>(logical)>=visible_columns_.size()) return CDRF_SKIPDEFAULT;
        HDC dc=draw->hdc; int saved=SaveDC(dc); RECT r=draw->rc;
        fill(dc,r,pal.header);
        RECT edge{r.right-1,r.top,r.right,r.bottom}; fill(dc,edge,pal.divider);
        SelectObject(dc,bold_font_); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,pal.text);
        int col=visible_columns_[logical]; auto title=wide(columns_[col].title.c_str());
        r.left+=scale(6); r.right-=scale(6);
        const int alignment=headers_follow_alignment_?columns_[col].align:LVCFMT_CENTER;
        const UINT flags=alignment==LVCFMT_RIGHT?DT_RIGHT:alignment==LVCFMT_LEFT?DT_LEFT:DT_CENTER;
        RECT label=r;
        if (sort_column_==col) label.bottom-=scale(5);
        DrawTextW(dc,title.c_str(),static_cast<int>(title.size()),&label,flags|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
        if (sort_column_==col) {
            RECT icon{(r.left+r.right)/2-scale(7),r.bottom-scale(12),(r.left+r.right)/2+scale(7),r.bottom};
            const wchar_t* arrow=sort_direction_>0?L"▴":L"▾";
            DrawTextW(dc,arrow,1,&icon,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX);
        }
        RestoreDC(dc,saved); return CDRF_SKIPDEFAULT;
    }
    void format_cell(size_t row, const column& col, pfc::string_base& text) {
        if (col.ref=="Cover") return;
        if (col.ref=="Index") { text=std::to_string(rows_[row]+1).c_str(); return; }
        if (col.state) {
            if (row>=row_data_.size()) return;
            const auto& state=row_data_[row];
            if (state.playing) text=state.paused?"Paused":"Playing";
            if (!state.queue_positions.empty()) {
                if (text.length()) text << "; "; text << "Queue: ";
                for (size_t i=0;i<state.queue_positions.size();++i) { if(i) text << ", "; text << state.queue_positions[i]; }
            }
            return;
        }
        // The playlist API associates dynamic info with the playing occurrence,
        // including stream title changes and custom playback-related fields.
        if (!pending_ && active_ != pfc::infinite_size) {
            playlist_manager::get()->playlist_item_format_title(active_,rows_[row],
                nullptr,text,col.script,nullptr,play_control::display_level_all);
        } else {
            // A queued structural refresh can leave row indices temporarily stale.
            items_[rows_[row]]->format_title(nullptr,text,col.script,nullptr);
        }
    }
    t_size playing_playlist() const {
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        if (!play_control::get()->is_playing() ||
            !playlist_manager::get()->get_playing_item_location(&playlist,&item))
            return pfc::infinite_size;
        return playlist;
    }
    void draw_speaker(HDC dc, const RECT& rect, COLORREF color) {
        const int x=(rect.left+rect.right)/2, y=(rect.top+rect.bottom)/2;
        const int unit=std::max(1,scale(2));
        const int saved=SaveDC(dc);
        HPEN pen=CreatePen(PS_SOLID,std::max(1,scale(1)),color);
        SelectObject(dc,pen); SelectObject(dc,GetStockObject(DC_BRUSH));
        SetDCBrushColor(dc,color);
        POINT body[]={{x-3*unit,y-unit},{x-2*unit,y-unit},{x,y-3*unit},
                      {x,y+3*unit},{x-2*unit,y+unit},{x-3*unit,y+unit}};
        Polygon(dc,body,6);
        POINT wave[]={{x+unit,y-2*unit},{x+2*unit,y-unit},
                      {x+2*unit,y+unit},{x+unit,y+2*unit}};
        Polyline(dc,wave,4);
        RestoreDC(dc,saved); DeleteObject(pen);
    }
    void repaint_playing_tab() {
        if (!destroying_ && tabs_) InvalidateRect(tabs_,nullptr,FALSE);
    }
    void on_playback_new_track(metadb_handle_ptr) override {
        repaint_playing_tab();
        update_state();
        if (list_) modern_playlist::invalidate_playlist_row(list_,-1);
        // The playlist location can become available after this callback returns.
        if (hwnd_) PostMessageW(hwnd_,state_message,0,0);
    }
    void on_playback_stop(play_control::t_stop_reason) override { repaint_playing_tab(); update_state(); if (list_) modern_playlist::invalidate_playlist_row(list_,-1); }
    void on_playback_pause(bool) override { update_state(); }
    static LRESULT CALLBACK ghost_proc(HWND wnd,UINT msg,WPARAM wp,LPARAM lp) {
        if (msg==WM_NCCREATE)
            SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
        auto* self=reinterpret_cast<playlist_view*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if (msg==WM_NCHITTEST) return HTTRANSPARENT;
        if (msg==WM_PAINT && self) {
            PAINTSTRUCT ps{}; HDC dc=BeginPaint(wnd,&ps); RECT r{}; GetClientRect(wnd,&r);
            const auto& pal=self->current_palette(); fill(dc,r,pal.header);
            SetBkMode(dc,TRANSPARENT); SetTextColor(dc,pal.text); SelectObject(dc,self->tabs_font_);
            const auto label=window_text(wnd);
            InflateRect(&r,-self->scale(6),0);
            DrawTextW(dc,label.c_str(),static_cast<int>(label.size()),&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
            EndPaint(wnd,&ps); return 0;
        }
        return DefWindowProcW(wnd,msg,wp,lp);
    }
    void move_drag_ghost(POINT pointer) {
        if (!drag_ghost_) return;
        RECT tab{}; if (!tab_rect(drag_tab_,&tab)) return;
        POINT screen{pointer.x-drag_grab_.x,pointer.y-drag_grab_.y};
        ClientToScreen(tabs_,&screen);
        SetWindowPos(drag_ghost_,HWND_TOPMOST,screen.x,screen.y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
    }
    void start_drag_ghost(POINT pointer) {
        if (drag_ghost_ || drag_tab_<0) return;
        RECT tab{}; if (!tab_rect(drag_tab_,&tab)) return;
        const auto& label=tab_names_[drag_tab_];
        WNDCLASSW wc{}; wc.lpfnWndProc=ghost_proc; wc.hInstance=core_api::get_my_instance();
        wc.lpszClassName=L"foo_modernplaylist.dragghost"; wc.style=CS_DROPSHADOW;
        RegisterClassW(&wc);
        drag_ghost_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,
            wc.lpszClassName,label.c_str(),WS_POPUP,0,0,tab.right-tab.left,tab.bottom-tab.top,hwnd_,nullptr,wc.hInstance,this);
        if (drag_ghost_) {
            SetLayeredWindowAttributes(drag_ghost_,0,190,LWA_ALPHA);
            move_drag_ghost(pointer);
            ShowWindow(drag_ghost_,SW_SHOWNA);
        }
    }
    void cancel_tab_drag() {
        drag_tab_=-1; drag_tab_moved_=false; drag_before_=-1;
        KillTimer(hwnd_,5); stop_drag_ghost();
        if(GetCapture()==tabs_) ReleaseCapture();
        InvalidateRect(tabs_,nullptr,FALSE);
    }
    void stop_drag_ghost() {
        if (drag_ghost_) { DestroyWindow(drag_ghost_); drag_ghost_=nullptr; }
    }    void paint_tabs() {
        // Paint only indices from the current playlist generation.
        if(pending_) { PAINTSTRUCT ps{}; auto dc=BeginPaint(tabs_,&ps); RECT r{}; GetClientRect(tabs_,&r); fill(dc,r,current_palette().surface); EndPaint(tabs_,&ps); return; }
        update_hover();
        const auto& pal = current_palette();
        PAINTSTRUCT ps{}; HDC paint=BeginPaint(tabs_,&ps); RECT bounds{}; GetClientRect(tabs_,&bounds);
        HDC buffer=CreateCompatibleDC(paint); HBITMAP bitmap=CreateCompatibleBitmap(paint,std::max(1L,bounds.right),std::max(1L,bounds.bottom));
        HGDIOBJ old_bitmap=nullptr;
        HDC dc=paint;
        if(buffer && bitmap) { old_bitmap=SelectObject(buffer,bitmap); dc=buffer; }
        int saved=SaveDC(dc); fill(dc,bounds,pal.surface); SetBkMode(dc,TRANSPARENT); SelectObject(dc,tabs_font_);
        const auto playing=playing_playlist();
        for (int i=0;i<int(tab_names_.size());++i) {
            RECT r{}; tab_rect(i,&r); r.bottom=std::min(r.bottom,bounds.bottom);
            if (r.right <= 0 || r.left >= bounds.right) continue;
            const bool active=size_t(i)==active_;
            SetDCBrushColor(dc,i==hovered_tab_?hover_background():(active?pal.header:pal.row));
            SelectObject(dc,GetStockObject(DC_BRUSH)); SelectObject(dc,GetStockObject(NULL_PEN));
            RoundRect(dc,r.left,r.top,r.right-scale(2),r.bottom,scale(6),scale(6));
            if(active) { RECT line{r.left,r.top,r.right,r.top+scale(2)}; fill(dc,line,pal.highlight); }
            const auto& title=tab_names_[i];
            RECT label=r; label.left+=scale(8); label.right-=scale(20); SetTextColor(dc,pal.text);
            DrawTextW(dc,title.c_str(),static_cast<int>(title.size()),&label,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            RECT close=r; close.left=close.right-scale(20); SetTextColor(dc,pal.muted);
            if (static_cast<t_size>(i)==playing) draw_speaker(dc,close,pal.text);
            else if(!modern_playlist::special_reserved(i) && !(playlist_manager::get()->playlist_lock_get_filter_mask(i)&playlist_lock::filter_remove_playlist)) {
                auto old=SelectObject(dc,close_font_); DrawTextW(dc,L"\uE711",1,&close,DT_SINGLELINE|DT_VCENTER|DT_CENTER); SelectObject(dc,old);
            }
            if(active && GetFocus()==tabs_) { RECT focus=r; InflateRect(&focus,-scale(3),-scale(3)); DrawFocusRect(dc,&focus); }
        }
        if(drag_tab_moved_ && drag_before_>=0) {
            int x=manager_.left(drag_before_); x=std::clamp(x,0,std::max(0,manager_.viewport-scale(2)));
            RECT marker{x,0,x+scale(2),tab_pixels_}; fill(dc,marker,pal.highlight);
        }
        RestoreDC(dc,saved);
        if(dc==buffer) { BitBlt(paint,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY); SelectObject(buffer,old_bitmap); }
        if(bitmap) DeleteObject(bitmap); if(buffer) DeleteDC(buffer);
        EndPaint(tabs_,&ps);
        update_arrows();
    }
    void schedule() { search_reveal_=pfc::infinite_size; ++content_epoch_; if (hwnd_ && !destroying_ && !pending_) { pending_ = true; modern_playlist::suspend_playlist_input(list_,true); PostMessageW(hwnd_,refresh_message,0,0); } }
    void refresh() {
        if (destroying_ || !list_) return;
        pending_ = false;
        auto pm = playlist_manager::get(); auto next = pm->get_active_playlist();
        queries_.resize(pm->get_playlist_count());
        const bool switched = next != active_;
        load_playlist_columns(next);
        active_ = next;
        if(switched) { collapsed_.clear(); clear_incremental(); search_reveal_=pfc::infinite_size; }
        bool group_sort_needed=false;
        if(grouping_.playlist_filter && (switched || apply_filter_next_) && active_<pm->get_playlist_count()) {
            pfc::string8 name; pm->playlist_get_name(active_,name);
            const auto pattern=modern_playlist::matching_pattern(grouping_.patterns,name.c_str(),grouping_.pattern);
            if(pattern!=grouping_.pattern) { grouping_.pattern=static_cast<unsigned>(pattern); collapsed_.clear(); compile_columns(); group_sort_needed=true; }
        }
        apply_filter_next_=false;
        rebuilding_ = true;
        // Track metadata and selection updates must not recreate the tab strip.
        // Rebuilding resets its scroll position and exposes intermediate paints.
        if(tabs_dirty_) {
            tab_names_.clear();
            for(t_size i=0;i<pm->get_playlist_count();++i) { pfc::string8 name; pm->playlist_get_name(i,name); tab_names_.push_back(wide(name.c_str())); }
            measure_tabs(); tabs_dirty_=false;
        }
        InvalidateRect(tabs_,nullptr,FALSE);
        if (switched && !search_settings_.scope) {
            KillTimer(hwnd_,search_timer);
            SetWindowTextW(search_,active_ < queries_.size() ? queries_[active_].c_str() : L"");
            applied_query_=window_text(search_);
        }
        if (switched) modern_playlist::reset_playlist_scroll(list_);
        rows_.clear(); items_.remove_all();
        std::wstring error;
        if (active_ < pm->get_playlist_count()) {
            pm->playlist_get_all_items(active_,items_);
            if(search_settings_.scope || search_settings_.locate || applied_query_.empty()) {
                for(t_size i=0;i<items_.get_count();++i) rows_.push_back(i);
            } else try {
                const auto matches=search_items(items_,applied_query_);
                for(t_size i=0;i<matches.size();++i) if(matches[i]) rows_.push_back(i);
            } catch(const std::exception& e) { error=L"Search: "+wide(e.what()); }
        }
        filtered_rows_=rows_;
        build_rows();
        SendMessageW(list_,WM_SETREDRAW,FALSE,0);
        ListView_SetItemCountEx(list_,static_cast<int>(rows_.size()),LVSICF_NOSCROLL);
        modern_playlist::set_playlist_groups(list_,groups_);
        ListView_SetItemState(list_,-1,0,LVIS_SELECTED | LVIS_FOCUSED);
        if (active_ < pm->get_playlist_count()) {
            bit_array_bittable selected(items_.get_count()); pm->playlist_get_selection_mask(active_,selected);
            auto focus = pm->playlist_get_focus_item(active_);
            for (size_t i=0;i<rows_.size();++i) ListView_SetItemState(list_,static_cast<int>(i),
                (selected[rows_[i]] ? LVIS_SELECTED : 0) | (focus == rows_[i] ? LVIS_FOCUSED : 0),LVIS_SELECTED | LVIS_FOCUSED);
        }

        SendMessageW(list_,WM_SETREDRAW,TRUE,0); InvalidateRect(list_,nullptr,TRUE);
        highlight_terms_=error.empty()?box_highlights():std::vector<std::wstring>{};
        update_search_visuals();
        notice(error);
        layout();
        if(switched) { NotifyWinEvent(EVENT_OBJECT_SELECTION,tabs_,OBJID_CLIENT,active_==pfc::infinite_size?CHILDID_SELF:LONG(active_+1)); manager_.reveal(active_==pfc::infinite_size?-1:int(active_)); InvalidateRect(tabs_,nullptr,FALSE); update_arrows(); }
        EnableWindow(search_,search_settings_.scope || active_ < pm->get_playlist_count()); rebuilding_ = false;
        modern_playlist::suspend_playlist_input(list_,false);
        update_state();
        if (switched) show_now_playing(false);
        if(group_sort_needed && grouping_.enabled) apply_group_sort();
    }
    void update_search_controls() {
        SendMessageW(search_field_,CB_SETCURSEL,search_settings_.field,0);
        SendMessageW(search_scope_,CB_SETCURSEL,search_settings_.scope,0);
        const auto theme_name=is_dark_mode()?L"DarkMode_CFD":L"Explorer";
        SetWindowTheme(search_field_,theme_name,nullptr); SetWindowTheme(search_scope_,theme_name,nullptr);
    }
    std::vector<std::wstring> box_highlights() const {
        if(search_settings_.field) return applied_query_.empty()?std::vector<std::wstring>{}:std::vector<std::wstring>{applied_query_};
        return modern_playlist::literal_search_terms(applied_query_);
    }
    void update_search_visuals(bool found=true) {
        modern_playlist::viewport_search state;
        state.terms=incremental_.text.empty()?highlight_terms_:std::vector<std::wstring>{incremental_.text};
        state.overlay=incremental_.text; state.found=found; state.color=search_settings_.color;
        modern_playlist::set_playlist_search(list_,state);
    }
    void clear_incremental() {
        KillTimer(hwnd_,incremental_timer); incremental_.clear(); update_search_visuals();
    }
    void toggle_search() {
        search_settings_.visible=!search_settings_.visible;
        if(!search_settings_.visible && (GetFocus()==search_ || GetFocus()==search_field_ || GetFocus()==search_scope_)) SetFocus(list_);
        layout();
    }
    std::vector<bool> search_items(metadb_handle_list_cref items,const std::wstring& query) {
        std::vector<bool> result(items.get_count(),true);
        if(query.empty()) return result;
        if(!search_settings_.field) {
            auto filter=search_filter_manager::get()->create(utf8(query).c_str());
            std::unique_ptr<bool[]> matches(new bool[items.get_count()]); filter->test_multi(items,matches.get());
            for(size_t i=0;i<result.size();++i) result[i]=matches[i];
        } else {
            const char* patterns[]={"","[%artist%]","[%title%]","[%album%]"};
            titleformat_object::ptr script; titleformat_compiler::get()->compile_safe(script,patterns[search_settings_.field]);
            for(size_t i=0;i<result.size();++i) {
                pfc::string8 text; items[i]->format_title(nullptr,text,script,nullptr);
                result[i]=!modern_playlist::search_matches(wide(text.c_str()),{query}).empty();
            }
        }
        return result;
    }
    bool locate_track(t_size track) {
        if(pending_ || track>=items_.get_count()) return false;
        search_reveal_=track;
        auto row=std::lower_bound(rows_.begin(),rows_.end(),track);
        if(row==rows_.end() || *row!=track) {
            // A search reveal temporarily overrides both manual and automatic collapse.
            refresh(); row=std::lower_bound(rows_.begin(),rows_.end(),track);
        }
        if(row==rows_.end() || *row!=track) return false;
        const int index=int(row-rows_.begin());
        rebuilding_=true;
        ListView_SetItemState(list_,-1,0,LVIS_SELECTED|LVIS_FOCUSED);
        ListView_SetItemState(list_,index,LVIS_SELECTED|LVIS_FOCUSED,LVIS_SELECTED|LVIS_FOCUSED);
        rebuilding_=false; select_view(); ListView_EnsureVisible(list_,index,FALSE); return true;
    }
    void incremental_input(wchar_t ch) {
        if(pending_ || dragging_tracks_) return;
        const auto now=GetTickCount64();
        // Space retains its selection action until a typing search is in progress.
        if(ch==L' ' && (incremental_.text.empty() || incremental_.expired(now))) return;
        incremental_.input(ch,now);
        if(incremental_.text.empty()) { clear_incremental(); return; }
        // Wait for the low surrogate instead of searching half a UTF-16 character.
        const bool high=ch>=0xd800 && ch<=0xdbff;
        bool found=false;
        if(!high) for(auto track:filtered_rows_) {
            pfc::string8 value; items_[track]->format_title(nullptr,value,search_settings_.group_key?group_script_:artist_search_,nullptr);
            if(!modern_playlist::search_matches(wide(value.c_str()),{incremental_.text}).empty()) { found=locate_track(track); break; }
        }
        update_search_visuals(found);
        SetTimer(hwnd_,incremental_timer,modern_playlist::incremental_idle_ms,nullptr);
    }
    void apply_search() {
        if(destroying_) return;
        clear_incremental(); search_reveal_=pfc::infinite_size; applied_query_=window_text(search_);
        if(search_settings_.scope) {
            // Empty library queries do not copy the entire library or erase the last results.
            if(applied_query_.empty()) { highlight_terms_.clear(); refresh(); return; }
            try {
                metadb_handle_list library,results; library_manager::get()->get_all_items(library);
                const auto matches=search_items(library,applied_query_);
                for(size_t i=0;i<matches.size();++i) if(matches[i]) results.add_item(library[i]);
                auto pm=playlist_manager::get();
                auto target=pm->find_playlist("Media Library Search");
                if(target!=pfc::infinite_size && (!playlist_allows(target,playlist_lock::filter_add|playlist_lock::filter_remove) || modern_playlist::special_reserved(target)))
                    throw std::runtime_error("Media Library Search is locked; its contents were left unchanged.");
                if(target==pfc::infinite_size) target=pm->create_playlist("Media Library Search",pfc::infinite_size,pfc::infinite_size);
                if(target==pfc::infinite_size) throw std::runtime_error("Cannot create Media Library Search.");
                pm->playlist_undo_backup(target);
                pm->playlist_remove_items(target,bit_array_true());
                pm->playlist_insert_items(target,pfc::infinite_size,results,bit_array_false());
                pm->set_active_playlist(target); refresh();
                notice(std::to_wstring(results.get_count())+L" library matches");
            } catch(const std::exception& e) { highlight_terms_.clear(); update_search_visuals(); notice(L"Search: "+wide(e.what())); }
            return;
        }
        refresh();
        if(search_settings_.locate && !applied_query_.empty()) try {
            const auto matches=search_items(items_,applied_query_);
            auto match=std::find(matches.begin(),matches.end(),true);
            if(match!=matches.end()) locate_track(size_t(match-matches.begin())); else notice(L"Search: no matching tracks");
        } catch(const std::exception& e) { highlight_terms_.clear(); update_search_visuals(); notice(L"Search: "+wide(e.what())); }
    }
    void append_search_menu(HMENU menu) {
        HMENU search=CreatePopupMenu();
        AppendMenuW(search,MF_STRING|(search_settings_.visible?MF_CHECKED:0),500,L"Show search row\tMiddle-click");
        AppendMenuW(search,MF_STRING|(search_settings_.group_key?0:MF_CHECKED),501,L"Typing searches artist");
        AppendMenuW(search,MF_STRING|(search_settings_.group_key?MF_CHECKED:0),502,L"Typing searches group key");
        AppendMenuW(search,MF_STRING|(search_settings_.locate?0:MF_CHECKED),503,L"Search box filters playlist");
        AppendMenuW(search,MF_STRING|(search_settings_.locate?MF_CHECKED:0),504,L"Search box locates tracks");
        AppendMenuW(search,MF_STRING,505,L"Highlight color…");
        AppendMenuW(search,MF_STRING,506,L"Reset highlight color");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(search),L"Search");
    }
    bool search_command(int command) {
        if(command<500 || command>506) return false;
        if(command==500) toggle_search();
        if(command==501 || command==502) { search_settings_.group_key=command==502; clear_incremental(); }
        if(command==503 || command==504) { search_settings_.locate=command==504; if(!search_settings_.scope) apply_search(); }
        if(command==505) {
            static COLORREF custom[16]{};
            CHOOSECOLORW choose{sizeof(choose)}; choose.hwndOwner=hwnd_; choose.rgbResult=search_settings_.color;
            choose.lpCustColors=custom; choose.Flags=CC_FULLOPEN|CC_RGBINIT;
            if(ChooseColorW(&choose)) search_settings_.color=choose.rgbResult;
        }
        if(command==506) search_settings_.color=modern_playlist::search_settings{}.color;
        update_search_visuals(); return true;
    }
    void clear_search() {
        if (destroying_ || !search_) return;
        KillTimer(hwnd_, search_timer);
        rebuilding_=true; SetWindowTextW(search_, L""); rebuilding_=false;
        applied_query_.clear(); search_reveal_=pfc::infinite_size; clear_incremental();
        if (!search_settings_.scope && active_ < queries_.size()) queries_[active_].clear();
        InvalidateRect(search_, nullptr, TRUE);
        refresh();
        if (list_) SetFocus(list_);
    }
    void select_view() {
        if (rebuilding_ || pending_ || active_ == pfc::infinite_size) return;
        auto pm = playlist_manager::get();
        bit_array_bittable affected(items_.get_count()), selected(items_.get_count());
        for (size_t i=0;i<rows_.size();++i) { affected.set(rows_[i],true); selected.set(rows_[i],(ListView_GetItemState(list_,static_cast<int>(i),LVIS_SELECTED) & LVIS_SELECTED) != 0); }
        // Filtered-out selection is intentionally untouched.
        rebuilding_ = true;
        pm->playlist_set_selection(active_,affected,selected);
        int focus = ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (focus >= 0 && static_cast<size_t>(focus) < rows_.size()) pm->playlist_set_focus_item(active_,rows_[focus]);
        rebuilding_ = false;
    }
    std::vector<t_size> selected_rows() const {
        std::vector<t_size> result;
        for (int i=ListView_GetNextItem(list_,-1,LVNI_SELECTED); i >= 0; i=ListView_GetNextItem(list_,i,LVNI_SELECTED))
            if (static_cast<size_t>(i) < rows_.size()) result.push_back(rows_[i]);
        return result;
    }
    void play() {
        int row = ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (!pending_ && row >= 0 && static_cast<size_t>(row)<rows_.size()) playlist_manager::get()->playlist_execute_default_action(active_,rows_[row]);
    }
    void remove_tracks() {
        if (pending_ || !playlist_allows(active_,playlist_lock::filter_remove)) return;
        auto selected = selected_rows(); if (selected.empty()) return;
        bit_array_bittable mask(items_.get_count());
        for (auto i:selected) mask.set(i,true);
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_remove_items(active_,mask);
    }
    void copy_tracks(bool cut) {
        if (pending_ || (cut && !playlist_allows(active_,playlist_lock::filter_remove))) return;
        metadb_handle_list selected; for (auto i : selected_rows()) selected.add_item(items_[i]);
        if (!selected.get_count()) return;
        auto object = ole_interaction::get()->create_dataobject(selected);
        if (SUCCEEDED(OleSetClipboard(object.get_ptr()))) {
            OleFlushClipboard();
            if (cut) remove_tracks();
        }
    }
    void paste_tracks() {
        if (pending_ || !playlist_allows(active_,playlist_lock::filter_add)) return;
        pfc::com_ptr_t<IDataObject> object;
        if (FAILED(OleGetClipboard(object.receive_ptr()))) return;
        metadb_handle_list incoming;
        if (FAILED(ole_interaction::get()->parse_dataobject_immediate(object,incoming))) {
            notice(L"Paste tracks copied from foobar2000. Use Add files for other files."); return;
        }
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_);
        pm->playlist_insert_items(active_,pfc::infinite_size,incoming,bit_array_true());
    }
    void move_tracks(int direction) {
        if (pending_ || rows_.empty() || !playlist_allows(active_,playlist_lock::filter_reorder)) return;
        auto selected = selected_rows();
        std::vector<bool> mask(rows_.size());
        for (size_t i=0;i<rows_.size();++i) mask[i]=std::find(selected.begin(),selected.end(),rows_[i])!=selected.end();
        auto sorted=modern_playlist::nudge_order(mask,direction);
        auto order=modern_playlist::visible_order(rows_,sorted,items_.get_count());
        auto pm=playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size());
    }
    void drop_tracks(int row) {
        if (pending_ || row < 0 || static_cast<size_t>(row)>rows_.size() || !playlist_allows(active_,playlist_lock::filter_reorder)) return;
        auto selected=selected_rows(); std::vector<bool> mask(rows_.size());
        for(size_t i=0;i<rows_.size();++i) mask[i]=std::find(selected.begin(),selected.end(),rows_[i])!=selected.end();
        auto sorted=modern_playlist::drop_order(mask,static_cast<size_t>(row));
        auto order=modern_playlist::visible_order(rows_,sorted,items_.get_count());
        auto pm=playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size());
    }
    void track_menu(POINT pt) {
        if (pending_) return;
        HMENU menu=CreatePopupMenu();

        auto selected=selected_rows();
        const auto epoch=content_epoch_;
        if (!selected.empty()) {
            AppendMenuW(menu,MF_STRING,4,L"Play"); // Queue action is supplied once by the native context menu.
            AppendMenuW(menu,MF_STRING,6,L"Copy\tCtrl+C"); AppendMenuW(menu,MF_STRING|(playlist_allows(active_,playlist_lock::filter_remove)?0:MF_GRAYED),7,L"Remove\tDelete");
        }
        AppendMenuW(menu,MF_STRING|(playlist_allows(active_,playlist_lock::filter_add)?0:MF_GRAYED),8,L"Paste\tCtrl+V");
        AppendMenuW(menu,MF_STRING|(show_header_?MF_CHECKED:0),9,L"Show column headers	Ctrl+T");

        append_groups_menu(menu); append_search_menu(menu);
        contextmenu_manager::ptr context;
        if (!selected.empty()) {
            metadb_handle_list handles; for (auto i:selected) handles.add_item(items_[i]);
            context=contextmenu_manager::g_create(); context->init_context(handles,0);
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr); context->win32_build_menu(menu,1000,0x7000);
        }
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr); DestroyMenu(menu);
        if (content_epoch_ != epoch) return;
        if(search_command(command) || group_command(command)) return;
        switch(command) {

        case 4: play(); break;

        case 6: copy_tracks(false); break;
        case 7: remove_tracks(); break;
        case 8: paste_tracks(); break;
        case 9: toggle_header(); break;

        default: if (command>=1000 && context.is_valid()) context->execute_by_id(command-1000); break;
        }
    }
    void sort(int logical) {
        if (!playlist_allows(active_,playlist_lock::filter_reorder) || pending_ || logical < 0 || static_cast<size_t>(logical)>=visible_columns_.size() || filtered_rows_.empty()) return;
        int col = visible_columns_[logical]; if (columns_[col].state || columns_[col].ref=="Cover" || columns_[col].ref=="Index") return; sort_direction_ = sort_column_ == col ? -sort_direction_ : 1; sort_column_ = col;
        metadb_handle_list visible; for (auto i: filtered_rows_) visible.add_item(items_[i]);
        std::vector<t_size> sorted(filtered_rows_.size());
        metadb_handle_list_helper::sort_by_format_get_order(visible,sorted.data(),columns_[col].sort_script,nullptr,sort_direction_);
        auto order = modern_playlist::visible_order(filtered_rows_,sorted,items_.get_count());
        auto pm = playlist_manager::get(); pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size());
        InvalidateRect(header_,nullptr,TRUE);
    }
    void new_playlist() {
        auto pm = playlist_manager::get();
        const auto index = pm->create_playlist_autoname();
        if (index != pfc::infinite_size) pm->set_active_playlist(index);
    }
    bool playlist_allows(t_size index,t_uint32 operation) const {
        auto pm=playlist_manager::get();
        return index<pm->get_playlist_count() && !(pm->playlist_lock_get_filter_mask(index)&operation);
    }
    void rename_playlist(t_size index) {
        auto pm=playlist_manager_v5::get();
        if(!playlist_allows(index,playlist_lock::filter_rename) || modern_playlist::special_reserved(index)) return;
        const GUID id=pm->playlist_get_guid(index);
        pfc::string8 name; pm->playlist_get_name(index,name); dialog_data data; data.value=wide(name.c_str());
        if(DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_TEXT),hwnd_,dialog_proc,reinterpret_cast<LPARAM>(&data))!=IDOK) return;
        index=pm->find_playlist_by_guid(id);
        if(!playlist_allows(index,playlist_lock::filter_rename) || modern_playlist::special_reserved(index)) return;
        const auto text=utf8(data.value); if(!text.empty()) pm->playlist_rename(index,text.c_str(),text.size());
    }
    void create_autoplaylist(const autoplaylist_data& data,t_size before) {
        auto pm=playlist_manager_v5::get();
        // Validate before creating anything, including predefined queries.
        search_filter_manager::get()->create(data.query.c_str());
        if(before==0 && modern_playlist::library_pinned(0)) before=1;
        const auto index=pm->create_playlist(data.name.c_str(),data.name.size(),before);
        if(index==pfc::infinite_size) return;
        try { autoplaylist_manager::get()->add_client_simple(data.query.c_str(),data.sort.c_str(),index,data.force?autoplaylist_flag_sort:0); }
        catch(...) { pm->remove_playlist(index); throw; }
        pm->set_active_playlist(index);
    }
    void tab_menu(POINT pt) {
        if(pending_) refresh();
        POINT local=pt; ScreenToClient(tabs_,&local);
        auto pm=playlist_manager_v5::get();
        int index=tab_hit(local);
        const GUID target=index>=0?pm->playlist_get_guid(index):GUID{};
        const bool automatic=index>=0 && autoplaylist_manager::get()->is_client_present(index);
        const bool reserved=index>=0 && modern_playlist::special_reserved(index);
        auto flags=[&](t_uint32 mask,bool normal=false) -> UINT {
            return MF_STRING|((index<0 || !playlist_allows(index,mask) || (normal && reserved))?MF_GRAYED:0);
        };
        HMENU menu=CreatePopupMenu(),create=CreatePopupMenu(),presets=CreatePopupMenu(),special=CreatePopupMenu();
        AppendMenuW(create,MF_STRING,1,L"New Playlist\tCtrl+N");
        AppendMenuW(create,MF_STRING,6,L"New Autoplaylist...");
        const char* names[]={"Tracks never played","Tracks played in the last 5 days","Tracks unrated","Tracks rated 3 to 5","Tracks rated 4","Tracks rated 5","Loved Tracks"};
        const char* queries[]={"%play_count% MISSING OR %play_count% IS 0","%last_played% DURING LAST 5 DAYS","%rating% MISSING OR %rating% IS 0","%rating% GREATER 2 AND %rating% LESS 6","%rating% IS 4","%rating% IS 5","%mood% GREATER 0"};
        for(int i=0;i<7;++i) AppendMenuW(presets,MF_STRING,100+i,wide(names[i]).c_str());
        AppendMenuW(create,MF_POPUP,reinterpret_cast<UINT_PTR>(presets),L"Pre-defined Autoplaylist");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(create),index>=0?L"Insert...":L"Add...");
        AppendMenuW(menu,MF_STRING,7,L"Load a Playlist...");
        if(index>=0) {
            AppendMenuW(menu,MF_STRING,8,L"Save this Playlist...");
            AppendMenuW(menu,MF_STRING,9,L"Duplicate");
            AppendMenuW(menu,flags(playlist_lock::filter_rename,true),2,L"Rename...\tF2");
            AppendMenuW(menu,flags(playlist_lock::filter_remove_playlist,true),3,L"Remove");
            const bool pin=modern_playlist::library_pinned(index);
            AppendMenuW(menu,MF_STRING|((pin || index==0 || modern_playlist::library_pinned(index-1))?MF_GRAYED:0),4,L"Move left");
            AppendMenuW(menu,MF_STRING|((pin || size_t(index+1)>=pm->get_playlist_count())?MF_GRAYED:0),5,L"Move right");
            if(automatic) {
                auto client=autoplaylist_manager::get()->query_client(index);
                autoplaylist_client_v2::ptr v2; const bool supported=!client->service_query_t(v2) || v2->show_ui_available();
                AppendMenuW(menu,MF_STRING|((!supported || reserved)?MF_GRAYED:0),10,L"Autoplaylist properties...");
            }
            AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
            AppendMenuW(menu,flags(playlist_lock::filter_add),11,L"Add files...");
            AppendMenuW(menu,flags(playlist_lock::filter_add),12,L"Add folder...");
        }
        using kind=modern_playlist::special_playlist;
        AppendMenuW(special,MF_STRING|(modern_playlist::special_enabled(kind::library)?MF_CHECKED:0),20,L"Media Library (first playlist)");
        AppendMenuW(special,MF_STRING|(modern_playlist::special_enabled(kind::history)?MF_CHECKED:0),21,L"Historic (played tracks)");
        AppendMenuW(special,MF_STRING|(modern_playlist::special_enabled(kind::queue)?MF_CHECKED:0),22,L"Queue Content (read-only)");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(special),L"Special playlists");
        AppendMenuW(menu,MF_STRING,23,L"Sort playlists by name A-Z");
        AppendMenuW(menu,MF_STRING,24,L"Sort playlists by name Z-A");
        const int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr); DestroyMenu(menu);
        if(!command) return;
        const auto resolved=index>=0?pm->find_playlist_by_guid(target):pfc::infinite_size;
        if(command>=20 && command<=22) { modern_playlist::toggle_special(command==20?kind::library:command==21?kind::history:kind::queue); return; }
        if(command==23 || command==24) {
            std::vector<std::wstring> names;
            for(t_size i=0;i<pm->get_playlist_count();++i) { pfc::string8 name; pm->playlist_get_name(i,name); names.push_back(wide(name.c_str())); }
            const auto order=modern_playlist::manager_name_order(names.size(),modern_playlist::library_pinned(0),[&](size_t a,size_t b) {
                const int comparison=CompareStringOrdinal(names[a].c_str(),-1,names[b].c_str(),-1,TRUE);
                return comparison==(command==23?CSTR_LESS_THAN:CSTR_GREATER_THAN);
            });
            pm->reorder(order.data(),order.size()); return;
        }
        if(command==7) { standard_commands::main_load_playlist(); return; }
        if(index>=0 && resolved==pfc::infinite_size) return;
        auto before=resolved;
        if(command==1) {
            if(before==0 && modern_playlist::library_pinned(0)) before=1;
            auto created=pm->create_playlist_autoname(before); if(created!=pfc::infinite_size) pm->set_active_playlist(created); return;
        }
        if(command==6 || (command>=100 && command<107)) {
            autoplaylist_data data;
            if(command>=100) { data.name=names[command-100]; data.query=queries[command-100]; if(command==101) data.sort="%last_played%"; }
            else if(DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_AUTOPLAYLIST),hwnd_,autoplaylist_dialog,reinterpret_cast<LPARAM>(&data))!=IDOK) return;
            before=index>=0?pm->find_playlist_by_guid(target):pfc::infinite_size;
            if(index>=0 && before==pfc::infinite_size) return;
            create_autoplaylist(data,before); return;
        }
        if(resolved==pfc::infinite_size) return;
        if(command==2) rename_playlist(resolved);
        if(command==3 && !modern_playlist::special_reserved(resolved) && playlist_allows(resolved,playlist_lock::filter_remove_playlist)) pm->remove_playlist_user(resolved);
        if((command==4 || command==5) && !modern_playlist::library_pinned(resolved)) {
            const auto to=command==4?(resolved?resolved-1:resolved):std::min(resolved+1,pm->get_playlist_count()-1);
            if(!modern_playlist::library_pinned(to)) { auto order=modern_playlist::move_order(pm->get_playlist_count(),resolved,to); pm->reorder(order.data(),order.size()); }
        }
        if(command==9) {
            metadb_handle_list tracks; pm->playlist_get_all_items(resolved,tracks);
            pfc::string8 name; pm->playlist_get_name(resolved,name); name << " (copy)";
            const auto copy=pm->create_playlist(name.c_str(),name.length(),resolved+1);
            if(copy!=pfc::infinite_size) { pm->playlist_insert_items(copy,0,tracks,bit_array_false()); pm->set_active_playlist(copy); }
        }
        if(command==10 && !modern_playlist::special_reserved(resolved) && autoplaylist_manager::get()->is_client_present(resolved))
            autoplaylist_manager::get()->query_client(resolved)->show_ui(resolved);
        if(command==8 || ((command==11 || command==12) && playlist_allows(resolved,playlist_lock::filter_add))) {
            // Host commands address the active playlist; activate the actual menu target first.
            pm->set_active_playlist(resolved);
            if(command==8) standard_commands::main_save_playlist();
            if(command==11) standard_commands::main_add_files();
            if(command==12) standard_commands::main_add_directory();
        }
    }
    void append_groups_menu(HMENU menu) {
        HMENU groups=CreatePopupMenu(), patterns=CreatePopupMenu();
        AppendMenuW(groups,MF_STRING|(grouping_.enabled?MF_CHECKED:0),300,L"Enable Groups");
        AppendMenuW(groups,MF_STRING|(grouping_.playlist_filter?MF_CHECKED:0),301,L"Enable Playlist Filter");
        for(size_t i=0;i<grouping_.patterns.size();++i)
            AppendMenuW(patterns,MF_STRING|(i==grouping_.pattern?MF_CHECKED:0),400+i,wide(grouping_.patterns[i].label.c_str()).c_str());
        AppendMenuW(groups,MF_POPUP,reinterpret_cast<UINT_PTR>(patterns),L"Change Group Pattern");
        AppendMenuW(groups,MF_STRING|(playlist_allows(active_,playlist_lock::filter_reorder)?0:MF_GRAYED),302,L"Apply Group Sorting");
        AppendMenuW(groups,MF_STRING,303,L"Collapse All"); AppendMenuW(groups,MF_STRING,304,L"Expand All");
        AppendMenuW(groups,MF_STRING|(grouping_.collapse_default?MF_CHECKED:0),305,L"Collapse groups by default");
        AppendMenuW(groups,MF_STRING|(grouping_.autocollapse?MF_CHECKED:0),306,L"Auto-collapse to playing group");
        AppendMenuW(groups,MF_SEPARATOR,0,nullptr);
        AppendMenuW(groups,MF_STRING,307,L"Edit current group pattern...");
        AppendMenuW(groups,MF_STRING|(grouping_.patterns.size()>=64?MF_GRAYED:0),308,L"Add group pattern...");
        AppendMenuW(groups,MF_STRING|(grouping_.patterns.size()<=1?MF_GRAYED:0),309,L"Delete current group pattern");
        AppendMenuW(groups,MF_STRING,310,L"Use current pattern for this playlist");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(groups),L"Groups");
    }
    void apply_group_sort() {
        if(pending_ || filtered_rows_.empty() || grouping_.patterns[grouping_.pattern].sort_order.empty()) return;
        auto pm=playlist_manager::get();
        if(pm->playlist_lock_get_filter_mask(active_)&playlist_lock::filter_reorder) { notice(L"This playlist does not allow reordering."); return; }
        metadb_handle_list tracks; for(auto i:filtered_rows_) tracks.add_item(items_[i]);
        std::vector<t_size> sorted(filtered_rows_.size());
        metadb_handle_list_helper::sort_by_format_get_order(tracks,sorted.data(),group_sort_,nullptr,1);
        auto order=modern_playlist::visible_order(filtered_rows_,sorted,items_.get_count());
        bool changed=false; for(size_t i=0;i<order.size();++i) if(order[i]!=i) { changed=true; break; }
        sort_column_=-1; InvalidateRect(header_,nullptr,FALSE);
        if(changed) { pm->playlist_undo_backup(active_); pm->playlist_reorder_items(active_,order.data(),order.size()); }
    }
    bool group_command(int command) {
        if(command<300 || command>=464) return false;
        search_reveal_=pfc::infinite_size;
        if(command>=400) {
            if(size_t(command-400)>=grouping_.patterns.size()) return true;
            grouping_.pattern=command-400; grouping_.enabled=true;
            // Manual selection stays in effect until a subsequent playlist switch.
            collapsed_.clear(); compile_columns();
            const bool filter=grouping_.playlist_filter; grouping_.playlist_filter=false; refresh(); grouping_.playlist_filter=filter;
            apply_group_sort(); return true;
        }
        if(command==300) grouping_.enabled=!grouping_.enabled;
        else if(command==301) { grouping_.playlist_filter=!grouping_.playlist_filter; apply_filter_next_=true; }
        else if(command==302) { apply_group_sort(); return true; }
        else if(command==303 || command==304) {
            grouping_.autocollapse=false;
            for(const auto& id:group_ids_) collapsed_[id]=command==303;
        } else if(command==305) { grouping_.collapse_default=!grouping_.collapse_default; collapsed_.clear(); }
        else if(command==306) { grouping_.autocollapse=!grouping_.autocollapse; collapsed_.clear(); }
        else if(command==307 || command==308) {
            if(command==308 && grouping_.patterns.size()>=64) return true;
            group_dialog_data edited{grouping_.patterns[grouping_.pattern],grouping_.minimum_rows,grouping_.extra_rows};
            if(command==308) { edited.pattern.label="New pattern"; edited.pattern.playlist_filter=""; }
            const auto epoch=playlist_epoch_;
            if(DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_GROUP),hwnd_,group_dialog,reinterpret_cast<LPARAM>(&edited))!=IDOK) return true;
            if(pending_ || epoch!=playlist_epoch_) return true;
            if(command==308) { grouping_.patterns.push_back(edited.pattern); grouping_.pattern=unsigned(grouping_.patterns.size()-1); }
            else grouping_.patterns[grouping_.pattern]=edited.pattern;
            grouping_.minimum_rows=edited.minimum; grouping_.extra_rows=edited.extra;
            collapsed_.clear(); compile_columns(); refresh(); apply_group_sort(); return true;
        } else if(command==309) {
            if(grouping_.patterns.size()<=1) return true;
            grouping_.patterns.erase(grouping_.patterns.begin()+grouping_.pattern); grouping_.pattern=0; collapsed_.clear(); compile_columns(); refresh(); apply_group_sort(); return true;
        } else if(command==310) {
            pfc::string8 name; playlist_manager::get()->playlist_get_name(active_,name);
            if(strchr(name.c_str(),';') || name=="*") { notice(L"Playlist filter names cannot contain a semicolon or be named *."); return true; }
            for(auto& pattern:grouping_.patterns) {
                std::string filter;
                for(const auto& part:modern_playlist::filter_names(pattern.playlist_filter)) if(part!=name.c_str()) { if(!filter.empty()) filter+=";"; filter+=part; }
                pattern.playlist_filter=filter;
            }
            auto& filter=grouping_.patterns[grouping_.pattern].playlist_filter;
            if(!filter.empty()) filter+=";"; filter+=name.c_str(); grouping_.playlist_filter=true;
        } else return false;
        refresh(); return true;
    }
    void column_menu(POINT pt) {
        if (pending_) refresh();
        const auto menu_id=layout_id_;
        const auto epoch=playlist_epoch_;
        capture_columns();
        POINT local=pt; ScreenToClient(header_,&local);
        HDHITTESTINFO hit{}; hit.pt=local;
        const int logical=static_cast<int>(SendMessageW(header_,HDM_HITTEST,0,reinterpret_cast<LPARAM>(&hit)));
        const int col=logical>=0 && static_cast<size_t>(logical)<visible_columns_.size()?visible_columns_[logical]:-1;
        HMENU menu=CreatePopupMenu(), column_items=CreatePopupMenu(), header_items=CreatePopupMenu(), group_items=CreatePopupMenu();
        for (size_t i=0;i<columns_.size();++i) {
            const UINT flags=MF_STRING|(columns_[i].visible?MF_CHECKED:0)|
                (columns_[i].visible && visible_columns_.size()==1?MF_GRAYED:0);
            AppendMenuW(column_items,flags,100+i,wide(columns_[i].title.c_str()).c_str());
        }
        AppendMenuW(column_items,MF_SEPARATOR,0,nullptr);
        AppendMenuW(column_items,MF_STRING|(columns_.size()>=64?MF_GRAYED:0),1,L"Add column…");
        HMENU edit_items=CreatePopupMenu();
        for (size_t i=0;i<columns_.size();++i)
            AppendMenuW(edit_items,MF_STRING,200+i,wide(columns_[i].title.c_str()).c_str());
        AppendMenuW(column_items,MF_POPUP,reinterpret_cast<UINT_PTR>(edit_items),L"Edit columns…");
        if (col>=0) AppendMenuW(column_items,MF_STRING|(visible_columns_.size()==1?MF_GRAYED:0),3,L"Delete this column");
        AppendMenuW(column_items,MF_STRING,4,L"Reset columns");
        AppendMenuW(header_items,MF_STRING|(show_header_?MF_CHECKED:0),11,L"Show column headers	Ctrl+T");
        AppendMenuW(header_items,MF_STRING|(headers_follow_alignment_?MF_CHECKED:0),12,L"Headers follow content alignment");
        AppendMenuW(header_items,MF_STRING|(fit_to_window_?MF_CHECKED:0),6,L"Fit to Window");
        AppendMenuW(header_items,MF_STRING|(show_tabs_?MF_CHECKED:0),5,L"Show playlist tabs");
        AppendMenuW(header_items,MF_STRING|(manager_bottom_?MF_CHECKED:0),14,L"Playlist manager below playlist");
        AppendMenuW(group_items,MF_STRING|(core_.group_parity?MF_CHECKED:0),13,L"Alternate within album groups");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(column_items),L"Columns");
        AppendMenuW(menu,MF_POPUP,reinterpret_cast<UINT_PTR>(header_items),L"Header Bar");
        DestroyMenu(group_items); append_groups_menu(menu); append_search_menu(menu);
        AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
        AppendMenuW(menu,MF_STRING|(core_.extra_line?MF_CHECKED:0),7,L"Show Row Extra-Line Infos");
        AppendMenuW(menu,MF_STRING,8,L"Panel Settings…");
        if (play_control::get()->is_playing()) AppendMenuW(menu,MF_STRING,9,L"Show Now Playing");
        int command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,hwnd_,nullptr);
        DestroyMenu(menu);
        if (!command || epoch!=playlist_epoch_ || menu_id!=layout_id_ || pending_) return;
        if(search_command(command) || group_command(command)) return;
        if (command==5) { toggle_tabs(); return; }
        if (command==14) { manager_bottom_=!manager_bottom_; layout(); return; }
        if (command==6) { toggle_fit_to_window(); return; }
        if (command==7) { core_.extra_line=!core_.extra_line; theme(); layout(); return; }
        if (command==8) { edit_core_settings(); return; }
        if (command==9) { show_now_playing(true); return; }
        if (command==11) { toggle_header(); return; }
        if (command==12) { headers_follow_alignment_=!headers_follow_alignment_; InvalidateRect(header_,nullptr,FALSE); return; }
        if (command==13) { core_.group_parity=!core_.group_parity; theme(); layout(); return; }
        if (command>=100 && command<200 && static_cast<size_t>(command-100)<columns_.size()) {
            auto& c=columns_[command-100];
            if (!c.visible || visible_columns_.size()>1) {
                c.visible=!c.visible;
                normalize_percents();
            }
        } else if (command==1 || (command>=200 && static_cast<size_t>(command-200)<columns_.size())) {
            dialog_data d; d.is_column=true;
            d.edited=command==1?column{"New column","%title%",150}:columns_[command-200];
            if (command==1) d.edited.percent=1000;
            if (DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_COLUMN),hwnd_,dialog_proc,reinterpret_cast<LPARAM>(&d))!=IDOK) return;
            if (epoch!=playlist_epoch_ || menu_id!=layout_id_ || pending_) return;
            if (command==1) { if (columns_.size()<64) columns_.push_back(d.edited); }
            else columns_[command-200]=d.edited;
        } else if (command==3 && col>=0 && visible_columns_.size()>1) {
            columns_.erase(columns_.begin()+col);
            if (sort_column_==col) sort_column_=-1;
            else if (sort_column_>col) --sort_column_;
        } else if (command==4) { columns_=defaults(); sort_column_=-1; }
        else return;
        compile_columns(); make_columns(); save_playlist_columns(); refresh();
    }
    static LRESULT CALLBACK child_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR subclass_id, DWORD_PTR data) {
        auto* self=reinterpret_cast<playlist_view*>(data);
        // Never query a child control to identify it here: SendMessage would
        // re-enter this subclass (LVM_GETHEADER caused the exit stack overflow).
        if (msg==WM_NCDESTROY) {
            RemoveWindowSubclass(wnd,child_proc,subclass_id);
            self->forget_child(wnd);
            return DefSubclassProc(wnd,msg,wp,lp);
        }
        if (self->destroying_) return DefSubclassProc(wnd,msg,wp,lp);
        try {
            if (msg==HDM_LAYOUT && wnd==self->header_) {
                LRESULT result=DefSubclassProc(wnd,msg,wp,lp);
                auto* layout=reinterpret_cast<HDLAYOUT*>(lp);
                const int height=self->show_header_?self->header_pixels_:0;
                layout->pwpos->cy=height; layout->prc->top=layout->pwpos->y+height;
                return result;
            }
            if(wnd==self->tabs_) {
                // The shared MSAA adapter queries this read-only list-message contract.
                if(msg==WM_GETOBJECT && static_cast<LONG>(lp)==OBJID_CLIENT) {
                    if(!self->manager_accessible_) self->manager_accessible_.Attach(new modern_playlist::viewport_accessibility(wnd,true));
                    return LresultFromObject(IID_IAccessible,wp,self->manager_accessible_.Get());
                }
                if(msg==LVM_GETITEMCOUNT) return self->pending_?0:self->tab_names_.size();
                if(msg==LVM_GETITEMTEXTW) {
                    auto* item=reinterpret_cast<LVITEMW*>(lp);
                    if(item && item->pszText && item->cchTextMax>0 && wp<self->tab_names_.size()) {
                        lstrcpynW(item->pszText,self->tab_names_[wp].c_str(),item->cchTextMax); return lstrlenW(item->pszText);
                    }
                    return 0;
                }
                if(msg==LVM_GETITEMRECT) return self->tab_rect(int(wp),reinterpret_cast<RECT*>(lp));
                if(msg==LVM_HITTEST) { auto* hit=reinterpret_cast<LVHITTESTINFO*>(lp); return hit->iItem=self->tab_hit(hit->pt); }
                if(msg==LVM_GETITEMSTATE) return wp==self->active_?(LVIS_SELECTED|LVIS_FOCUSED)&lp:0;
                if(msg==LVM_GETNEXTITEM) return self->active_<self->tab_names_.size() && int(wp)<int(self->active_)?LRESULT(self->active_):-1;
                if(msg==modern_playlist::viewport_access_action) {
                    if(self->pending_ || wp>=self->tab_names_.size()) return FALSE;
                    SetFocus(wnd); playlist_manager::get()->set_active_playlist(wp); return TRUE;
                }
            }
            if(msg==WM_CONTEXTMENU && (wnd==self->tabs_ || wnd==self->add_ || wnd==self->tab_left_ || wnd==self->tab_right_))
                return SendMessageW(self->hwnd_,msg,reinterpret_cast<WPARAM>(wnd),lp);
            if (wnd==self->list_ && msg==WM_SIZE && !self->fitting_columns_ && !self->rebuilding_)
                PostMessageW(self->hwnd_,fit_columns_message,0,0);
            if (wnd==self->list_ && msg==WM_NOTIFY) {
                auto* header=reinterpret_cast<NMHDR*>(lp);
                if (header->hwndFrom==self->header_) {
                    if (header->code==NM_CUSTOMDRAW) return self->draw_header(reinterpret_cast<NMCUSTOMDRAW*>(lp));
                    if (header->code==HDN_ITEMCHANGINGW || header->code==HDN_ITEMCHANGINGA) {
                        auto* change=reinterpret_cast<NMHEADERW*>(lp);
                        if (!self->fitting_columns_ && !self->rebuilding_ && change->pitem &&
                            (change->pitem->mask&HDI_WIDTH) && change->pitem->cxy<self->scale(32)) return TRUE;
                    }
                    if (header->code==HDN_ENDTRACKW || header->code==HDN_ENDTRACKA) {
                        auto* change=reinterpret_cast<NMHEADERW*>(lp);
                        if (!self->fitting_columns_ && change->pitem && (change->pitem->mask&HDI_WIDTH)) {
                            for (size_t i=0;i<self->visible_columns_.size();++i) {
                                const int width=static_cast<int>(i)==change->iItem ? change->pitem->cxy :
                                    ListView_GetColumnWidth(wnd,static_cast<int>(i));
                                self->columns_[self->visible_columns_[i]].width=
                                    std::clamp(MulDiv(width,96,self->scale(96)),32,4000);
                            }
                            self->normalize_percents();
                            if (change->iItem==self->stretched_column_)
                                self->stretched_base_width_=std::max(self->scale(32),change->pitem->cxy);
                        }
                        PostMessageW(self->hwnd_,fit_columns_message,0,0);
                    }
                    if (header->code==HDN_ENDDRAG) PostMessageW(self->hwnd_,header_order_message,0,0);
                }
            }
            if(msg==WM_ERASEBKGND && (wnd==self->add_ || wnd==self->tab_left_ || wnd==self->tab_right_)) {
                RECT r{}; GetClientRect(wnd,&r); fill(reinterpret_cast<HDC>(wp),r,self->current_palette().row); return 1;
            }
            if (wnd==self->tabs_ && msg==WM_PAINT) { self->paint_tabs(); return 0; }
            if (wnd==self->tabs_ && msg==WM_ERASEBKGND) return 1;
            if (msg==WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL)) {
                self->zoom(GET_WHEEL_DELTA_WPARAM(wp)); return 0;
            }
            if (msg==WM_KEYDOWN) {
                bool ctrl=(GetKeyState(VK_CONTROL)&0x8000)!=0;
                if (ctrl && wp=='F') { self->search_settings_.visible=true; self->layout(); SetFocus(self->search_); SendMessageW(self->search_,EM_SETSEL,0,-1); return 0; }
                if (ctrl && wp=='T') { self->toggle_header(); return 0; }
                if (ctrl && wp=='N') { self->new_playlist(); return 0; }
                if (wp==VK_ESCAPE && self->drag_tab_>=0) { self->cancel_tab_drag(); return 0; }
                if (wp==VK_ESCAPE && wnd==self->list_ && !self->incremental_.text.empty()) { self->clear_incremental(); return 0; }
                if (wp==VK_ESCAPE) {
                    if (wnd==self->search_ || (wnd==self->list_ && GetWindowTextLengthW(self->search_) > 0)) {
                        self->clear_search(); return 0;
                    }
                }
                if (wnd==self->list_ && !self->pending_) {
                    if(wp==VK_SPACE && !ctrl && !self->incremental_.text.empty() && !self->incremental_.expired(GetTickCount64())) return 0;
                    if (wp==VK_RETURN) { self->play(); return 0; }
                    if (wp==VK_DELETE) { self->remove_tracks(); return 0; }
                    if (ctrl && wp=='A') { ListView_SetItemState(wnd,-1,LVIS_SELECTED,LVIS_SELECTED); return 0; }
                    if (ctrl && wp=='Z') { playlist_manager::get()->activeplaylist_undo_restore(); return 0; }
                    if (ctrl && wp=='Y') { playlist_manager::get()->activeplaylist_redo_restore(); return 0; }
                    if (ctrl && (wp=='C' || wp=='X')) { self->copy_tracks(wp=='X'); return 0; }
                    if (ctrl && wp=='V') { self->paste_tracks(); return 0; }
                }
            }
            if(msg==WM_MBUTTONUP && (wnd==self->list_ || wnd==self->search_)) { self->toggle_search(); return 0; }
            if (wnd==self->list_) {
                if(msg==WM_KILLFOCUS) self->clear_incremental();
                if(msg==WM_CHAR && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000)) {
                    if(wp==VK_BACK || (wp>=32 && wp!=127)) { self->incremental_input(wchar_t(wp)); return 0; }
                }
                if (msg==WM_SYSKEYDOWN && (wp==VK_UP || wp==VK_DOWN)) { self->move_tracks(wp==VK_UP?-1:1); return 0; }
                if (msg==WM_LBUTTONUP && self->dragging_tracks_) {
                    self->dragging_tracks_=false; ReleaseCapture();
                    LVHITTESTINFO hit{}; hit.pt={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    RECT rc{}; GetClientRect(wnd,&rc);
                    if (PtInRect(&rc,hit.pt)) {
                        int row=ListView_HitTest(wnd,&hit); self->drop_tracks(row<0?static_cast<int>(self->rows_.size()):row);
                    }
                    return 0;
                }
                if (msg==WM_CAPTURECHANGED) self->dragging_tracks_=false;
            }
            if (wnd==self->tabs_) {
                if(msg==WM_GETDLGCODE) return DLGC_WANTARROWS|DLGC_WANTCHARS;
                if(msg==WM_SETFOCUS || msg==WM_KILLFOCUS) InvalidateRect(wnd,nullptr,FALSE);
                if(msg==WM_KEYDOWN && (wp==VK_LEFT || wp==VK_RIGHT || wp==VK_HOME || wp==VK_END)) {
                    auto pm=playlist_manager::get(); int count=int(pm->get_playlist_count());
                    if(count) { int next=wp==VK_HOME?0:wp==VK_END?count-1:int(pm->get_active_playlist())+(wp==VK_LEFT?-1:1); pm->set_active_playlist(std::clamp(next,0,count-1)); }
                    return 0;
                }
                if(msg==WM_KEYDOWN && wp==VK_F2) { self->rename_playlist(playlist_manager::get()->get_active_playlist()); return 0; }
                if(msg==WM_LBUTTONDOWN) {
                    if(self->pending_) self->refresh();
                    SetFocus(wnd); POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    self->drag_tab_=self->tab_hit(pt); self->drag_epoch_=self->playlist_epoch_;
                    self->drag_tab_start_=pt; self->drag_tab_moved_=false;
                    if(self->drag_tab_>=0) self->drag_grab_={pt.x-self->manager_.left(self->drag_tab_),pt.y};
                    if(self->drag_tab_>=0) SetCapture(wnd);
                    return 0;
                }
                if(msg==WM_MOUSEMOVE && self->drag_tab_>=0 && (wp&MK_LBUTTON)) {
                    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
                    if(self->drag_epoch_!=self->playlist_epoch_) { self->cancel_tab_drag(); return 0; }
                    if(!self->drag_tab_moved_ && !modern_playlist::library_pinned(self->drag_tab_) &&
                        (std::abs(pt.x-self->drag_tab_start_.x)>=self->scale(4) || std::abs(pt.y-self->drag_tab_start_.y)>=self->scale(4))) {
                        self->drag_tab_moved_=true; self->start_drag_ghost(pt);
                        SetTimer(self->hwnd_,5,120,nullptr);
                    }
                    if(self->drag_tab_moved_) {
                        self->drag_before_=self->manager_.insertion(pt.x);
                        if(modern_playlist::library_pinned(0)) self->drag_before_=std::max(1,self->drag_before_);
                        self->move_drag_ghost(pt); InvalidateRect(wnd,nullptr,FALSE);
                    }
                    return 0;
                }
                if(msg==WM_LBUTTONUP && self->drag_tab_>=0) {
                    POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}; const int hit=self->tab_hit(pt),from=self->drag_tab_;
                    const bool moved=self->drag_tab_moved_,valid=self->drag_epoch_==self->playlist_epoch_;
                    const int before=self->manager_.insertion(pt.x);
                    self->cancel_tab_drag();
                    if(!valid) return 0;
                    auto pm=playlist_manager::get();
                    if(moved && pt.y>=0 && pt.y<self->tab_pixels_ && pt.x>=0 && pt.x<self->manager_.viewport) {
                        int to=modern_playlist::manager_drop_destination(from,before,int(pm->get_playlist_count()),modern_playlist::library_pinned(0)?0:-1);
                        if(to>=0 && to!=from) { auto order=modern_playlist::move_order(pm->get_playlist_count(),from,to); pm->reorder(order.data(),order.size()); }
                    } else if(!moved && hit==from) {
                        RECT tab{}; self->tab_rect(hit,&tab);
                        if(pt.x>=tab.right-self->scale(20)) {
                            if(size_t(hit)!=self->playing_playlist() && !modern_playlist::special_reserved(hit) &&
                                !(pm->playlist_lock_get_filter_mask(hit)&playlist_lock::filter_remove_playlist)) pm->remove_playlist_user(hit);
                        } else pm->set_active_playlist(hit);
                    }
                    return 0;
                }
                if(msg==WM_CAPTURECHANGED || msg==WM_CANCELMODE || (msg==WM_KEYDOWN && wp==VK_ESCAPE)) { self->cancel_tab_drag(); return 0; }
                if(msg==WM_MOUSEWHEEL || msg==WM_MOUSEHWHEEL) { self->on_tabs_wheel(GET_WHEEL_DELTA_WPARAM(wp)*(msg==WM_MOUSEWHEEL?1:-1)); return 0; }
            }
            if (msg == WM_MOUSEMOVE) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, wnd, 0};
                TrackMouseEvent(&tme);
                self->update_hover();
            } else if (msg == WM_MOUSELEAVE) {
                self->update_hover();
            }
            if (wnd == self->search_) {
                if (msg == WM_GETDLGCODE) {
                    LRESULT res = DefSubclassProc(wnd, msg, wp, lp);
                    if (wp == VK_ESCAPE || (lp && reinterpret_cast<MSG*>(lp)->message == WM_KEYDOWN && reinterpret_cast<MSG*>(lp)->wParam == VK_ESCAPE)) {
                        return res | DLGC_WANTALLKEYS;
                    }
                    return res;
                }
                if (msg == WM_CHAR && wp == VK_ESCAPE) return 0;
                if (msg == WM_PAINT) {
                    if (GetWindowTextLengthW(wnd) == 0) {
                        PAINTSTRUCT ps{};
                        HDC dc = BeginPaint(wnd, &ps);
                        if (dc) {
                            int saved = SaveDC(dc);
                            const auto& pal = self->current_palette();
                            RECT client{};
                            GetClientRect(wnd, &client);
                            fill(dc, client, pal.search_bg);
                            SelectObject(dc, self->default_font_);
                            SetTextColor(dc, pal.muted);
                            SetBkMode(dc, TRANSPARENT);
                            RECT text_rect = client;
                            text_rect.left += self->scale(2);
                            text_rect.right -= self->scale(2);
                            DrawTextW(dc, search_placeholder, -1, &text_rect, DT_SINGLELINE | DT_NOPREFIX);
                            RestoreDC(dc, saved);
                            EndPaint(wnd, &ps);
                        }
                        return 0;
                    }
                    return DefSubclassProc(wnd, msg, wp, lp);
                }
                if (msg == WM_ERASEBKGND) {
                    if (GetWindowTextLengthW(wnd) == 0) return 1;
                }
                if (msg == WM_SETFOCUS || msg == WM_KILLFOCUS || msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == EM_SETSEL) {
                    LRESULT lr = DefSubclassProc(wnd, msg, wp, lp);
                    if (GetWindowTextLengthW(wnd) == 0) {
                        InvalidateRect(wnd, nullptr, FALSE);
                    }
                    return lr;
                }
                if (msg == WM_SETTEXT) {
                    LRESULT lr = DefSubclassProc(wnd, msg, wp, lp);
                    InvalidateRect(wnd, nullptr, TRUE);
                    return lr;
                }
            }
        } catch (const std::exception& e) { console::error(e.what()); }
        return DefSubclassProc(wnd,msg,wp,lp);
    }
    static LRESULT CALLBACK window_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self=reinterpret_cast<playlist_view*>(GetWindowLongPtrW(wnd,GWLP_USERDATA));
        if (msg==WM_NCCREATE) {
            self=static_cast<playlist_view*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
            self->hwnd_=wnd; SetWindowLongPtrW(wnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(wnd,msg,wp,lp);
        try { return self->message(msg,wp,lp); }
        catch (const std::exception& e) { self->rebuilding_=false; console::error(e.what()); return 0; }
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        if (msg==WM_DESTROY) { begin_destroy(); return 0; }
        if (msg==WM_NCDESTROY) {
            const auto wnd=hwnd_;
            hwnd_=tabs_=search_=list_=header_=notice_=add_=tab_left_=tab_right_=search_field_=search_scope_=nullptr;
            SetWindowLongPtrW(wnd,GWLP_USERDATA,0);
            return DefWindowProcW(wnd,msg,wp,lp);
        }
        if (destroying_) return DefWindowProcW(hwnd_,msg,wp,lp);
        switch(msg) {
        case WM_CREATE: {
            auto instance=core_api::get_my_instance();
            tabs_=CreateWindowExW(0,L"STATIC",L"Playlist manager",WS_CHILD|WS_TABSTOP|SS_NOTIFY,0,0,0,0,hwnd_,nullptr,instance,nullptr);
            add_=CreateWindowExW(0,L"BUTTON",L"+",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(10),instance,nullptr);
            tab_left_=CreateWindowExW(0,L"BUTTON",L"Scroll playlists left",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(12),instance,nullptr);
            tab_right_=CreateWindowExW(0,L"BUTTON",L"Scroll playlists right",WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(13),instance,nullptr);
            search_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(11),instance,nullptr);
            SendMessageW(search_,EM_SETLIMITTEXT,16384,0);
            search_field_=CreateWindowExW(0,L"COMBOBOX",L"Search field",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(15),instance,nullptr);
            search_scope_=CreateWindowExW(0,L"COMBOBOX",L"Search scope",WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST,0,0,0,0,hwnd_,reinterpret_cast<HMENU>(16),instance,nullptr);
            for(auto label:{L"All fields",L"Artist",L"Title",L"Album"}) SendMessageW(search_field_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            for(auto label:{L"Current playlist",L"Media library"}) SendMessageW(search_scope_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label));
            list_=modern_playlist::create_playlist_viewport(hwnd_,instance);
            if (!list_) throw std::runtime_error("Cannot create playlist viewport");
            notice_=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|SS_LEFT,0,0,0,0,hwnd_,nullptr,instance,nullptr);
            // Resolve the header once, before the list's subclass is installed.
            header_=ListView_GetHeader(list_);
            for (HWND child:{tabs_,search_,list_,add_,header_,tab_left_,tab_right_,search_field_,search_scope_}) SetWindowSubclass(child,child_proc,1,reinterpret_cast<DWORD_PTR>(this));
            // The virtual viewport owns its HWND; do not let host dark-list helpers replace it.
            // Establish the tab viewport before inserting/selecting tabs.
            update_dpi(); make_columns(); theme(); layout(); refresh(); return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint_frame(); return 0;
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&tme);
            update_hover();
            return 0;
        }
        case WM_MOUSELEAVE: {
            update_hover();
            return 0;
        }
        case WM_ACTIVATE: {
            if (LOWORD(wp) == WA_INACTIVE && hover_ != hover_area::none) {
                hover_ = hover_area::none;
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            break;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            const auto& pal = current_palette();
            SetTextColor(reinterpret_cast<HDC>(wp),pal.text);
            SetBkColor(reinterpret_cast<HDC>(wp),reinterpret_cast<HWND>(lp)==search_?pal.search_bg:pal.surface);
            return reinterpret_cast<LRESULT>(reinterpret_cast<HWND>(lp)==search_?edit_background_:background_);
        }
        case WM_DRAWITEM: {
            auto* draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (draw->hwndItem==add_) {
                const auto& pal = current_palette();
                update_hover();
                const bool pressed=(draw->itemState&ODS_SELECTED)!=0;
                const bool enabled=(draw->itemState&ODS_DISABLED)==0;
                int saved=SaveDC(draw->hDC);
                fill(draw->hDC,draw->rcItem,enabled && pressed ? pal.selection :
                    (enabled && hovered_add_ ? hover_background() : pal.row));
                SetTextColor(draw->hDC,!enabled?pal.muted:(pressed?pal.selected_text:pal.text)); SetBkMode(draw->hDC,TRANSPARENT); SelectObject(draw->hDC,tabs_font_);
                DrawTextW(draw->hDC,L"+",1,&draw->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
                if (draw->itemState&ODS_FOCUS) DrawFocusRect(draw->hDC,&draw->rcItem);
                RestoreDC(draw->hDC,saved); return TRUE;
            }
            if (draw->hwndItem==tab_left_ || draw->hwndItem==tab_right_) {
                const auto& pal = current_palette();
                int saved=SaveDC(draw->hDC);
                bool pressed = (draw->itemState & ODS_SELECTED) != 0;
                fill(draw->hDC, draw->rcItem, pressed ? pal.header : pal.row);
                bool enabled = IsWindowEnabled(draw->hwndItem);
                COLORREF color = enabled ? (pressed ? pal.selected_text : pal.text) : pal.muted;
                int cx = (draw->rcItem.left + draw->rcItem.right) / 2;
                int cy = (draw->rcItem.top + draw->rcItem.bottom) / 2;
                int s = scale(4);
                HPEN pen = CreatePen(PS_SOLID, std::max(1, scale(2)), color);
                HGDIOBJ old_pen = SelectObject(draw->hDC, pen);
                POINT pts[3];
                if (draw->hwndItem == tab_left_) {
                    pts[0] = {cx + s / 2, cy - s};
                    pts[1] = {cx - s / 2, cy};
                    pts[2] = {cx + s / 2, cy + s};
                } else {
                    pts[0] = {cx - s / 2, cy - s};
                    pts[1] = {cx + s / 2, cy};
                    pts[2] = {cx - s / 2, cy + s};
                }
                Polyline(draw->hDC, pts, 3);
                SelectObject(draw->hDC, old_pen);
                DeleteObject(pen);
                if (draw->itemState & ODS_FOCUS) DrawFocusRect(draw->hDC, &draw->rcItem);
                RestoreDC(draw->hDC, saved);
                return TRUE;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL) { zoom(GET_WHEEL_DELTA_WPARAM(wp)); return 0; }
            if (show_tabs_) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                RECT strip{}; GetWindowRect(tabs_,&strip); MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
                if (pt.y >= strip.top && pt.y < strip.bottom) {
                    on_tabs_wheel(GET_WHEEL_DELTA_WPARAM(wp));
                    return 0;
                }
            }
            break;
        }
        case WM_MOUSEHWHEEL: {
            if (show_tabs_) {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ScreenToClient(hwnd_, &pt);
                RECT strip{}; GetWindowRect(tabs_,&strip); MapWindowPoints(nullptr,hwnd_,reinterpret_cast<POINT*>(&strip),2);
                if (pt.y >= strip.top && pt.y < strip.bottom) {
                    on_tabs_wheel(-GET_WHEEL_DELTA_WPARAM(wp));
                    return 0;
                }
            }
            break;
        }
        case WM_DPICHANGED:
        case 0x02E3: // WM_DPICHANGED_AFTERPARENT (newer than the SDK minimum target)
            capture_columns(); update_dpi(); theme(); make_columns(); layout(); invalidate_all(); return 0;
        case WM_SIZE: layout(); return 0;
        case WM_SETFOCUS: SetFocus(list_); return 0;
        case WM_MBUTTONUP: toggle_search(); return 0;
        case WM_COMMAND:
            if((LOWORD(wp)==15 || LOWORD(wp)==16) && HIWORD(wp)==CBN_SELCHANGE) {
                KillTimer(hwnd_,search_timer);
                search_settings_.field=unsigned(SendMessageW(search_field_,CB_GETCURSEL,0,0));
                search_settings_.scope=unsigned(SendMessageW(search_scope_,CB_GETCURSEL,0,0));
                if(!search_settings_.scope && active_<queries_.size()) queries_[active_]=window_text(search_);
                apply_search(); return 0;
            }
            if (LOWORD(wp)==IDCANCEL) { clear_search(); return 0; }
            if (LOWORD(wp)==10 && HIWORD(wp)==BN_CLICKED) { new_playlist(); SendMessageW(add_,BM_SETSTATE,FALSE,0); SetFocus(tabs_); InvalidateRect(add_,nullptr,FALSE); }
            if (LOWORD(wp)==12 && HIWORD(wp)==BN_CLICKED) scroll_tabs(-1);
            if (LOWORD(wp)==13 && HIWORD(wp)==BN_CLICKED) scroll_tabs(1);
            if (LOWORD(wp)==11 && HIWORD(wp)==EN_CHANGE && !rebuilding_) {
                int len = GetWindowTextLengthW(search_);
                if (len <= 1) {
                    InvalidateRect(search_, nullptr, TRUE);
                }
                if (!search_settings_.scope && active_<queries_.size()) queries_[active_]=window_text(search_);
                SetTimer(hwnd_,search_timer,modern_playlist::search_delay_ms,nullptr);
            }
            return 0;
        case state_message: SetTimer(hwnd_,state_timer,1,nullptr); return 0;
        case WM_TIMER: if(wp==incremental_timer) {
            if(incremental_.expired(GetTickCount64())) clear_incremental(); return 0;
        } if(wp==5) {
            if(drag_epoch_!=playlist_epoch_ || !drag_tab_moved_) { cancel_tab_drag(); return 0; }
            POINT pt{}; GetCursorPos(&pt); ScreenToClient(tabs_,&pt);
            if(pt.x<scale(20)) scroll_tabs(-1); else if(pt.x>=manager_.viewport-scale(20)) scroll_tabs(1);
            drag_before_=manager_.insertion(pt.x); if(modern_playlist::library_pinned(0)) drag_before_=std::max(1,drag_before_);
            InvalidateRect(tabs_,nullptr,FALSE); return 0;
        } if(wp==4) { finish_cover(); return 0; } if (wp==state_timer) { KillTimer(hwnd_,state_timer); update_state(); return 0; } if (wp==search_timer) { KillTimer(hwnd_,search_timer); apply_search(); } return 0;
        case fit_columns_message: fit_columns(); return 0;
        case header_order_message: save_playlist_columns(); fit_columns(); return 0;
        case refresh_message: if (pending_) refresh(); return 0;
        case WM_NOTIFY: {
            auto* h=reinterpret_cast<NMHDR*>(lp);
            if (h->hwndFrom==list_) {
                if(h->code==modern_playlist::viewport_group_toggle) {
                    const int g=reinterpret_cast<modern_playlist::viewport_group_request*>(lp)->group;
                    if(!pending_ && g>=0 && size_t(g)<groups_.size()) {
                        grouping_.autocollapse=false; search_reveal_=pfc::infinite_size;
                        collapsed_[group_ids_[g]]=!groups_[g].collapsed; refresh();
                    }
                    return 0;
                }
                if(h->code==modern_playlist::viewport_group_cover) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_group_request*>(lp);
                    if(!pending_ && request.group>=0 && size_t(request.group)<group_members_.size())
                        request.pixels=group_cover(group_members_[request.group].front(),request.load);
                    return 0;
                }
                if (h->code==modern_playlist::viewport_row_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_row_request*>(lp);
                    if (request.row>=0 && static_cast<size_t>(request.row)<row_data_.size()) {
                        const auto& row=row_data_[request.row]; request.global_index=row.track_index; request.group_index=row.track_index_in_group;
                        request.playing=row.playing; request.paused=row.paused;
                        for (auto position : row.queue_positions) { if(!request.queue.empty()) request.queue+=L", "; request.queue+=std::to_wstring(position); }
                    }
                    return 0;
                }
                if (h->code==modern_playlist::viewport_cell_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_cell_request*>(lp);
                    if (request.row>=0 && static_cast<size_t>(request.row)<rows_.size() && request.column>=0 && static_cast<size_t>(request.column)<visible_columns_.size()) {
                        const auto& col=columns_[visible_columns_[request.column]]; request.state_column=col.state;
                        if (core_.extra_line && !col.state && !col.secondary_pattern.empty()) {
                            column secondary=col; secondary.script=col.secondary_script; pfc::string8 text;
                            format_cell(request.row,secondary,text); request.secondary=wide(text.c_str());
                        }
                    }
                    return 0;
                }
                if (h->code==modern_playlist::viewport_tooltip_info) {
                    auto& request=*reinterpret_cast<modern_playlist::viewport_tooltip_request*>(lp);
                    if (!pending_ && core_.tooltips && request.row>=0 && static_cast<size_t>(request.row)<rows_.size()) {
                        pfc::string8 text; playlist_manager::get()->playlist_item_format_title(active_,rows_[request.row],nullptr,text,tooltip_script_,nullptr,play_control::display_level_all);
                        request.text=wide(text.c_str());
                    }
                    return 0;
                }

                if (h->code==LVN_GETDISPINFOW) {
                    auto* info=reinterpret_cast<NMLVDISPINFOW*>(lp); auto& item=info->item;
                    if ((item.mask & LVIF_TEXT) && item.iItem>=0 && static_cast<size_t>(item.iItem)<rows_.size() && item.iSubItem>=0 && static_cast<size_t>(item.iSubItem)<visible_columns_.size()) {
                        pfc::string8 text; format_cell(static_cast<size_t>(item.iItem),columns_[visible_columns_[item.iSubItem]],text);
                        cell_=wide(text.c_str()); item.pszText=cell_.data();
                    } return 0;
                }
                if (h->code==LVN_ITEMCHANGED || h->code==LVN_ODSTATECHANGED) { select_view(); return 0; }
                if (h->code==NM_DBLCLK) { if (reinterpret_cast<NMITEMACTIVATE*>(lp)->iItem>=0) default_action(); return 0; }
                if (h->code==LVN_BEGINDRAG && !pending_) { dragging_tracks_=true; SetCapture(list_); return 0; }
                if (h->code==LVN_COLUMNCLICK) { sort(reinterpret_cast<NMLISTVIEW*>(lp)->iSubItem); return 0; }
            }
            break;
        }
        case WM_CONTEXTMENU: {
            HWND source=reinterpret_cast<HWND>(wp); POINT pt{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            if (pt.x==-1 && pt.y==-1) {
                pt={10,10};
                if(source==tabs_) { manager_.reveal(active_==pfc::infinite_size?-1:int(active_)); RECT tab{};
                    if(tab_rect(int(active_),&tab)) pt={std::max(0L,tab.left)+scale(8),scale(8)};
                }
                if (source==list_) {
                    RECT row{}; int focus=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
                    if (focus>=0 && ListView_GetItemRect(list_,focus,&row,LVIR_BOUNDS)) pt={row.left+8,row.top+8};
                    else { RECT header{}; GetWindowRect(header_,&header); pt.y=header.bottom-header.top+8; }
                }
                ClientToScreen(source,&pt);
            }
            RECT header{}; GetWindowRect(header_,&header);
            if ((source==list_ || source==header_) && PtInRect(&header,pt)) column_menu(pt);
            else if (source==tabs_ || source==add_ || source==tab_left_ || source==tab_right_) tab_menu(pt);
            else if(source==hwnd_ && show_tabs_) {
                RECT strip{}; GetWindowRect(tabs_,&strip);
                if(pt.y>=strip.top && pt.y<strip.bottom) tab_menu(pt); else break;
            }
            else if (source==list_ && !pending_) {
                track_menu(pt);
            } else break;
            return 0;
        }
        }
        return DefWindowProcW(hwnd_,msg,wp,lp);
    }
    std::shared_ptr<modern_playlist::cover_pixels> group_cover(t_size track,bool load) {
        const std::string key=std::string(items_[track]->get_path())+"#"+std::to_string(items_[track]->get_subsong_index());
        const auto found=covers_.find(key); if(found!=covers_.end()) return found->second;
        if(!load || cover_job_.valid() || destroying_) return {};
        const auto handle=items_[track]; auto manager=album_art_manager_v2::get();
        cover_job_key_=key;
        auto* abort=&cover_abort_;
        cover_job_=std::async(std::launch::async,[handle,manager,abort]() -> std::shared_ptr<modern_playlist::cover_pixels> {
            const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
            struct com_scope { HRESULT result; ~com_scope(){ if(SUCCEEDED(result)) CoUninitialize(); } } scope{initialized};
            try {
                metadb_handle_list tracks; tracks.add_item(handle);
                pfc::list_t<GUID> ids; ids.add_item(album_art_ids::cover_front);
                auto extractor=manager->open(tracks,ids,*abort);
                auto data=extractor->query(album_art_ids::cover_front,*abort);
                if(data->get_size()>32*1024*1024) return {};
                using Microsoft::WRL::ComPtr;
                ComPtr<IStream> stream; stream.Attach(SHCreateMemStream(static_cast<const BYTE*>(data->get_ptr()),static_cast<UINT>(data->get_size())));
                ComPtr<IWICImagingFactory> factory; ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
                if(!stream || FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(factory.GetAddressOf()))) ||
                    FAILED(factory->CreateDecoderFromStream(stream.Get(),nullptr,WICDecodeMetadataCacheOnLoad,decoder.GetAddressOf())) ||
                    FAILED(decoder->GetFrame(0,frame.GetAddressOf()))) return {};
                UINT width=0,height=0; if(FAILED(frame->GetSize(&width,&height)) || !width || !height) return {};
                const double ratio=std::min(1.0,256.0/std::max(width,height));
                width=std::max(1U,UINT(width*ratio)); height=std::max(1U,UINT(height*ratio));
                ComPtr<IWICBitmapScaler> scaler; ComPtr<IWICFormatConverter> converter;
                if(FAILED(factory->CreateBitmapScaler(scaler.GetAddressOf())) ||
                    FAILED(scaler->Initialize(frame.Get(),width,height,WICBitmapInterpolationModeFant)) ||
                    FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
                    FAILED(converter->Initialize(scaler.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
                auto pixels=std::make_shared<modern_playlist::cover_pixels>(); pixels->width=width; pixels->height=height; pixels->bgra.resize(size_t(width)*height*4);
                if(FAILED(converter->CopyPixels(nullptr,width*4,static_cast<UINT>(pixels->bgra.size()),pixels->bgra.data()))) return {};
                abort->check(); return pixels;
            } catch(...) { return {}; }
        });
        SetTimer(hwnd_,4,30,nullptr); return {};
    }
    void finish_cover() {
        if(!cover_job_.valid()) { KillTimer(hwnd_,4); return; }
        if(cover_job_.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
        auto pixels=cover_job_.get();
        if(covers_.size()>=128) covers_.erase(covers_.begin());
        covers_[cover_job_key_]=std::move(pixels); KillTimer(hwnd_,4);
        modern_playlist::invalidate_playlist_row(list_,-1);
    }
    void build_rows() {
        rows_.clear(); row_data_.clear(); groups_.clear(); group_members_.clear(); group_ids_.clear();
        modern_playlist::group_position position{}; std::string previous;
        std::vector<modern_playlist::group_position> positions(items_.get_count());
        std::vector<std::vector<t_size>> members; std::vector<std::string> ids;
        size_t visible=0;
        for(size_t i=0;i<items_.get_count();++i) {
            pfc::string8 key; items_[i]->format_title(nullptr,key,group_script_,nullptr);
            position=modern_playlist::next_group_position(previous,key.c_str(),position,i==0);
            if(i==0 || key.c_str()!=previous) { members.emplace_back(); ids.push_back(std::to_string(i)+":"+key.c_str()); }
            previous=key.c_str(); positions[i]=position;
            if(visible<filtered_rows_.size() && filtered_rows_[visible]==i) { members.back().push_back(i); ++visible; }
        }
        t_size playing_playlist=pfc::infinite_size, playing_item=pfc::infinite_size;
        auto pm=playlist_manager::get();
        auto_item_=play_control::get()->is_playing() && pm->get_playing_item_location(&playing_playlist,&playing_item) && playing_playlist==active_?playing_item:pfc::infinite_size;
        bool cover=false; unsigned cover_rows=0;
        for(int col:visible_columns_) if(columns_[col].ref=="Cover") {
            cover=true; cover_rows=std::max(cover_rows,unsigned((scale(columns_[col].width)+row_pixels_-1)/row_pixels_));
        }
        for(size_t g=0;g<members.size();++g) {
            if(members[g].empty()) continue;
            const auto& tracks=members[g];
            bool collapsed=grouping_.collapse_default;
            const auto saved=collapsed_.find(ids[g]); if(saved!=collapsed_.end()) collapsed=saved->second;
            if(grouping_.autocollapse) collapsed=std::find(tracks.begin(),tracks.end(),auto_item_)==tracks.end();
            if(std::find(tracks.begin(),tracks.end(),search_reveal_)!=tracks.end()) collapsed=false;
            if(grouping_.enabled) {
                modern_playlist::viewport_group group;
                group.collapsed=collapsed; group.cover=cover; group.band.first=rows_.size();
                group.band.count=collapsed?0:tracks.size();
                group.band.padding=collapsed?0:static_cast<unsigned>(std::max<size_t>(tracks.size(),std::max(grouping_.minimum_rows,cover_rows))-tracks.size())+grouping_.extra_rows;
                std::wstring* labels[]={&group.l1,&group.r1,&group.l2,&group.r2};
                for(int n=0;n<4;++n) { pfc::string8 text; items_[tracks.front()]->format_title(nullptr,text,group_labels_[n],nullptr); *labels[n]=wide(text.c_str()); }
                double seconds=0; for(auto track:tracks) seconds+=items_[track]->get_length();
                const auto total=static_cast<unsigned long long>(std::max(0.0,seconds));
                group.r2+=(group.r2.empty()?L"":L" | ")+std::to_wstring(tracks.size())+L" tracks | "+
                    std::to_wstring(total/60)+L":"+(total%60<10?L"0":L"")+std::to_wstring(total%60);
                groups_.push_back(std::move(group)); group_members_.push_back(tracks); group_ids_.push_back(ids[g]);
                if(collapsed) continue;
            }
            for(auto i:tracks) {
                modern_playlist::playlist_row<metadb_handle_ptr> row;
                row.row_index=rows_.size(); row.track_index=i; row.metadb=items_[i];
                row.group_index=positions[i].group; row.track_index_in_group=positions[i].index;
                const char* path=items_[i]->get_path();
                row.tracktype=strstr(path,"://") && _strnicmp(path,"file://",7)?modern_playlist::track_kind::stream:modern_playlist::track_kind::file;
                rows_.push_back(i); row_data_.push_back(std::move(row));
            }
        }
    }
    void update_state() {
        if (destroying_ || !list_ || pending_ || row_data_.size()!=rows_.size()) return;
        auto pm=playlist_manager::get(); pfc::list_t<t_playback_queue_item> queue; pm->queue_get_contents(queue);
        std::vector<const void*> handles; handles.reserve(items_.get_count());
        for(size_t i=0;i<items_.get_count();++i) handles.push_back(items_[i].get_ptr());
        std::vector<modern_playlist::queue_entry> entries; entries.reserve(queue.get_count());
        for(size_t i=0;i<queue.get_count();++i) entries.push_back({queue[i].m_playlist,queue[i].m_item,queue[i].m_handle.get_ptr()});
        const auto positions=modern_playlist::queue_positions(active_,handles,entries);
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        const bool playing=play_control::get()->is_playing() && pm->get_playing_item_location(&playlist,&item) && playlist==active_;
        const t_size next_auto=playing?item:pfc::infinite_size;
        if(grouping_.enabled && grouping_.autocollapse && next_auto!=auto_item_) { refresh(); return; }
        const bool paused=play_control::get()->is_paused(); int playing_row=-1;
        const std::vector<size_t> empty;
        for (size_t i=0;i<row_data_.size();++i) {
            auto& row=row_data_[i]; const auto found=positions.find(row.track_index);
            const auto& indices=found==positions.end()?empty:found->second;
            const bool now=playing && row.track_index==item;
            if (row.queue_positions!=indices || row.playing!=now || row.paused!=(now && paused)) {
                row.queue_positions=indices; row.playing=now; row.paused=now && paused;
                modern_playlist::invalidate_playlist_row(list_,static_cast<int>(i));
            }
            if(now) playing_row=static_cast<int>(i);
        }
        if (std::none_of(visible_columns_.begin(),visible_columns_.end(),[&](int col){return columns_[col].state;})) playing_row=-1;
        modern_playlist::set_playlist_playback(list_,playing_row,paused);
    }
    void show_now_playing(bool activate) {
        t_size playlist=pfc::infinite_size, item=pfc::infinite_size;
        auto pm=playlist_manager::get();
        if (!play_control::get()->is_playing() || !pm->get_playing_item_location(&playlist,&item)) return;
        if (playlist!=active_) { if(activate) pm->set_active_playlist(playlist); return; }
        for(size_t g=0;g<groups_.size();++g) if(groups_[g].collapsed &&
            std::find(group_members_[g].begin(),group_members_[g].end(),item)!=group_members_[g].end()) {
            collapsed_[group_ids_[g]]=false; refresh(); break;
        }
        const auto row=std::lower_bound(rows_.begin(),rows_.end(),item);
        if (row!=rows_.end() && *row==item) ListView_EnsureVisible(list_,static_cast<int>(row-rows_.begin()),FALSE);
    }
    void default_action() {
        if (!core_.enqueue_on_double_click) { play(); return; }
        const int row=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (!pending_ && row>=0 && static_cast<size_t>(row)<rows_.size()) playlist_manager::get()->queue_add_item_playlist(active_,rows_[row]);
    }
    void edit_core_settings() {
        auto edited=core_;
        if (DialogBoxParamW(core_api::get_my_instance(),MAKEINTRESOURCEW(IDD_PLAYLIST_CORE),hwnd_,core_dialog,reinterpret_cast<LPARAM>(&edited))!=IDOK) return;
        core_=std::move(edited); compile_columns(); theme(); layout(); update_state();
    }
    void sync_host_selection(t_size playlist, const bit_array* affected = nullptr, const bit_array* state = nullptr) {
        if (destroying_ || rebuilding_ || pending_ || playlist!=active_) return;
        ++content_epoch_;
        auto pm=playlist_manager::get();
        const auto focused=pm->playlist_get_focus_item(active_);
        const auto position=std::lower_bound(rows_.begin(),rows_.end(),focused);
        const int focus=position!=rows_.end() && *position==focused ? static_cast<int>(position-rows_.begin()) : -1;
        rebuilding_=true;
        if (affected && state) for (size_t row=0;row<rows_.size();++row) if ((*affected)[rows_[row]]) {
            ListView_SetItemState(list_,static_cast<int>(row),(*state)[rows_[row]]?LVIS_SELECTED:0,LVIS_SELECTED);
        }
        const int old=ListView_GetNextItem(list_,-1,LVNI_FOCUSED);
        if (old!=focus) {
            if (old>=0) ListView_SetItemState(list_,old,0,LVIS_FOCUSED);
            if (focus>=0) ListView_SetItemState(list_,focus,LVIS_FOCUSED,LVIS_FOCUSED);
        }
        rebuilding_=false;
    }
    void invalidate_metadata(t_size playlist,const bit_array& mask) {
        if (destroying_ || !list_ || pending_ || playlist!=active_) return;
        modern_playlist::invalidate_playlist_rows(list_,[&](int row) { return mask[rows_[row]]; });
    }
    void on_items_added(t_size playlist,t_size,metadb_handle_list_cref,const bit_array&) override { if (playlist==active_) schedule(); }
    void on_items_reordered(t_size playlist,const t_size*,t_size) override { if (playlist==active_) schedule(); }
    void on_items_removed(t_size playlist,const bit_array&,t_size,t_size) override { if (playlist==active_) schedule(); }
    void on_items_selection_change(t_size playlist,const bit_array& affected,const bit_array& state) override { sync_host_selection(playlist,&affected,&state); }
    void on_item_focus_change(t_size playlist,t_size,t_size) override { sync_host_selection(playlist); }
    void on_items_modified(t_size playlist,const bit_array&) override {
        if (destroying_ || pending_ || playlist!=active_) return;
        // Tag changes can alter filter membership or contiguous album-group parity.
        if (grouping_.enabled || !applied_query_.empty()) { covers_.clear(); schedule(); }
        else { build_rows(); update_state(); modern_playlist::invalidate_playlist_row(list_,-1); }
    }
    void on_items_modified_fromplayback(t_size playlist,const bit_array& mask,play_control::t_display_level) override {
        invalidate_metadata(playlist,mask);
    }
    void on_items_replaced(t_size playlist,const bit_array&,const pfc::list_base_const_t<t_on_items_replaced_entry>&) override { if (playlist==active_) schedule(); }
    void on_playlist_locked(t_size,bool) override { schedule(); InvalidateRect(tabs_,nullptr,FALSE); }
    void on_playlist_activate(t_size,t_size) override { schedule(); }
    void on_playlist_created(t_size index,const char*,t_size) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        if (index<=queries_.size()) queries_.insert(queries_.begin()+index,L""); active_=pfc::infinite_size; schedule();
    }
    void on_playlists_reorder(const t_size* order,t_size count) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        auto old=queries_; queries_.resize(count);
        for(t_size i=0;i<count;++i) queries_[i]=order[i]<old.size()?old[order[i]]:L"";
        active_=pfc::infinite_size; schedule();
    }
    void on_playlists_removed(const bit_array& mask,t_size old_count,t_size) override {
        ++playlist_epoch_;
        tabs_dirty_=true;
        std::vector<std::wstring> remaining;
        for(t_size i=0;i<old_count;++i) if(!mask[i]) remaining.push_back(i<queries_.size()?queries_[i]:L"");
        queries_=std::move(remaining); active_=pfc::infinite_size; schedule();
    }
    void on_playlist_renamed(t_size,const char*,t_size) override { ++playlist_epoch_; tabs_dirty_=true; apply_filter_next_=true; schedule(); }
};

class playlist_element : public ui_element {
public:
    GUID get_guid() override { return element_id; }
    GUID get_subclass() override { return ui_element_subclass_playlist_renderers; }
    void get_name(pfc::string_base& out) override { out="Modern Playlist"; }
    bool get_description(pfc::string_base& out) override { out="Playlist tabs, search and configurable title-format columns."; return true; }
    ui_element_instance::ptr instantiate(HWND parent,ui_element_config::ptr cfg,ui_element_instance_callback_ptr cb) override {
        return new service_impl_t<playlist_view>(parent,cfg,cb);
    }
    ui_element_config::ptr get_default_configuration() override { return ui_element_config::g_create_empty(element_id); }
    ui_element_children_enumerator::ptr enumerate_children(ui_element_config::ptr) override { return nullptr; }
};
service_factory_single_t<playlist_element> element_factory;
} // namespace

namespace modern_playlist {
ui_element_instance::ptr create_columns_view(HWND parent, ui_element_config::ptr config) {
    return new service_impl_t<playlist_view>(parent, config, nullptr, false);
}
}
