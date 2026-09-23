#include "playlist_view.h"
#include <columns_ui-sdk/ui_extension.h>

namespace {
constexpr GUID panel_id = {0xc454b280,0x1943,0x41f7,{0xaf,0x39,0x68,0x18,0x8b,0x6e,0x70,0x22}};

class columns_panel : public uie::window {
public:
    const GUID& get_extension_guid() const override { return panel_id; }
    void get_name(pfc::string_base& out) const override { out = "Modern Playlist"; }
    void get_category(pfc::string_base& out) const override { out = "Playlist views"; }
    unsigned get_type() const override { return uie::type_panel | uie::type_playlist; }
    bool is_available(const uie::window_host_ptr&) const override { return true; }
    bool get_description(pfc::string_base& out) const override {
        out = "Playlist tabs, search and configurable title-format columns."; return true;
    }
    HWND get_wnd() const override { return view_.is_valid() ? view_->get_wnd() : nullptr; }
    HWND create_or_transfer_window(HWND parent, const uie::window_host_ptr& host,
        const ui_helpers::window_position_t& position) override {
        if (get_wnd()) {
            const auto wnd = get_wnd();
            ShowWindow(wnd, SW_HIDE);
            SetParent(wnd, parent);
            if (host_.is_valid()) host_->relinquish_ownership(wnd);
        } else {
            auto config = config_;
            if (config.is_empty()) config = ui_element_config::g_create_empty(modern_playlist::element_id);
            view_ = modern_playlist::create_columns_view(parent, config);
        }
        host_ = host;
        SetWindowPos(get_wnd(), nullptr, position.x, position.y, position.cx, position.cy,
            SWP_NOZORDER | SWP_NOACTIVATE);
        // Columns UI owns visibility; the view is created without WS_VISIBLE.
        return get_wnd();
    }
    void destroy_window() override {
        if (view_.is_valid()) {
            config_ = view_->get_configuration();
            view_.release(); // Unregister callbacks and destroy the shared view.
        }
        host_.release();
    }
    void set_config(stream_reader* reader, t_size size, abort_callback& abort) override {
        if (size > 1024 * 1024) throw exception_io_data();
        config_ = ui_element_config::g_create(modern_playlist::element_id, reader, size, abort);
    }
    void get_config(stream_writer* writer, abort_callback& abort) const override {
        auto config = view_.is_valid() ? view_->get_configuration() : config_;
        if (config.is_valid()) writer->write(config->get_data(), config->get_data_size(), abort);
    }
private:
    uie::window_host_ptr host_;
    ui_element_instance::ptr view_;
    ui_element_config::ptr config_;
};
uie::window_factory<columns_panel> panel_factory;
}
