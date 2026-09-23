#include "special_playlists.h"
#include "model.h"
#include <memory>
#include <stdexcept>
#include <SDK/autoplaylist.h>

namespace modern_playlist {
namespace {
constexpr GUID setting_id(unsigned n) { return {0x9fc37400+n,0xc2bc,0x42ad,{0xac,0x17,0x27,0x7f,0xa0,0x90,0x45,0x08}}; }
cfg_bool library_on(setting_id(0),false), history_on(setting_id(1),false), queue_on(setting_id(2),false);
cfg_guid library_id(setting_id(3),GUID{}), history_id(setting_id(4),GUID{}), queue_id(setting_id(5),GUID{});
cfg_bool& enabled(special_playlist kind) { return kind==special_playlist::library?library_on:kind==special_playlist::history?history_on:queue_on; }
cfg_guid& identity(special_playlist kind) { return kind==special_playlist::library?library_id:kind==special_playlist::history?history_id:queue_id; }
t_size locate(special_playlist kind) { return playlist_manager_v5::get()->find_playlist_by_guid(identity(kind).get_value()); }
bool running=false, scheduled=false, queue_dirty=false;

class queue_lock : public playlist_lock {
public:
    bool writing=false;
    bool query_items_add(t_size,metadb_handle_list_cref,const bit_array&) override { return writing; }
    bool query_items_reorder(const t_size*,t_size) override { return writing; }
    bool query_items_remove(const bit_array&,bool force) override { return writing || force; }
    bool query_item_replace(t_size,const metadb_handle_ptr&,const metadb_handle_ptr&) override { return writing; }
    bool query_playlist_rename(const char*,t_size) override { return false; }
    bool query_playlist_remove() override { return true; }
    bool execute_default_action(t_size) override { return false; }
    void on_playlist_index_change(t_size) override {}
    void on_playlist_remove() override {}
    void get_lock_name(pfc::string_base& out) override { out="Modern Playlist: Queue Content"; }
    void show_ui() override {}
    t_uint32 get_filter_mask() override { return filter_add|filter_remove|filter_reorder|filter_replace|filter_rename; }
};
service_ptr_t<queue_lock> queue_guard;

void synchronize() {
    auto pm=playlist_manager::get();
    for(auto kind:{special_playlist::library,special_playlist::history,special_playlist::queue})
        if(enabled(kind) && locate(kind)==pfc::infinite_size) enabled(kind)=false;
    if(library_on) {
        auto index=locate(special_playlist::library);
        if(index!=0) { auto order=move_order(pm->get_playlist_count(),index,0); pm->reorder(order.data(),order.size()); }
    }
    if(queue_on && queue_dirty) {
        queue_dirty=false;
        const auto index=locate(special_playlist::queue);
        if(!queue_guard.is_valid()) {
            auto guard=fb2k::service_new<queue_lock>();
            if(!pm->playlist_lock_install(index,guard)) throw std::runtime_error("Cannot lock Queue Content playlist");
            queue_guard=guard;
        }
        pfc::list_t<t_playback_queue_item> queue; pm->queue_get_contents(queue);
        metadb_handle_list incoming; for(t_size i=0;i<queue.get_count();++i) incoming.add_item(queue[i].m_handle);
        metadb_handle_list current; pm->playlist_get_all_items(index,current);
        bool same=current.get_count()==incoming.get_count();
        for(t_size i=0;same && i<current.get_count();++i) same=current[i]==incoming[i];
        if(!same) {
            struct write_scope { queue_lock& lock; write_scope(queue_lock& l):lock(l){lock.writing=true;} ~write_scope(){lock.writing=false;} } scope(*queue_guard);
            pm->playlist_remove_items(index,bit_array_true());
            pm->playlist_insert_items(index,pfc::infinite_size,incoming,bit_array_false());
        }
    }
    if(!queue_on) queue_guard.release();
}
void schedule(bool queue=false) {
    queue_dirty=queue_dirty || queue;
    if(!running || scheduled) return;
    scheduled=true;
    fb2k::inMainThread([] {
        scheduled=false; if(!running) return;
        try { synchronize(); } catch(const std::exception& e) { console::error(e.what()); }
    });
}
class callbacks : private play_callback_impl_base, private playlist_callback_impl_base {
public:
    callbacks():play_callback_impl_base(0),playlist_callback_impl_base(0) {}
    void start() {
        running=true;
        play_callback_reregister(play_callback::flag_on_playback_new_track);
        set_callback_flags(playlist_callback::flag_on_playlists_reorder|playlist_callback::flag_on_playlists_removed|playlist_callback::flag_on_playlist_created);
        schedule(true);
    }
    void stop() {
        running=false; play_callback_reregister(0); set_callback_flags(0);
        if(queue_guard.is_valid()) {
            auto index=locate(special_playlist::queue);
            if(index!=pfc::infinite_size) playlist_manager::get()->playlist_lock_uninstall(index,queue_guard);
            queue_guard.release();
        }
    }
private:
    void on_playback_new_track(metadb_handle_ptr track) override {
        if(!history_on) return;
        const GUID id=history_id.get_value();
        fb2k::inMainThread([track,id] {
            if(!running || !history_on || history_id.get_value()!=id) return;
            try {
                auto pm=playlist_manager_v5::get(); auto index=pm->find_playlist_by_guid(id);
                if(index==pfc::infinite_size) return;
                metadb_handle_list tracks; tracks.add_item(track);
                pm->playlist_insert_items(index,pfc::infinite_size,tracks,bit_array_false());
            } catch(const std::exception& e) { console::error(e.what()); }
        });
    }
    void on_playlists_reorder(const t_size*,t_size) override { schedule(); }
    void on_playlists_removed(const bit_array&,t_size,t_size) override { schedule(true); }
    void on_playlist_created(t_size,const char*,t_size) override { schedule(); }
};
class lifecycle : public initquit {
    std::unique_ptr<callbacks> callbacks_;
public:
    void on_init() override { callbacks_=std::make_unique<callbacks>(); callbacks_->start(); }
    void on_quit() override { callbacks_->stop(); callbacks_.reset(); }
};
initquit_factory_t<lifecycle> lifecycle_factory;
class queue_notifications : public playback_queue_callback {
    void on_changed(t_change_origin) override { schedule(true); }
};
service_factory_single_t<queue_notifications> queue_factory;
}
bool special_enabled(special_playlist kind) { return enabled(kind); }
bool library_pinned(t_size playlist) { return library_on && playlist==locate(special_playlist::library); }
bool special_reserved(t_size playlist) {
    for(auto kind:{special_playlist::library,special_playlist::history,special_playlist::queue})
        if(enabled(kind) && playlist==locate(kind)) return true;
    return false;
}
void toggle_special(special_playlist kind) {
    auto pm=playlist_manager_v5::get();
    auto index=locate(kind);
    if(kind==special_playlist::queue && index==pfc::infinite_size) queue_guard.release();
    if(enabled(kind)) {
        // Use the host's removal confirmation; cancellation keeps the feature on.
        if(index!=pfc::infinite_size && !pm->remove_playlist_user(index)) return;
        enabled(kind)=false;
        if(kind==special_playlist::queue) queue_guard.release();
        return;
    }
    const char* name=kind==special_playlist::library?"Media Library":kind==special_playlist::history?"Historic":"Queue Content";
    // Never take over a user's same-named playlist.
    index=pm->create_playlist(name,pfc::infinite_size,kind==special_playlist::library?0:pfc::infinite_size);
    if(index==pfc::infinite_size) return;
    try {
        if(kind==special_playlist::library) autoplaylist_manager::get()->add_client_simple("ALL","%album artist% | %album% | %discnumber% | %tracknumber%",index,autoplaylist_flag_sort);
        identity(kind)=pm->playlist_get_guid(index); enabled(kind)=true;
        if(kind==special_playlist::queue) queue_dirty=true;
        synchronize();
        schedule(true);
        pm->set_active_playlist(locate(kind));
    } catch(...) { enabled(kind)=false; pm->remove_playlist(index); throw; }
}
}
