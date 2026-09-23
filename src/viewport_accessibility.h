#pragma once
#include <windows.h>
#include "playlist_viewport.h"
#include <oleacc.h>
#include <commctrl.h>
#include <atomic>
#include <algorithm>
#include <new>
#include <utility>
#include <string>
#include <vector>

namespace modern_playlist {
inline constexpr UINT viewport_access_action = WM_APP + 205;
class selection_enumerator final : public IEnumVARIANT {
    std::atomic<ULONG> refs_{1};
    std::vector<LONG> rows_; size_t position_=0;
public:
    explicit selection_enumerator(std::vector<LONG> rows,size_t position=0) : rows_(std::move(rows)),position_(position) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if (!out) return E_POINTER; *out=nullptr;
        if (id!=IID_IUnknown && id!=IID_IEnumVARIANT) return E_NOINTERFACE;
        *out=static_cast<IEnumVARIANT*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto n=--refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE Next(ULONG count,VARIANT* values,ULONG* fetched) override {
        if (!values || (!fetched && count!=1)) return E_POINTER;
        ULONG n=0; while(n<count && position_<rows_.size()) { VariantInit(&values[n]); values[n].vt=VT_I4; values[n++].lVal=rows_[position_++]; }
        if(fetched) *fetched=n; return n==count?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) override { const size_t old=position_; position_+=std::min(rows_.size()-position_,static_cast<size_t>(count)); return position_-old==count?S_OK:S_FALSE; }
    HRESULT STDMETHODCALLTYPE Reset() override { position_=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT** out) override {
        if(!out) return E_POINTER; *out=new(std::nothrow) selection_enumerator(rows_,position_); return *out?S_OK:E_OUTOFMEMORY;
    }
};
// MSAA preserves screen-reader names, selection, focus, hit testing, and default
// actions after removing the native list control. The owner disconnects it before
// HWND destruction; clients may retain COM references beyond the panel lifetime.
class viewport_accessibility final : public IAccessible {
    std::atomic<ULONG> refs_{1}; HWND window_; bool tabs_=false;
    bool valid(VARIANT child) const { return window_ && child.vt==VT_I4 && child.lVal>=0 && child.lVal<=ListView_GetItemCount(window_); }
    static HRESULT string(const wchar_t* value,BSTR* out) { if(!out) return E_POINTER; *out=SysAllocString(value); return *out?S_OK:E_OUTOFMEMORY; }
public:
    explicit viewport_accessibility(HWND window,bool tabs=false):window_(window),tabs_(tabs) {}
    void disconnect() { window_=nullptr; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out) return E_POINTER; *out=nullptr;
        if(id!=IID_IUnknown && id!=IID_IDispatch && id!=IID_IAccessible) return E_NOINTERFACE;
        *out=static_cast<IAccessible*>(this); AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n=--refs_; if(!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* out) override { if(!out) return E_POINTER; *out=0; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT,LCID,ITypeInfo** out) override { if(out) *out=nullptr; return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID,LPOLESTR*,UINT,LCID,DISPID*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Invoke(DISPID,REFIID,LCID,WORD,DISPPARAMS*,VARIANT*,EXCEPINFO*,UINT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** out) override {
        if(!out) return E_POINTER; *out=nullptr; if(!window_) return CO_E_OBJNOTCONNECTED;
        return AccessibleObjectFromWindow(GetParent(window_),static_cast<DWORD>(OBJID_CLIENT),IID_IDispatch,reinterpret_cast<void**>(out));
    }
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* out) override { if(!out) return E_POINTER; *out=window_?ListView_GetItemCount(window_):0; return window_?S_OK:CO_E_OBJNOTCONNECTED; }
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT,IDispatch** out) override { if(!out) return E_POINTER; *out=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child,BSTR* out) override {
        if(!out) return E_POINTER; *out=nullptr; if(!valid(child)) return E_INVALIDARG;
        if(child.lVal==CHILDID_SELF) return string(tabs_?L"Playlist manager":L"Playlist",out);
        std::wstring name;
        for(int i=0;i<(tabs_?1:Header_GetItemCount(ListView_GetHeader(window_)));++i) {
            wchar_t buffer[4096]{}; LVITEMW item{}; item.iSubItem=i; item.pszText=buffer; item.cchTextMax=4096;
            SendMessageW(window_,LVM_GETITEMTEXTW,child.lVal-1,reinterpret_cast<LPARAM>(&item));
            if(buffer[0]) { if(!name.empty()) name+=L", "; name+=buffer; }
        }
        return string(name.c_str(),out);
    }
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT,BSTR* out) override { if(!out) return E_POINTER; *out=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT,BSTR* out) override { if(!out) return E_POINTER; *out=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child,VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!valid(child)) return E_INVALIDARG;
        out->vt=VT_I4; out->lVal=tabs_?(child.lVal==CHILDID_SELF?ROLE_SYSTEM_PAGETABLIST:ROLE_SYSTEM_PAGETAB):(child.lVal==CHILDID_SELF?ROLE_SYSTEM_LIST:ROLE_SYSTEM_LISTITEM); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child,VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!valid(child)) return E_INVALIDARG;
        out->vt=VT_I4; out->lVal=STATE_SYSTEM_FOCUSABLE;
        if(!IsWindowVisible(window_)) out->lVal|=STATE_SYSTEM_INVISIBLE;
        if(child.lVal==CHILDID_SELF) { if(!tabs_) out->lVal|=STATE_SYSTEM_MULTISELECTABLE|STATE_SYSTEM_EXTSELECTABLE; }
        else {
            out->lVal|=STATE_SYSTEM_SELECTABLE;
            const auto state=ListView_GetItemState(window_,child.lVal-1,LVIS_SELECTED|LVIS_FOCUSED);
            if(state&LVIS_SELECTED) out->lVal|=STATE_SYSTEM_SELECTED;
            if((state&LVIS_FOCUSED) && GetFocus()==window_) out->lVal|=STATE_SYSTEM_FOCUSED;
            RECT r{},client{},clipped{}; ListView_GetItemRect(window_,child.lVal-1,&r,LVIR_BOUNDS); GetClientRect(window_,&client);
            if(!IntersectRect(&clipped,&r,&client)) out->lVal|=STATE_SYSTEM_OFFSCREEN;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT,BSTR* out) override { if(!out) return E_POINTER; *out=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* out,VARIANT,long* topic) override { if(out) *out=nullptr; if(topic) *topic=-1; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT,BSTR* out) override { if(!out) return E_POINTER; *out=nullptr; return S_FALSE; }
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!window_) return CO_E_OBJNOTCONNECTED;
        if(GetFocus()!=window_) return S_FALSE;
        out->vt=VT_I4; out->lVal=ListView_GetNextItem(window_,-1,LVNI_FOCUSED)+1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!window_) return CO_E_OBJNOTCONNECTED;
        std::vector<LONG> rows; for(int row=ListView_GetNextItem(window_,-1,LVNI_SELECTED);row>=0;row=ListView_GetNextItem(window_,row,LVNI_SELECTED)) rows.push_back(row+1);
        if(rows.empty()) return S_OK;
        if(rows.size()==1) { out->vt=VT_I4; out->lVal=rows[0]; }
        else { auto* e=new(std::nothrow) selection_enumerator(std::move(rows)); if(!e) return E_OUTOFMEMORY; out->vt=VT_UNKNOWN; out->punkVal=e; }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child,BSTR* out) override {
        if(!out) return E_POINTER; *out=nullptr; if(!valid(child)) return E_INVALIDARG;
        return child.lVal?string(tabs_?L"Activate":(SendMessageW(window_,viewport_enqueue_query,0,0)?L"Add to playback queue":L"Play"),out):S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accSelect(long flags,VARIANT child) override {
        if(!valid(child) || child.lVal==CHILDID_SELF) return E_INVALIDARG;
        return SendMessageW(window_,viewport_access_action,child.lVal-1,flags)?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE accLocation(long* x,long* y,long* width,long* height,VARIANT child) override {
        if(!x || !y || !width || !height) return E_POINTER;
        if(!valid(child)) return E_INVALIDARG;
        RECT r{}; if(child.lVal) ListView_GetItemRect(window_,child.lVal-1,&r,LVIR_BOUNDS); else GetClientRect(window_,&r);
        MapWindowPoints(window_,nullptr,reinterpret_cast<POINT*>(&r),2);
        *x=r.left; *y=r.top; *width=r.right-r.left; *height=r.bottom-r.top; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accNavigate(long direction,VARIANT start,VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!valid(start)) return E_INVALIDARG;
        const int count=ListView_GetItemCount(window_); long row=start.lVal;
        switch(direction) {
        case NAVDIR_FIRSTCHILD: if(row) return S_FALSE; row=1; break;
        case NAVDIR_LASTCHILD: if(row) return S_FALSE; row=count; break;
        case NAVDIR_RIGHT: if(!tabs_) return S_FALSE; [[fallthrough]];
        case NAVDIR_NEXT: case NAVDIR_DOWN: if(!row) return S_FALSE; ++row; break;
        case NAVDIR_LEFT: if(!tabs_) return S_FALSE; [[fallthrough]];
        case NAVDIR_PREVIOUS: case NAVDIR_UP: if(!row) return S_FALSE; --row; break;
        default: return S_FALSE;
        }
        if(row<1 || row>count) return S_FALSE; out->vt=VT_I4; out->lVal=row; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accHitTest(long x,long y,VARIANT* out) override {
        if(!out) return E_POINTER; VariantInit(out); if(!window_) return CO_E_OBJNOTCONNECTED;
        LVHITTESTINFO hit{}; hit.pt={x,y}; ScreenToClient(window_,&hit.pt); RECT r{}; GetClientRect(window_,&r);
        if(!PtInRect(&r,hit.pt)) return S_FALSE;
        out->vt=VT_I4; out->lVal=ListView_HitTest(window_,&hit)+1; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) override {
        if(!valid(child) || child.lVal==CHILDID_SELF) return E_INVALIDARG;
        return SendMessageW(window_,viewport_access_action,child.lVal-1,-1)?S_OK:S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT,BSTR) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT,BSTR) override { return E_NOTIMPL; }
};
}
