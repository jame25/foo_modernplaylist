#pragma once
#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>
namespace modern_playlist {
struct group_pattern {
    std::string label="Album", key="$if2(%album artist%,%artist%)$char(31)%album%$char(31)%discnumber%";
    std::string l1="%album%", r1="[%date%]", l2="$if2(%album artist%,%artist%)", r2="[%codec%]";
    std::string sort_order="%album artist% | %album% | %discnumber% | %tracknumber% | %title%", playlist_filter="*";
};
struct grouping_settings {
    bool enabled=false, playlist_filter=false, collapse_default=false, autocollapse=false;
    unsigned minimum_rows=0, extra_rows=0, pattern=0;
    std::vector<group_pattern> patterns{group_pattern{}};
};
inline std::vector<std::string> filter_names(const std::string& filter) {
    std::vector<std::string> names;
    for(size_t start=0;start<=filter.size();) {
        auto end=filter.find(';',start); if(end==std::string::npos) end=filter.size();
        auto name=filter.substr(start,end-start);
        const auto a=name.find_first_not_of(" \t\r\n"), b=name.find_last_not_of(" \t\r\n");
        if(a!=std::string::npos) names.push_back(name.substr(a,b-a+1));
        if(end==filter.size()) break; start=end+1;
    }
    return names;
}
// Explicit names outrank the first wildcard, irrespective of list order.
inline size_t matching_pattern(const std::vector<group_pattern>& patterns,const std::string& name,size_t fallback) {
    size_t wildcard=patterns.size();
    for(size_t i=0;i<patterns.size();++i) for(const auto& part:filter_names(patterns[i].playlist_filter)) {
        if(part==name && part!="*") return i;
        if(part=="*" && wildcard==patterns.size()) wildcard=i;
    }
    return wildcard<patterns.size()?wildcard:std::min(fallback,patterns.size()-1);
}
struct group_band { size_t first=0, count=0; unsigned padding=0; };
struct visual_slot { int track=-1, group=-1, line=-1; };
struct group_geometry {
    std::vector<visual_slot> slots;
    std::vector<size_t> track_slots;
    void build(size_t tracks,const std::vector<group_band>& bands) {
        slots.clear(); track_slots.assign(tracks,0);
        size_t row=0;
        auto add=[&] { track_slots[row]=slots.size(); slots.push_back({static_cast<int>(row++),-1,-1}); };
        for(size_t g=0;g<bands.size();++g) {
            const auto& band=bands[g];
            while(row<std::min(tracks,band.first)) add();
            slots.push_back({-1,static_cast<int>(g),0});
            slots.push_back({-1,static_cast<int>(g),1});
            const auto end=std::min(tracks,band.first+band.count);
            while(row<end) add();
            for(unsigned n=0;n<band.padding;++n) slots.push_back({-1,static_cast<int>(g),-1});
        }
        while(row<tracks) add();
    }
};
}
