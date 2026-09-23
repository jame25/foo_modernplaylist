#include "playlist_viewport.h"
#include "scroll_model.h"
#include "playlist_core.h"
#include "viewport_accessibility.h"
#include <commctrl.h>
#include <windowsx.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <climits>
#include <map>
#include <string>
#include <vector>

namespace modern_playlist {
namespace {
using Microsoft::WRL::ComPtr;
constexpr UINT_PTR frame_timer = 41;
constexpr UINT_PTR dirty_timer = 42, playback_timer = 43;
constexpr UINT search_message = WM_APP + 210;
constexpr UINT groups_message = WM_APP + 209;
constexpr UINT playback_message = WM_APP + 207;
constexpr UINT style_message = WM_APP + 201;
constexpr UINT invalidate_row_message = WM_APP + 202;
constexpr UINT reset_scroll_message = WM_APP + 203;
constexpr UINT suspend_input_message = WM_APP + 204;
constexpr UINT invalidate_rows_message = WM_APP + 206;

class viewport {
    HWND window_ = nullptr, header_ = nullptr, tooltip_ = nullptr;
    int hover_row_ = -1, playing_row_ = -1;
    bool paused_ = false, play_phase_ = false, play_timer_running_ = false;
    std::wstring tooltip_text_;
    HFONT extra_font_ = nullptr;
    ComPtr<viewport_accessibility> accessible_;
    HFONT font_ = nullptr; // Borrowed from the panel, replaced before it is deleted.
    viewport_style style_;
    viewport_search search_;
    scroll_model scroll_;
    std::vector<unsigned char> selected_;
    std::vector<viewport_group> groups_;
    group_geometry group_layout_;
    size_t slot(int row) const { return row>=0 && size_t(row)<group_layout_.track_slots.size()?group_layout_.track_slots[row]:0; }
    size_t visual_count() const { return group_layout_.slots.size(); }
    int focus_ = -1, anchor_ = -1, horizontal_ = 0;
    int wheel_remainder_ = 0, horizontal_remainder_ = 0;
    bool redraw_ = true, laying_out_ = false, suspended_ = false;
    bool mouse_down_ = false, drag_sent_ = false, defer_single_ = false;
    bool touching_ = false;
    POINT mouse_start_{}, touch_previous_{};
    ULONGLONG last_frame_ = 0, touch_start_ = 0;
    RECT dirty_{};
    bool dirty_pending_ = false;
    ComPtr<ID2D1Factory> factory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IDWriteFactory> text_factory_;
    ComPtr<IDWriteTextFormat> text_format_, extra_text_format_;
    struct cached_cell {
        std::wstring text, secondary;
        bool state = false;
        ComPtr<IDWriteTextLayout> layout, secondary_layout;
        int width = -1;
    };
    std::map<int, std::vector<cached_cell>> cache_;

    static COLORREF mix(COLORREF background,COLORREF foreground,unsigned alpha) {
        alpha=std::min(255U,alpha);
        return RGB((GetRValue(background)*(255-alpha)+GetRValue(foreground)*alpha)/255,
                   (GetGValue(background)*(255-alpha)+GetGValue(foreground)*alpha)/255,
                   (GetBValue(background)*(255-alpha)+GetBValue(foreground)*alpha)/255);
    }
    viewport_row_request row_info(int row) {
        viewport_row_request info; info.row=row; info.global_index=info.group_index=row;
        notify(&info.hdr,viewport_row_info); return info;
    }
    COLORREF row_background(int row,const viewport_row_request& info) const {
        const auto background=style_.alternating && alternate_row(info.global_index,info.group_index,style_.group_parity) ? style_.alternate : style_.row;
        return selected_[row] ? mix(background,style_.selection,style_.selection_alpha) : background;
    }
    COLORREF row_text(int row,COLORREF background) const {
        if (!selected_[row] || !style_.selection_alpha) return style_.text;
        if (style_.selection_alpha==255) return style_.selected_text;
        return 299*GetRValue(background)+587*GetGValue(background)+114*GetBValue(background)>=128000 ? RGB(0,0,0) : RGB(255,255,255);
    }
    void hide_tooltip() {
        hover_row_=-1;
        if (tooltip_) { TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; SendMessageW(tooltip_,TTM_TRACKACTIVATE,FALSE,reinterpret_cast<LPARAM>(&tool)); }
    }
    void track_hover(POINT pt) {
        const int row=hit(pt);
        if (row!=hover_row_) { hide_tooltip(); hover_row_=row; }
        TRACKMOUSEEVENT track{sizeof(track),TME_HOVER|TME_LEAVE,window_,style_.tooltip_delay}; TrackMouseEvent(&track);
    }
    void show_tooltip() {
        if (!search_.overlay.empty() || !style_.tooltips || suspended_ || mouse_down_ || touching_ || scroll_.moving() || hover_row_<0 || hover_row_>=count() || !selected_[hover_row_]) return;
        viewport_tooltip_request request; request.row=hover_row_; notify(&request.hdr,viewport_tooltip_info);
        if (request.text.empty()) return;
        tooltip_text_=std::move(request.text);
        if (!tooltip_) {
            tooltip_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,TOOLTIPS_CLASSW,nullptr,WS_POPUP|TTS_NOPREFIX|TTS_ALWAYSTIP,
                CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,CW_USEDEFAULT,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!tooltip_) return;
            TOOLINFOW tool{sizeof(tool)}; tool.uFlags=TTF_TRACK|TTF_ABSOLUTE; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
            SendMessageW(tooltip_,TTM_ADDTOOLW,0,reinterpret_cast<LPARAM>(&tool));
        }
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
        SendMessageW(tooltip_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),FALSE);
        SendMessageW(tooltip_,TTM_SETMAXTIPWIDTH,0,std::max(240,style_.padding*70));
        SendMessageW(tooltip_,TTM_SETTITLEW,TTI_NONE,reinterpret_cast<LPARAM>(L"Selected track"));
        SendMessageW(tooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
        POINT pt{}; GetCursorPos(&pt);
        SendMessageW(tooltip_,TTM_TRACKPOSITION,0,MAKELPARAM(pt.x+12,pt.y+20));
        if (IsWindowVisible(window_)) SendMessageW(tooltip_,TTM_TRACKACTIVATE,TRUE,reinterpret_cast<LPARAM>(&tool));
        RECT tip{}; GetWindowRect(tooltip_,&tip); MONITORINFO monitor{sizeof(monitor)};
        if (GetMonitorInfoW(MonitorFromPoint(pt,MONITOR_DEFAULTTONEAREST),&monitor)) {
            const int x=std::max(monitor.rcWork.left,std::min(tip.left,monitor.rcWork.right-(tip.right-tip.left)));
            const int y=std::max(monitor.rcWork.top,std::min(tip.top,monitor.rcWork.bottom-(tip.bottom-tip.top)));
            SetWindowPos(tooltip_,HWND_TOPMOST,x,y,0,0,SWP_NOSIZE|SWP_NOACTIVATE);
        }
    }
    void refresh_tooltip(int row) {
        if (!tooltip_ || (row>=0 && row!=hover_row_) || hover_row_<0 ||
            hover_row_>=count() || !selected_[hover_row_]) return;
        viewport_tooltip_request request; request.row=hover_row_; notify(&request.hdr,viewport_tooltip_info);
        if (request.text.empty()) { hide_tooltip(); return; }
        if (request.text==tooltip_text_) return;
        tooltip_text_=std::move(request.text);
        TOOLINFOW tool{sizeof(tool)}; tool.hwnd=window_; tool.uId=1; tool.lpszText=tooltip_text_.data();
        SendMessageW(tooltip_,TTM_UPDATETIPTEXTW,0,reinterpret_cast<LPARAM>(&tool));
    }
    void playback_clock() {
        const auto range=scroll_.visible(visual_count(),style_.row_height,page());
        const bool needed=!suspended_ && !paused_ && playing_row_>=0 && slot(playing_row_)>=range.first && slot(playing_row_)<range.second && IsWindowVisible(window_);
        if (needed && !play_timer_running_) play_timer_running_=SetTimer(window_,playback_timer,350,nullptr)!=0;
        if (!needed && play_timer_running_) { KillTimer(window_,playback_timer); play_timer_running_=false; play_phase_=false; }
    }
    RECT client() const { RECT r{}; GetClientRect(window_, &r); return r; }
    RECT body() const { auto r = client(); r.top = std::min(r.bottom, LONG(style_.header_height)); return r; }
    int page() const { const auto r = body(); return std::max(0L, r.bottom - r.top); }
    int count() const { return static_cast<int>(selected_.size()); }
    static D2D1_COLOR_F color(COLORREF c) { return D2D1::ColorF(GetRValue(c)/255.f, GetGValue(c)/255.f, GetBValue(c)/255.f); }
    void invalidate_body() { if (redraw_) { const auto r = body(); InvalidateRect(window_, &r, FALSE); } }
    RECT row_rect(int row) const {
        auto r = body();
        r.top = static_cast<LONG>(std::clamp(std::floor(style_.header_height + double(slot(row))*style_.row_height - scroll_.displayed),
            double(LONG_MIN), double(LONG_MAX-style_.row_height-1)));
        r.bottom = static_cast<LONG>(std::clamp(std::ceil(style_.header_height + double(slot(row)+1)*style_.row_height - scroll_.displayed),
            double(LONG_MIN), double(LONG_MAX)));
        return r;
    }
    void invalidate_row(int row) {
        if (!redraw_ || row < 0 || row >= count()) return;
        const auto bounds = body(), row_bounds = row_rect(row);
        RECT clipped{};
        if (IntersectRect(&clipped, &bounds, &row_bounds)) InvalidateRect(window_, &clipped, FALSE);
    }
    // Async metadata/cover producers use this path: one invalidation per frame.
    void queue_dirty(int row) {
        refresh_tooltip(row);
        if (row < 0) { cache_.clear(); dirty_ = body(); }
        else {
            cache_.erase(row);
            const auto r = row_rect(row), bounds = body(); RECT clipped{};
            if (!IntersectRect(&clipped, &r, &bounds)) return;
            if (dirty_pending_) { RECT joined{}; UnionRect(&joined, &dirty_, &clipped); dirty_ = joined; }
            else dirty_ = clipped;
        }
        if (!dirty_pending_ && !SetTimer(window_, dirty_timer, 16, nullptr)) {
            if (redraw_) InvalidateRect(window_, &dirty_, FALSE);
            return;
        }
        dirty_pending_ = true;
    }
    LRESULT notify(NMHDR* hdr, UINT code) {
        hdr->hwndFrom = window_; hdr->idFrom = GetDlgCtrlID(window_); hdr->code = code;
        return SendMessageW(GetParent(window_), WM_NOTIFY, hdr->idFrom, reinterpret_cast<LPARAM>(hdr));
    }
    void selection_changed() {
        hide_tooltip();
        if (!redraw_) return;
        NMLISTVIEW change{}; change.iItem = -1; change.uChanged = LVIF_STATE;
        notify(&change.hdr, LVN_ITEMCHANGED);
        if (accessible_) NotifyWinEvent(EVENT_OBJECT_SELECTIONWITHIN, window_, OBJID_CLIENT, CHILDID_SELF);
    }
    void focus(int row) {
        const int old = focus_; focus_ = row;
        invalidate_row(old); invalidate_row(focus_);
        if (accessible_ && redraw_ && row>=0) NotifyWinEvent(EVENT_OBJECT_FOCUS,window_,OBJID_CLIENT,row+1);
    }
    void select(int row, bool control, bool shift) {
        if (row < 0 || row >= count()) return;
        int first_dirty=count(), last_dirty=-1;
        auto set = [&](int i, bool value) {
            if ((selected_[i]!=0)==value) return;
            selected_[i]=value; first_dirty=std::min(first_dirty,i); last_dirty=std::max(last_dirty,i);
        };
        if (shift) {
            if (anchor_ < 0) anchor_ = focus_ >= 0 ? focus_ : row;
            const int first = std::min(anchor_, row), last = std::max(anchor_, row);
            for (int i = 0; i < count(); ++i) set(i,(i>=first && i<=last) || (control && selected_[i]));
        } else {
            for (int i = 0; i < count(); ++i) set(i,i==row ? (control ? !selected_[i] : true) : (control && selected_[i]));
            anchor_ = row;
        }
        if (last_dirty>=first_dirty && redraw_) {
            RECT changed=row_rect(first_dirty), last=row_rect(last_dirty), clipped{}, bounds=body();
            changed.bottom=last.bottom;
            if (IntersectRect(&clipped,&changed,&bounds)) InvalidateRect(window_,&clipped,FALSE);
        }
        focus(row); selection_changed();
    }
    int hit(POINT point) const {
        const auto r = body();
        if (point.x < r.left || point.x >= r.right) return -1;
        const int visual=scroll_.hit(point.y-r.top,style_.row_height,r.bottom-r.top,visual_count());
        return visual>=0?group_layout_.slots[visual].track:-1;
    }
    int content_width() const {
        int width = 0;
        for (int i = 0; i < Header_GetItemCount(header_); ++i) {
            HDITEMW item{}; item.mask = HDI_WIDTH; Header_GetItem(header_, i, &item); width += item.cxy;
        }
        return width;
    }
    void sync_scrollbar() {
        // Scale only when pixel extent exceeds Win32's signed scrollbar range.
        const double content = std::max(1.0, double(visual_count()) * style_.row_height);
        const double units = std::min(content, double(INT_MAX));
        SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
        info.nMin = 0; info.nMax = static_cast<int>(units) - 1;
        info.nPage = static_cast<UINT>(std::min(units, std::max(1.0, std::floor(page()*units/content))));
        const int range = std::max(0, info.nMax - int(info.nPage) + 1);
        info.nPos = scroll_.maximum ? static_cast<int>(std::round(scroll_.target / scroll_.maximum * range)) : 0;
        SetScrollInfo(window_, SB_VERT, &info, TRUE);
    }
    void layout() {
        if (laying_out_ || !header_) return;
        laying_out_ = true;
        for (int pass = 0; pass < 3; ++pass) {
            auto r = client();
            const int width = content_width();
            horizontal_ = std::clamp(horizontal_, 0, std::max(0, width-int(r.right)));
            SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
            info.nMax = std::max(0, width - 1); info.nPage = std::max(1L, r.right); info.nPos = horizontal_;
            SetScrollInfo(window_, SB_HORZ, &info, TRUE);
            scroll_.extent(double(visual_count()) * style_.row_height, page());
            sync_scrollbar();
        }
        const auto r = client();
        SetWindowPos(header_, nullptr, -horizontal_, 0, std::max(content_width(), int(r.right)),
            style_.header_height, SWP_NOZORDER | SWP_NOACTIVATE);
        if (target_ && FAILED(target_->Resize(D2D1::SizeU(std::max(1L,r.right), std::max(1L,r.bottom))))) discard_target();
        laying_out_ = false;
    }
    void animate() {
        if (scroll_.moving()) hide_tooltip();
        sync_scrollbar();
        if (scroll_.moving() && !last_frame_) {
            last_frame_ = GetTickCount64();
            if (!SetTimer(window_, frame_timer, 16, nullptr)) {
                scroll_.reset(scroll_.target); last_frame_ = 0; invalidate_body();
            }
        }
    }
    void stop_timer() { KillTimer(window_, frame_timer); last_frame_ = 0; }
    void reveal(int row) { if (row >= 0 && row < count()) { scroll_.reveal(slot(row), style_.row_height, page()); animate(); } }
    void vertical(WPARAM wp) {
        scroll_.cancel_inertia();
        switch (LOWORD(wp)) {
        case SB_LINEUP: scroll_.by(-style_.row_height); break;
        case SB_LINEDOWN: scroll_.by(style_.row_height); break;
        case SB_PAGEUP: scroll_.by(-page()); break;
        case SB_PAGEDOWN: scroll_.by(page()); break;
        case SB_TOP: scroll_.to(0); break;
        case SB_BOTTOM: scroll_.to(scroll_.maximum); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(window_, SB_VERT, &info);
            const int range = std::max(1, info.nMax-int(info.nPage)+1);
            scroll_.to(double(info.nTrackPos) / range * scroll_.maximum); break;
        }
        }
        animate();
    }
    void horizontal(WPARAM wp) {
        hide_tooltip();
        switch (LOWORD(wp)) {
        case SB_LINELEFT: horizontal_ -= style_.row_height; break;
        case SB_LINERIGHT: horizontal_ += style_.row_height; break;
        case SB_PAGELEFT: horizontal_ -= client().right; break;
        case SB_PAGERIGHT: horizontal_ += client().right; break;
        case SB_LEFT: horizontal_ = 0; break;
        case SB_RIGHT: horizontal_ = content_width(); break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: {
            SCROLLINFO info{sizeof(info), SIF_TRACKPOS}; GetScrollInfo(window_, SB_HORZ, &info); horizontal_ = info.nTrackPos; break;
        }
        }
        layout(); invalidate_body();
    }
    void discard_target() { brush_.Reset(); target_.Reset(); }
    void reset_text() { cache_.clear(); text_format_.Reset(); extra_text_format_.Reset(); hide_tooltip(); }
    bool resources() {
        if (!factory_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, factory_.GetAddressOf()))) return false;
        if (!text_factory_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(text_factory_.GetAddressOf())))) return false;
        if (!target_) {
            const auto r = client();
            // Retained contents allow genuine dirty-rectangle paints.
            auto props = D2D1::RenderTargetProperties(); props.dpiX = props.dpiY = 96;
            if (FAILED(factory_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(window_,
                D2D1::SizeU(std::max(1L,r.right),std::max(1L,r.bottom)), D2D1_PRESENT_OPTIONS_RETAIN_CONTENTS), &target_))) return false;
            if (FAILED(target_->CreateSolidColorBrush(color(style_.text), &brush_))) { discard_target(); return false; }
        }
        if (!text_format_) {
            LOGFONTW lf{}; GetObjectW(font_, sizeof(lf), &lf);
            if (FAILED(text_factory_->CreateTextFormat(lf.lfFaceName[0] ? lf.lfFaceName : L"Segoe UI", nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(lf.lfWeight ? lf.lfWeight : FW_NORMAL),
                lf.lfItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                float(std::max(1L,std::abs(lf.lfHeight))), L"", &text_format_))) return false;
            text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            if (FAILED(text_factory_->CreateTextFormat(lf.lfFaceName[0]?lf.lfFaceName:L"Segoe UI",nullptr,
                static_cast<DWRITE_FONT_WEIGHT>(lf.lfWeight?lf.lfWeight:FW_NORMAL),lf.lfItalic?DWRITE_FONT_STYLE_ITALIC:DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,float(std::max(1L,std::abs(lf.lfHeight)))*.9f,L"",&extra_text_format_))) { text_format_.Reset(); return false; }
            extra_text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            extra_text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        return true;
    }
    std::vector<cached_cell>& cells(int row) {
        auto found = cache_.find(row);
        if (found != cache_.end()) return found->second;
        std::vector<cached_cell> result(Header_GetItemCount(header_));
        for (int col = 0; col < static_cast<int>(result.size()); ++col) {
            NMLVDISPINFOW request{}; request.item.mask = LVIF_TEXT;
            request.item.iItem = row; request.item.iSubItem = col;
            notify(&request.hdr, LVN_GETDISPINFOW);
            if (request.item.pszText) result[col].text = request.item.pszText;
            viewport_cell_request extra; extra.row=row; extra.column=col; notify(&extra.hdr,viewport_cell_info);
            result[col].secondary=std::move(extra.secondary); result[col].state=extra.state_column;
        }
        return cache_.emplace(row, std::move(result)).first->second;
    }
    struct column_geometry { RECT rect; int align; };
    std::vector<column_geometry> geometry() const {
        std::vector<column_geometry> result;
        for (int i = 0; i < Header_GetItemCount(header_); ++i) {
            RECT r{}; Header_GetItemRect(header_, i, &r); OffsetRect(&r, -horizontal_, 0);
            HDITEMW item{}; item.mask = HDI_FORMAT; Header_GetItem(header_, i, &item);
            result.push_back({r, item.fmt & HDF_JUSTIFYMASK});
        }
        return result;
    }
    void prune_cache(int first, int end) {
        const int margin = std::max(8, end-first);
        for (auto it = cache_.begin(); it != cache_.end();) {
            if (it->first < first-margin || it->first >= end+margin) it = cache_.erase(it);
            else ++it;
        }
    }
    void state_label(cached_cell& value,const viewport_row_request& info) {
        if (!value.state) return;
        std::wstring label=info.playing ? (info.paused?L"Ⅱ":L"▶") : L"";
        if (!info.queue.empty()) { if(!label.empty()) label+=L" "; label+=info.queue; }
        if (label!=value.text) { value.text=std::move(label); value.layout.Reset(); }
    }
    void draw_line(const std::wstring& text,ComPtr<IDWriteTextLayout>& layout,IDWriteTextFormat* format,
                   int width,int height,int alignment,float x,float y,COLORREF foreground,bool highlight=true) {
        if (!layout && SUCCEEDED(text_factory_->CreateTextLayout(text.c_str(),static_cast<UINT32>(text.size()),format,float(width),float(height),&layout))) {
            layout->SetTextAlignment(alignment==HDF_RIGHT?DWRITE_TEXT_ALIGNMENT_TRAILING:alignment==HDF_CENTER?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING);
            ComPtr<IDWriteInlineObject> ellipsis;
            if (SUCCEEDED(text_factory_->CreateEllipsisTrimmingSign(format,&ellipsis))) {
                DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER,0,0}; layout->SetTrimming(&trim,ellipsis.Get());
            }
        }
        if(layout) {
            // Layouts outlive render targets: never retain a target-owned brush in a layout.
            layout->SetDrawingEffect(nullptr,{0,static_cast<UINT32>(text.size())});
            if(highlight) for(const auto& match:search_matches(text,search_.terms)) {
                UINT32 count=0;
                layout->HitTestTextRange(UINT32(match.start),UINT32(match.length),x,y,nullptr,0,&count);
                std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
                if(count && SUCCEEDED(layout->HitTestTextRange(UINT32(match.start),UINT32(match.length),x,y,metrics.data(),count,&count))) {
                    brush_->SetColor(color(search_.color));
                    for(const auto& rect:metrics) if(!rect.isTrimmed && rect.isText) {
                        const float left=std::max(x,rect.left),right=std::min(x+width,rect.left+rect.width);
                        const float top=std::max(y,rect.top),bottom=std::min(y+height,rect.top+rect.height);
                        if(right>left && bottom>top) target_->FillRectangle(D2D1::RectF(left,top,right,bottom),brush_.Get());
                    }
                }
            }
        }
        ComPtr<ID2D1SolidColorBrush> highlighted;
        if(layout && highlight && !search_.terms.empty() && SUCCEEDED(target_->CreateSolidColorBrush(color(highlight_text()),&highlighted)))
            for(const auto& match:search_matches(text,search_.terms)) layout->SetDrawingEffect(highlighted.Get(),{UINT32(match.start),UINT32(match.length)});
        brush_->SetColor(color(foreground));
        if (layout) target_->DrawTextLayout(D2D1::Point2F(x,y),layout.Get(),brush_.Get(),D2D1_DRAW_TEXT_OPTIONS_CLIP|D2D1_DRAW_TEXT_OPTIONS_NO_SNAP);
        if(layout) layout->SetDrawingEffect(nullptr,{0,static_cast<UINT32>(text.size())});
    }
    COLORREF highlight_text() const {
        const auto c=search_.color;
        return 299*GetRValue(c)+587*GetGValue(c)+114*GetBValue(c)>=128000?RGB(0,0,0):RGB(255,255,255);
    }
    void draw_gdi_line(HDC dc,const std::wstring& text,RECT rect,UINT flags,bool highlight=true) {
        // Ask GDI for the actual ellipsized string so hidden matches and the
        // ellipsis itself are never highlighted. Extra capacity is required by GDI.
        std::vector<wchar_t> display(text.begin(),text.end()); display.resize(text.size()+5,0);
        RECT measured=rect;
        DrawTextExW(dc,display.data(),int(text.size()),&measured,flags|DT_MODIFYSTRING,nullptr);
        if(!highlight || search_.terms.empty()) return;
        std::wstring shown(display.data());
        size_t visible=shown.size();
        if(shown!=text && visible>=3 && shown.substr(visible-3)==L"...") visible-=3;
        SIZE size{}; GetTextExtentPoint32W(dc,shown.c_str(),int(shown.size()),&size);
        const int x=(flags&DT_RIGHT)?rect.right-size.cx:(flags&DT_CENTER)?rect.left+(rect.right-rect.left-size.cx)/2:rect.left;
        const auto foreground=GetTextColor(dc);
        for(const auto& match:search_matches(shown.substr(0,visible),search_.terms)) {
            SIZE start{},end{};
            GetTextExtentPoint32W(dc,shown.c_str(),int(match.start),&start);
            GetTextExtentPoint32W(dc,shown.c_str(),int(match.start+match.length),&end);
            RECT mark{x+start.cx,rect.top+(rect.bottom-rect.top-size.cy)/2,x+end.cx,rect.top+(rect.bottom-rect.top+size.cy)/2};
            RECT clipped{}; if(!IntersectRect(&clipped,&mark,&rect)) continue;
            const int saved=SaveDC(dc); IntersectClipRect(dc,clipped.left,clipped.top,clipped.right,clipped.bottom);
            SetDCBrushColor(dc,search_.color); FillRect(dc,&clipped,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            SetTextColor(dc,highlight_text());
            DrawTextW(dc,text.c_str(),int(text.size()),&rect,flags);
            RestoreDC(dc,saved);
        }
        SetTextColor(dc,foreground);
    }
    void draw_search_overlay(HDC dc) {
        if(search_.overlay.empty()) return;
        const auto bounds=body(); const int pad=std::max(4,style_.padding*2);
        const int height=std::min(int(bounds.bottom-bounds.top),std::max(40,style_.row_height*2));
        RECT rect{bounds.left+pad,(bounds.top+bounds.bottom-height)/2,bounds.right-pad,(bounds.top+bounds.bottom+height)/2};
        if(rect.right<=rect.left || height<=0) return;
        const std::wstring label=search_.overlay+(search_.found?L"":L" — No match");
        const auto bg=mix(style_.row,style_.text,25);
        LOGFONTW lf{}; GetObjectW(font_,sizeof(lf),&lf); lf.lfHeight=-std::max(20L,std::abs(lf.lfHeight)*2); lf.lfWeight=FW_SEMIBOLD;
        if(dc) {
            SetDCBrushColor(dc,bg); FillRect(dc,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            HFONT large=CreateFontIndirectW(&lf); auto old=SelectObject(dc,large?large:font_);
            SetTextColor(dc,style_.text); InflateRect(&rect,-pad,0);
            DrawTextW(dc,label.c_str(),int(label.size()),&rect,DT_CENTER|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX);
            SelectObject(dc,old); if(large) DeleteObject(large);
        } else {
            brush_->SetColor(color(bg)); target_->FillRectangle(D2D1::RectF(float(rect.left),float(rect.top),float(rect.right),float(rect.bottom)),brush_.Get());
            ComPtr<IDWriteTextFormat> format;
            if(SUCCEEDED(text_factory_->CreateTextFormat(lf.lfFaceName[0]?lf.lfFaceName:L"Segoe UI",nullptr,DWRITE_FONT_WEIGHT_SEMI_BOLD,
                DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,float(-lf.lfHeight),L"",&format))) {
                format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP); format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                ComPtr<IDWriteTextLayout> layout;
                draw_line(label,layout,format.Get(),std::max(1,int(rect.right-rect.left)-2*pad),height,HDF_CENTER,float(rect.left+pad),float(rect.top),style_.text,false);
            }
        }
    }
    void paint(HDC print_dc = nullptr) {
        PAINTSTRUCT ps{}; const HDC dc=print_dc?print_dc:BeginPaint(window_,&ps);
        struct paint_guard { HWND w; PAINTSTRUCT* p; ~paint_guard(){if(p) EndPaint(w,p);} } guard{window_,print_dc?nullptr:&ps};
        if (!redraw_) return;
        const auto bounds=body(); if(bounds.bottom<=bounds.top || bounds.right<=0) return;
        const auto columns=geometry(); const auto range=scroll_.visible(visual_count(),style_.row_height,page());
        const int first=static_cast<int>(range.first),end=static_cast<int>(range.second); prune_cache(static_cast<int>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),range.first)-group_layout_.track_slots.begin()),
            static_cast<int>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),range.second)-group_layout_.track_slots.begin())); playback_clock();
        const int primary_height=(style_.row_height*3+3)/4, secondary_top=(style_.row_height+1)/2;
        const bool fresh=!target_; bool drawn=!print_dc && resources();
        if (drawn) {
            target_->BeginDraw(); target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
            RECT dirty=fresh?bounds:ps.rcPaint; IntersectRect(&dirty,&dirty,&bounds);
            target_->PushAxisAlignedClip(D2D1::RectF(float(dirty.left),float(dirty.top),float(dirty.right),float(dirty.bottom)),D2D1_ANTIALIAS_MODE_ALIASED);
            brush_->SetColor(color(style_.row)); target_->FillRectangle(D2D1::RectF(0,float(bounds.top),float(bounds.right),float(bounds.bottom)),brush_.Get());
            for(int visual=first;visual<end;++visual) {
                const auto& entry=group_layout_.slots[visual]; const int row=entry.track;
                const float y=float(style_.header_height+double(visual)*style_.row_height-scroll_.displayed);
                if(row<0) { if(entry.line==0 || (entry.line==1 && visual==first)) draw_group(entry.group,y-entry.line*style_.row_height,nullptr); continue; }
                if(y+style_.row_height<=dirty.top || y>=dirty.bottom) continue;
                const auto info=row_info(row); const auto background=row_background(row,info),foreground=row_text(row,background);
                brush_->SetColor(color(background)); target_->FillRectangle(D2D1::RectF(0,y,float(bounds.right),y+style_.row_height),brush_.Get());
                auto& values=cells(row);
                for(size_t col=0;col<columns.size();++col) {
                    const auto& g=columns[col]; const int width=g.rect.right-g.rect.left-2*style_.padding;
                    if(width<=0 || g.rect.right<=dirty.left || g.rect.left>=dirty.right) continue;
                    auto& value=values[col]; state_label(value,info);
                    if(value.width!=width) { value.layout.Reset(); value.secondary_layout.Reset(); value.width=width; }
                    const bool extra=style_.extra_line && !value.state;
                    const float offset=value.state && info.playing && !info.paused && play_phase_?float(std::max(1,style_.padding/4)):0;
                    draw_line(value.text,value.layout,text_format_.Get(),width,extra?primary_height:style_.row_height,g.align,float(g.rect.left+style_.padding),y+offset,foreground,!value.state);
                    if(extra && !value.secondary.empty()) draw_line(value.secondary,value.secondary_layout,extra_text_format_.Get(),width,
                        style_.row_height-secondary_top,g.align,float(g.rect.left+style_.padding),y+secondary_top,
                        style_.derived_extra_color?mix(background,foreground,165):style_.secondary);
                }
                if(row==focus_ && GetFocus()==window_ && style_.focus_alpha) {
                    brush_->SetColor(color(mix(background,style_.focus,style_.focus_alpha)));
                    target_->DrawRectangle(D2D1::RectF(.5f,y+.5f,float(bounds.right)-.5f,y+style_.row_height-.5f),brush_.Get());
                }
            }
            draw_search_overlay(nullptr);
            target_->PopAxisAlignedClip(); if(FAILED(target_->EndDraw())) { discard_target(); drawn=false; invalidate_body(); }
        }
        if(!drawn) {
            HDC memory=CreateCompatibleDC(dc); HBITMAP bitmap=CreateCompatibleBitmap(dc,bounds.right,bounds.bottom);
            HGDIOBJ old_bitmap=bitmap && memory?SelectObject(memory,bitmap):nullptr; HDC out=old_bitmap?memory:dc;
            const int saved=SaveDC(out); IntersectClipRect(out,bounds.left,bounds.top,bounds.right,bounds.bottom); SetBkMode(out,TRANSPARENT);
            auto fill=[&](RECT rect,COLORREF c){SetDCBrushColor(out,c); FillRect(out,&rect,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));}; fill(bounds,style_.row);
            for(int visual=first;visual<end;++visual) {
                const auto& entry=group_layout_.slots[visual]; const int row=entry.track;
                if(row<0) { if(entry.line==0 || (entry.line==1 && visual==first)) draw_group(entry.group,float(style_.header_height+double(visual-entry.line)*style_.row_height-scroll_.displayed),out); continue; }
                auto rect=row_rect(row); const auto info=row_info(row); const auto background=row_background(row,info),foreground=row_text(row,background);
                fill(rect,background); auto& values=cells(row);
                for(size_t col=0;col<columns.size();++col) {
                    auto& value=values[col]; state_label(value,info); const bool extra=style_.extra_line && !value.state;
                    RECT cell{columns[col].rect.left+style_.padding,rect.top,columns[col].rect.right-style_.padding,extra?rect.top+primary_height:rect.bottom};
                    if(cell.right<=cell.left) continue;
                    const UINT flags=DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(columns[col].align==HDF_RIGHT?DT_RIGHT:columns[col].align==HDF_CENTER?DT_CENTER:DT_LEFT);
                    if(value.state && info.playing && !info.paused && play_phase_) OffsetRect(&cell,0,std::max(1,style_.padding/4));
                    SelectObject(out,font_); SetTextColor(out,foreground); draw_gdi_line(out,value.text,cell,flags,!value.state);
                    if(extra && !value.secondary.empty()) {
                        cell.top=rect.top+secondary_top; cell.bottom=rect.bottom; SelectObject(out,extra_font_?extra_font_:font_);
                        SetTextColor(out,style_.derived_extra_color?mix(background,foreground,165):style_.secondary);
                        draw_gdi_line(out,value.secondary,cell,flags);
                    }
                }
                if(row==focus_ && GetFocus()==window_ && style_.focus_alpha) {
                    const auto old_pen=SelectObject(out,GetStockObject(DC_PEN)), old_brush=SelectObject(out,GetStockObject(NULL_BRUSH));
                    SetDCPenColor(out,mix(background,style_.focus,style_.focus_alpha)); Rectangle(out,rect.left,rect.top,rect.right,rect.bottom);
                    SelectObject(out,old_pen); SelectObject(out,old_brush);
                }
            }
            draw_search_overlay(out);
            RestoreDC(out,saved);
            if(old_bitmap){BitBlt(dc,0,bounds.top,bounds.right,bounds.bottom-bounds.top,memory,0,bounds.top,SRCCOPY);SelectObject(memory,old_bitmap);}
            if(bitmap) DeleteObject(bitmap); if(memory) DeleteDC(memory);
        }
    }
    void draw_group(int index,float y,HDC dc) {
        const auto& g=groups_[index]; const auto bounds=body();
        const int h=style_.row_height,p=style_.padding;
        const float left=float(p+(g.cover?2*h:0)), right=float(bounds.right-p);
        const int available=std::max(0,int(right-left)), left_width=available*2/3, right_width=available-left_width;
        const auto bg=mix(style_.row,style_.text,22);
        const std::wstring lines[]={std::wstring(g.collapsed?L"\u25b8 ":L"\u25be ")+g.l1,g.r1,g.l2,g.r2};
        std::shared_ptr<cover_pixels> pixels;
        if(g.cover) {
            viewport_group_request request; request.group=index; request.load=!scroll_.moving(); notify(&request.hdr,viewport_group_cover); pixels=request.pixels;
        }
        if(dc) {
            RECT band{0,LONG(y),bounds.right,LONG(y+2*h)}; SetDCBrushColor(dc,bg);
            FillRect(dc,&band,static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
            for(int line=0;line<4;++line) {
                RECT r{LONG(left+(line%2?left_width:0)),LONG(y+(line/2)*h),LONG(line%2?right:left+left_width-p),LONG(y+(line/2+1)*h)};
                SelectObject(dc,line<2?font_:(extra_font_?extra_font_:font_));
                SetTextColor(dc,line<2?style_.text:style_.secondary);
                draw_gdi_line(dc,lines[line],r,DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS|DT_NOPREFIX|(line%2?DT_RIGHT:DT_LEFT));
            }
        } else {
            brush_->SetColor(color(bg)); target_->FillRectangle(D2D1::RectF(0,y,float(bounds.right),y+2*h),brush_.Get());
            for(int line=0;line<4;++line) {
                ComPtr<IDWriteTextLayout> layout;
                draw_line(lines[line],layout,line<2?text_format_.Get():extra_text_format_.Get(),
                    std::max(0,(line%2?right_width:left_width-p)),h,line%2?HDF_RIGHT:HDF_LEFT,
                    left+(line%2?left_width:0),y+(line/2)*h,line<2?style_.text:style_.secondary);
            }
        }
        if(g.cover) {
            const int size=2*h-2*p;
            if(pixels && pixels->width && pixels->height) {
                const float scale=std::min(float(size)/pixels->width,float(size)/pixels->height);
                const int w=int(pixels->width*scale), height=int(pixels->height*scale);
                const int x=p+(size-w)/2, top=int(y)+p+(size-height)/2;
                if(dc) {
                    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
                    info.bmiHeader.biWidth=pixels->width; info.bmiHeader.biHeight=-int(pixels->height);
                    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
                    SetStretchBltMode(dc,HALFTONE);
                    StretchDIBits(dc,x,top,w,height,0,0,pixels->width,pixels->height,pixels->bgra.data(),&info,DIB_RGB_COLORS,SRCCOPY);
                } else {
                    ComPtr<ID2D1Bitmap> bitmap;
                    if(SUCCEEDED(target_->CreateBitmap(D2D1::SizeU(pixels->width,pixels->height),pixels->bgra.data(),pixels->width*4,
                        D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_PREMULTIPLIED)),bitmap.GetAddressOf())))
                        target_->DrawBitmap(bitmap.Get(),D2D1::RectF(float(x),float(top),float(x+w),float(top+height)));
                }
            } else {
                if(dc) { RECT r{p,LONG(y)+p,p+size,LONG(y)+p+size}; SetTextColor(dc,style_.secondary); DrawTextW(dc,L"\u266b",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
                else { ComPtr<IDWriteTextLayout> layout; draw_line(L"\u266b",layout,text_format_.Get(),size,size,HDF_CENTER,float(p),y+p,style_.secondary); }
            }
        }
    }
    bool key(WPARAM key) {
        hide_tooltip();
        if (!count()) return false;
        const bool control=(GetKeyState(VK_CONTROL)&0x8000)!=0, shift=(GetKeyState(VK_SHIFT)&0x8000)!=0;
        const int step=std::max(1,page()/style_.row_height);
        int row=focus_ < 0 ? 0 : focus_;
        switch(key) {
        case VK_UP: --row; break;
        case VK_DOWN: if (focus_>=0) ++row; break;
        case VK_PRIOR: row-=step; break;
        case VK_NEXT: row+=step; break;
        case VK_HOME: row=0; break;
        case VK_END: row=count()-1; break;
        case VK_SPACE: select(row,control,shift); return true;
        default: return false;
        }
        row=std::clamp(row,0,count()-1); scroll_.cancel_inertia();
        if (control && !shift) { focus(row); selection_changed(); }
        else select(row,control,shift);
        reveal(row); return true;
    }
    LRESULT message(UINT msg, WPARAM wp, LPARAM lp) {
        switch(msg) {
        case WM_CREATE: {
            header_=CreateWindowExW(0,WC_HEADERW,L"",WS_CHILD|WS_VISIBLE|HDS_BUTTONS|HDS_DRAGDROP|HDS_FULLDRAG,
                0,0,0,0,window_,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!header_) return -1;
            GESTURECONFIG pan{GID_PAN,GC_PAN|GC_PAN_WITH_SINGLE_FINGER_VERTICALLY|GC_PAN_WITH_GUTTER,GC_PAN_WITH_INERTIA};
            SetGestureConfig(window_,0,1,&pan,sizeof(pan));
            return 0;
        }
        case WM_DESTROY:
            stop_timer(); KillTimer(window_,dirty_timer); KillTimer(window_,playback_timer);
            if (tooltip_) { DestroyWindow(tooltip_); tooltip_=nullptr; }
            if (extra_font_) { DeleteObject(extra_font_); extra_font_=nullptr; }
            if (accessible_) accessible_->disconnect();
            return 0;
        case WM_GETOBJECT:
            if (static_cast<LONG>(lp)==OBJID_CLIENT) {
                if (!accessible_) accessible_.Attach(new viewport_accessibility(window_));
                return LresultFromObject(IID_IAccessible,wp,accessible_.Get());
            }
            break;
        case viewport_access_action: {
            const int row=static_cast<int>(wp);
            if (suspended_ || row<0 || row>=count()) return FALSE;
            if (lp==-1) {
                select(row,false,false); NMITEMACTIVATE item{}; item.iItem=row; notify(&item.hdr,NM_DBLCLK);
            } else {
                if (lp&SELFLAG_TAKEFOCUS) { SetFocus(window_); focus(row); }
                if (lp&SELFLAG_TAKESELECTION) select(row,false,false);
                else if (lp&SELFLAG_EXTENDSELECTION) select(row,false,true);
                else {
                    if (lp&SELFLAG_ADDSELECTION) selected_[row]=true;
                    if (lp&SELFLAG_REMOVESELECTION) selected_[row]=false;
                    invalidate_row(row); selection_changed();
                }
                reveal(row);
            }
            return TRUE;
        }
        case viewport_enqueue_query: return style_.enqueue_default;
        case playback_message:
            invalidate_row(playing_row_); playing_row_=static_cast<int>(wp); paused_=lp!=0; play_phase_=false;
            invalidate_row(playing_row_); playback_clock(); return 0;
        case WM_GETDLGCODE: return DLGC_WANTARROWS|DLGC_WANTCHARS;
        case WM_SETFONT: {
            font_=reinterpret_cast<HFONT>(wp); reset_text(); if(extra_font_) DeleteObject(extra_font_);
            LOGFONTW lf{}; GetObjectW(font_,sizeof(lf),&lf); lf.lfHeight=MulDiv(lf.lfHeight,9,10); extra_font_=CreateFontIndirectW(&lf);
            if(lp) invalidate_body(); return 0;
        }
        case WM_GETFONT: return reinterpret_cast<LRESULT>(font_);
        case WM_SETREDRAW: redraw_=wp!=0; if (redraw_) { layout(); invalidate_body(); } return 0;
        case WM_SIZE: hide_tooltip(); layout(); invalidate_body(); return 0;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: paint(); return 0;
        case WM_PRINTCLIENT: paint(reinterpret_cast<HDC>(wp)); return 0;
        case WM_SETFOCUS: case WM_KILLFOCUS: hide_tooltip(); invalidate_row(focus_); return 0;
        case WM_SHOWWINDOW:
            if (!wp) { hide_tooltip(); scroll_.cancel_inertia(); stop_timer(); KillTimer(window_,playback_timer); play_timer_running_=false; }
            else animate();
            break;
        case WM_TIMER:
            if(wp==playback_timer) { playback_clock(); if(play_timer_running_) { play_phase_=!play_phase_; invalidate_row(playing_row_); } return 0; }
            if (wp==frame_timer) {
                const auto now=GetTickCount64(); const double elapsed=double(now-last_frame_); last_frame_=now;
                if (scroll_.tick(elapsed)) invalidate_body();
                sync_scrollbar();
                if (!scroll_.moving()) stop_timer();
                return 0;
            }
            if (wp==dirty_timer) {
                KillTimer(window_,dirty_timer);
                if (dirty_pending_ && redraw_) InvalidateRect(window_,&dirty_,FALSE);
                dirty_pending_=false; return 0;
            }
            break;
        case WM_VSCROLL: if (!suspended_) vertical(wp); return 0;
        case WM_HSCROLL: horizontal(wp); return 0;
        case WM_MOUSEWHEEL: {
            if (GET_KEYSTATE_WPARAM(wp)&MK_CONTROL) return SendMessageW(GetParent(window_),msg,wp,lp);
            if (suspended_) return 0;
            scroll_.cancel_inertia();
            UINT lines=3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES,0,&lines,0);
            // Keep sub-detent precision rather than dropping high-resolution input.
            const double distance=lines==WHEEL_PAGESCROLL ? page() : double(lines)*style_.row_height;
            const double delta=-GET_WHEEL_DELTA_WPARAM(wp)*distance/WHEEL_DELTA;
            wheel_remainder_+=static_cast<int>(std::round(delta*1000));
            scroll_.by(wheel_remainder_/1000); wheel_remainder_%=1000;
            animate(); return 0;
        }
        case WM_MOUSEHWHEEL:
            hide_tooltip();
            horizontal_remainder_+=GET_WHEEL_DELTA_WPARAM(wp)*style_.row_height;
            horizontal_+=horizontal_remainder_/WHEEL_DELTA; horizontal_remainder_%=WHEEL_DELTA;
            layout(); invalidate_body(); return 0;
        case WM_KEYDOWN: if (!suspended_ && key(wp)) return 0; break;
        case WM_LBUTTONDOWN: {
            hide_tooltip();
            if (suspended_) return 0;
            SetFocus(window_); scroll_.interrupt(style_.row_height); animate();
            mouse_start_={GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};
            const int visual=scroll_.hit(mouse_start_.y-style_.header_height,style_.row_height,page(),visual_count());
            if(visual>=0 && group_layout_.slots[visual].line>=0) {
                viewport_group_request request; request.group=group_layout_.slots[visual].group;
                notify(&request.hdr,viewport_group_toggle); return 0;
            }
            const int row=hit(mouse_start_);
            mouse_down_=row>=0; drag_sent_=false; defer_single_=false;
            if (row>=0) {
                const bool control=(wp&MK_CONTROL)!=0, shift=(wp&MK_SHIFT)!=0;
                defer_single_=selected_[row] && !control && !shift;
                if (!defer_single_) select(row,control,shift);
                else { focus(row); selection_changed(); }
                SetCapture(window_);
            } else if (!(wp&(MK_CONTROL|MK_SHIFT))) {
                std::fill(selected_.begin(),selected_.end(),static_cast<unsigned char>(0)); invalidate_body(); selection_changed();
            }
            return 0;
        }
        case WM_MOUSELEAVE: hide_tooltip(); hover_row_=-1; return 0;
        case WM_MOUSEHOVER: show_tooltip(); return 0;
        case WM_MOUSEMOVE:
            if(!mouse_down_ && !touching_) track_hover({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (mouse_down_ && !drag_sent_ && !suspended_ &&
                (std::abs(GET_X_LPARAM(lp)-mouse_start_.x)>=GetSystemMetrics(SM_CXDRAG) ||
                 std::abs(GET_Y_LPARAM(lp)-mouse_start_.y)>=GetSystemMetrics(SM_CYDRAG))) {
                drag_sent_=true; NMLISTVIEW drag{}; drag.iItem=focus_; drag.ptAction=mouse_start_; notify(&drag.hdr,LVN_BEGINDRAG);
            }
            return 0;
        case WM_LBUTTONUP:
            if (mouse_down_ && !drag_sent_ && defer_single_ && !suspended_) select(hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)}),false,false);
            mouse_down_=false; defer_single_=false; if (GetCapture()==window_) ReleaseCapture(); return 0;
        case WM_CAPTURECHANGED: mouse_down_=drag_sent_=defer_single_=false; return 0;
        case WM_CANCELMODE:
            touching_=mouse_down_=drag_sent_=false; scroll_.cancel_inertia(); if (GetCapture()==window_) ReleaseCapture(); return 0;
        case WM_RBUTTONDOWN: {
            hide_tooltip();
            if (suspended_) return 0;
            SetFocus(window_); scroll_.interrupt(style_.row_height); animate();
            const int row=hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (row>=0 && !selected_[row]) select(row,false,false);
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            if (suspended_) return 0;
            const int row=hit({GET_X_LPARAM(lp),GET_Y_LPARAM(lp)});
            if (row>=0) { focus(row); NMITEMACTIVATE item{}; item.iItem=row; notify(&item.hdr,NM_DBLCLK); }
            return 0;
        }
        case WM_GESTURE: {
            GESTUREINFO info{sizeof(info)};
            if (!GetGestureInfo(reinterpret_cast<HGESTUREINFO>(lp),&info)) break;
            if (info.dwID!=GID_PAN) break; // DefWindowProc closes unhandled gesture handles.
            if (!suspended_) {
                POINT point{info.ptsLocation.x,info.ptsLocation.y};
                if (info.dwFlags&GF_BEGIN) {
                    touching_=true; touch_previous_=point; touch_start_=GetTickCount64(); scroll_.cancel_inertia();
                    mouse_down_=drag_sent_=false;
                    // Cancel any promoted mouse drag before touch panning takes over.
                    if (GetCapture()==window_) ReleaseCapture();
                } else if (touching_) { scroll_.by(touch_previous_.y-point.y); touch_previous_=point; }
                if (info.dwFlags&GF_END) { touching_=false; scroll_.fling(double(GetTickCount64()-touch_start_)); }
                animate();
            }
            CloseGestureInfoHandle(reinterpret_cast<HGESTUREINFO>(lp)); return 0;
        }
        case WM_NOTIFY: {
            const auto* hdr=reinterpret_cast<NMHDR*>(lp);
            if (hdr->hwndFrom==header_) {
                if (hdr->code==HDN_ITEMCLICKW || hdr->code==HDN_ITEMCLICKA) {
                    if (!suspended_) { NMLISTVIEW click{}; click.iSubItem=reinterpret_cast<NMHEADERW*>(lp)->iItem; notify(&click.hdr,LVN_COLUMNCLICK); }
                    return 0;
                }
                if (hdr->code==HDN_ITEMCHANGEDW || hdr->code==HDN_ITEMCHANGEDA || hdr->code==HDN_ENDDRAG) {
                    layout(); invalidate_body();
                }
            }
            break;
        }
        // Compatibility surface for the existing panel/controller. Keeping this
        // small and explicit prevents two competing sources of scroll geometry.
        case LVM_GETHEADER: return reinterpret_cast<LRESULT>(header_);
        case LVM_SETEXTENDEDLISTVIEWSTYLE: return 0;
        case LVM_GETITEMCOUNT: return count();
        case LVM_GETITEMTEXTW: {
            const int row=static_cast<int>(wp); auto* item=reinterpret_cast<LVITEMW*>(lp);
            if (row<0 || row>=count() || item->iSubItem<0 || item->iSubItem>=Header_GetItemCount(header_) || item->cchTextMax<=0) return 0;
            // Accessibility can request offscreen names without growing the viewport cache.
            NMLVDISPINFOW request{}; request.item.mask=LVIF_TEXT; request.item.iItem=row; request.item.iSubItem=item->iSubItem;
            notify(&request.hdr,LVN_GETDISPINFOW);
            lstrcpynW(item->pszText,request.item.pszText?request.item.pszText:L"",item->cchTextMax);
            return lstrlenW(item->pszText);
        }
        case LVM_SETITEMCOUNT:
            selected_.resize(std::min<size_t>(wp,INT_MAX));
            groups_.clear(); group_layout_.build(selected_.size(),{});
            if (focus_>=count()) focus_=-1;
            if (anchor_>=count()) anchor_=-1;
            cache_.clear(); scroll_.cancel_inertia(); if(redraw_) layout(); invalidate_body(); return TRUE;
        case LVM_GETITEMSTATE: {
            const int row=static_cast<int>(wp);
            if (row<0 || row>=count()) return 0;
            return ((selected_[row]?LVIS_SELECTED:0)|(focus_==row?LVIS_FOCUSED:0))&lp;
        }
        case LVM_SETITEMSTATE: {
            const auto* item=reinterpret_cast<const LVITEMW*>(lp); const int row=static_cast<int>(wp);
            if (row < -1 || row>=count()) return FALSE;
            bool changed=false;
            const int first=row<0?0:row, end=row<0?count():row+1;
            if (item->stateMask&LVIS_SELECTED) for (int i=first;i<end;++i) {
                const bool value=(item->state&LVIS_SELECTED)!=0;
                if ((selected_[i]!=0)!=value) { selected_[i]=value; if (row>=0) invalidate_row(i); changed=true; }
            }
            if (changed && row<0) invalidate_body();
            if (item->stateMask&LVIS_FOCUSED) {
                const int old=focus_;
                if (item->state&LVIS_FOCUSED) focus(row<0?(count()?0:-1):row);
                else if (row<0 || focus_==row) focus(-1);
                changed|=old!=focus_;
            }
            if (changed) selection_changed(); return TRUE;
        }
        case LVM_GETNEXTITEM: {
            const int start=static_cast<int>(wp)+1;
            if (lp&LVNI_FOCUSED) return focus_>=start?focus_:-1;
            for (int i=std::max(0,start);i<count();++i) if (!(lp&LVNI_SELECTED) || selected_[i]) return i;
            return -1;
        }
        case LVM_GETTOPINDEX: return static_cast<LRESULT>(std::lower_bound(group_layout_.track_slots.begin(),group_layout_.track_slots.end(),size_t(std::max(0.0,std::floor(scroll_.displayed/style_.row_height))))-group_layout_.track_slots.begin());
        case LVM_GETCOUNTPERPAGE: return page()/style_.row_height;
        case LVM_GETITEMRECT: {
            const int row=static_cast<int>(wp); if (row<0 || row>=count()) return FALSE;
            *reinterpret_cast<RECT*>(lp)=row_rect(row); return TRUE;
        }
        case LVM_HITTEST: {
            auto* info=reinterpret_cast<LVHITTESTINFO*>(lp);
            info->iItem=hit(info->pt); info->flags=info->iItem>=0?LVHT_ONITEMLABEL:LVHT_NOWHERE; return info->iItem;
        }
        case LVM_ENSUREVISIBLE: reveal(static_cast<int>(wp)); return TRUE;
        case LVM_SCROLL:
            horizontal_+=static_cast<int>(wp); scroll_.by(static_cast<int>(lp)); layout(); animate(); invalidate_body(); return TRUE;
        case LVM_GETCOLUMNORDERARRAY:
            return Header_GetOrderArray(header_,static_cast<int>(wp),reinterpret_cast<int*>(lp));
        case LVM_SETCOLUMNORDERARRAY: {
            const auto result=Header_SetOrderArray(header_,static_cast<int>(wp),reinterpret_cast<int*>(lp)); invalidate_body(); return result;
        }
        case LVM_INSERTCOLUMNW: {
            const auto* col=reinterpret_cast<LVCOLUMNW*>(lp);
            HDITEMW item{}; item.mask=HDI_TEXT|HDI_WIDTH|HDI_FORMAT;
            item.pszText=col->pszText; item.cxy=col->cx; item.fmt=HDF_STRING|(col->fmt&LVCFMT_JUSTIFYMASK);
            const auto result=Header_InsertItem(header_,static_cast<int>(wp),&item);
            cache_.clear(); layout(); invalidate_body(); return result;
        }
        case LVM_DELETECOLUMN: {
            const auto result=Header_DeleteItem(header_,static_cast<int>(wp));
            cache_.clear(); layout(); invalidate_body(); return result;
        }
        case LVM_GETCOLUMNWIDTH: {
            HDITEMW item{}; item.mask=HDI_WIDTH; Header_GetItem(header_,static_cast<int>(wp),&item); return item.cxy;
        }
        case LVM_SETCOLUMNWIDTH: {
            HDITEMW item{}; item.mask=HDI_WIDTH; item.cxy=std::max(0,static_cast<int>(lp));
            const auto result=Header_SetItem(header_,static_cast<int>(wp),&item); layout(); invalidate_body(); return result;
        }
        case search_message:
            search_=*reinterpret_cast<const viewport_search*>(lp); hide_tooltip(); invalidate_body(); return 0;
        case groups_message: {
            hide_tooltip(); groups_=*reinterpret_cast<const std::vector<viewport_group>*>(lp);
            std::vector<group_band> bands; for(const auto& g:groups_) bands.push_back(g.band);
            group_layout_.build(selected_.size(),bands); cache_.clear(); if(redraw_) layout(); invalidate_body(); return 0;
        }
        case style_message: {
            const auto next=*reinterpret_cast<const viewport_style*>(lp);
            const double factor=double(next.row_height)/style_.row_height;
            scroll_.target*=factor; scroll_.displayed*=factor;
            style_=next; reset_text(); layout(); invalidate_body(); return 0;
        }
        case invalidate_row_message: queue_dirty(static_cast<int>(wp)); return 0;
        case invalidate_rows_message: {
            const auto& affected=*reinterpret_cast<const std::function<bool(int)>*>(lp);
            for (auto it=cache_.begin();it!=cache_.end();) {
                const int row=it->first; ++it;
                if (affected(row)) queue_dirty(row);
            }
            return 0;
        }
        case reset_scroll_message: hide_tooltip(); scroll_.reset(); stop_timer(); sync_scrollbar(); invalidate_body(); return 0;
        case suspend_input_message:
            suspended_=wp!=0;
            if (suspended_) {
                hide_tooltip(); hover_row_=-1;
                scroll_.cancel_inertia(); stop_timer(); touching_=mouse_down_=drag_sent_=false;
                if (GetCapture()==window_) ReleaseCapture();
                cache_.clear();
            } else animate();
            return 0;
        }
        return DefWindowProcW(window_,msg,wp,lp);
    }
public:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
        auto* self=reinterpret_cast<viewport*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        if (msg==WM_NCCREATE) {
            self=new(std::nothrow) viewport;
            if (!self) return FALSE;
            self->window_=window; SetWindowLongPtrW(window,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(window,msg,wp,lp);
        if (msg==WM_NCDESTROY) {
            SetWindowLongPtrW(window,GWLP_USERDATA,0); delete self; return DefWindowProcW(window,msg,wp,lp);
        }
        try { return self->message(msg,wp,lp); }
        catch (...) {
            // Never propagate C++ exceptions through a Win32 callback.
            self->laying_out_=false;
            if (msg==WM_CREATE) return -1;
            return 0;
        }
    }
};
}
HWND create_playlist_viewport(HWND parent, HINSTANCE instance) {
    WNDCLASSW wc{}; wc.lpfnWndProc=viewport::proc; wc.hInstance=instance;
    wc.lpszClassName=L"foo_modernplaylist.viewport"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.style=CS_DBLCLKS;
    RegisterClassW(&wc);
    return CreateWindowExW(0,wc.lpszClassName,L"Playlist",WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_CLIPCHILDREN|WS_HSCROLL|WS_VSCROLL,
        0,0,0,0,parent,nullptr,instance,nullptr);
}
void set_playlist_search(HWND w,const viewport_search& search) { SendMessageW(w,search_message,0,reinterpret_cast<LPARAM>(&search)); }
void set_playlist_groups(HWND w,const std::vector<viewport_group>& groups) { SendMessageW(w,groups_message,0,reinterpret_cast<LPARAM>(&groups)); }
void set_playlist_playback(HWND w,int row,bool paused) { SendMessageW(w,playback_message,static_cast<WPARAM>(row),paused); }
void configure_playlist_viewport(HWND w,const viewport_style& style) { SendMessageW(w,style_message,0,reinterpret_cast<LPARAM>(&style)); }
void invalidate_playlist_row(HWND w,int row) { SendMessageW(w,invalidate_row_message,static_cast<WPARAM>(row),0); }
void invalidate_playlist_rows(HWND w,const std::function<bool(int)>& affected) { SendMessageW(w,invalidate_rows_message,0,reinterpret_cast<LPARAM>(&affected)); }
void reset_playlist_scroll(HWND w) { SendMessageW(w,reset_scroll_message,0,0); }
void suspend_playlist_input(HWND w,bool suspended) { SendMessageW(w,suspend_input_message,suspended,0); }
}
