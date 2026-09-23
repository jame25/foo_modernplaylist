#include <SDK/foobar2000.h>
#include <SDK/coreDarkMode.h>
#include <commctrl.h>
#include <stdexcept>
#include "resource.h"

namespace {
constexpr GUID preferences_id = {0x6b678cb7,0xec93,0x4efb,{0x9b,0xc7,0xc1,0x6b,0x46,0x80,0xa3,0x12}};

class playlist_preferences : public preferences_page_instance {
public:
    HWND get_wnd() override { return hwnd_; }
    // General is intentionally empty; there are no values to apply or reset.
    t_uint32 get_state() override { return preferences_state::dark_mode_supported; }
    void apply() override {}
    void reset() override {}

    void create(HWND parent) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_TAB_CLASSES};
        if (!InitCommonControlsEx(&controls))
            throw std::runtime_error("Cannot initialize Modern Playlist preferences tabs");
        if (!CreateDialogParamW(core_api::get_my_instance(), MAKEINTRESOURCEW(IDD_PREFERENCES),
                parent, dialog_proc, reinterpret_cast<LPARAM>(this)))
            throw std::runtime_error("Cannot create Modern Playlist preferences");
    }

private:
    HWND hwnd_ = nullptr;
    fb2k::CCoreDarkModeHooks dark_mode_;

    static INT_PTR CALLBACK dialog_proc(HWND wnd, UINT msg, WPARAM, LPARAM lp) {
        auto* self = reinterpret_cast<playlist_preferences*>(GetWindowLongPtrW(wnd, DWLP_USER));
        if (msg == WM_INITDIALOG) {
            self = reinterpret_cast<playlist_preferences*>(lp);
            self->hwnd_ = wnd;
            SetWindowLongPtrW(wnd, DWLP_USER, lp);
            const HWND tabs = GetDlgItem(wnd, IDC_PREFS_TABS);
            wchar_t general[] = L"General";
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = general;
            TabCtrl_InsertItem(tabs, 0, &item);
            TabCtrl_SetCurSel(tabs, 0);
            self->dark_mode_.AddDialogWithControls(wnd);
            return TRUE;
        }
        if (!self) return FALSE;
        if (msg == WM_NCDESTROY) {
            // The preferences host destroys the dialog before releasing the instance.
            self->hwnd_ = nullptr;
            SetWindowLongPtrW(wnd, DWLP_USER, 0);
        }
        return FALSE;
    }
};

class playlist_preferences_page : public preferences_page_v3 {
public:
    const char* get_name() override { return "Modern Playlist"; }
    GUID get_guid() override { return preferences_id; }
    GUID get_parent_guid() override { return preferences_page::guid_tools; }
    preferences_page_instance::ptr instantiate(HWND parent, preferences_page_callback::ptr) override {
        auto instance = fb2k::service_new<playlist_preferences>();
        instance->create(parent);
        return instance;
    }
};

preferences_page_factory_t<playlist_preferences_page> preferences_factory;
}
