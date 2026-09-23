#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include <unordered_map>
#include <limits>
namespace modern_playlist {
enum class row_kind { track, group };
enum class track_kind { file, stream };
template<class Handle> struct playlist_row {
    row_kind type = row_kind::track;
    size_t row_index = 0, track_index = 0, group_index = 0, track_index_in_group = 0;
    Handle metadb;
    unsigned height_in_rows = 1;
    track_kind tracktype = track_kind::file;
    std::vector<size_t> queue_positions; // 1-based; repeated queue occurrences stay distinct
    bool playing = false, paused = false;
};
struct group_position { size_t group = 0, index = 0; };
inline group_position next_group_position(const std::string& previous, const std::string& key,
                                          group_position position, bool first) {
    if (first) return {};
    if (key != previous) return {position.group+1,0};
    return {position.group,position.index+1};
}
inline bool alternate_row(size_t global, size_t in_group, bool grouped) {
    return ((grouped ? in_group : global) & 1) != 0;
}
// Entries with no playlist location identify media, so every matching occurrence
// receives the marker. Located entries identify one exact playlist occurrence.
inline constexpr size_t no_location = std::numeric_limits<size_t>::max();
struct queue_entry { size_t playlist, item; const void* handle; };
inline std::unordered_map<size_t,std::vector<size_t>> queue_positions(
    size_t playlist,const std::vector<const void*>& handles,const std::vector<queue_entry>& queue) {
    std::unordered_map<size_t,std::vector<size_t>> result;
    std::unordered_map<const void*,std::vector<size_t>> by_handle;
    bool indexed=false;
    for(size_t i=0;i<queue.size();++i) {
        const auto& entry=queue[i];
        if(entry.playlist==no_location || entry.item==no_location) {
            if(!indexed) {
                for(size_t j=0;j<handles.size();++j) by_handle[handles[j]].push_back(j);
                indexed=true;
            }
            const auto found=by_handle.find(entry.handle);
            if(found!=by_handle.end()) for(auto item:found->second) result[item].push_back(i+1);
        } else if(entry.playlist==playlist && entry.item<handles.size() && handles[entry.item]==entry.handle)
            result[entry.item].push_back(i+1);
    }
    return result;
}
struct core_settings {
    bool enqueue_on_double_click = false;
    bool alternating = true, group_parity = false, extra_line = false, derived_extra_color = true;
    bool tooltips = true;
    unsigned selection_alpha = 255, focus_alpha = 180, tooltip_delay = 650;
    std::string tooltip_pattern = "%title%$char(10)[%artist%]$char(10)[%album%][ '('%date%')']$char(10)[%codec% | ][%bitrate% kbps | ]%length%$char(10)%path%";
};
}
