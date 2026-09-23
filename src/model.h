#pragma once
#include <algorithm>
#include <cstddef>
#include <numeric>
#include <utility>
#include <vector>
namespace modern_playlist {
// Tab rectangles move when the native strip scrolls. Only their widths are
// intrinsic; absolute right edges must never determine the strip's width.
inline int tab_strip_width(const std::vector<std::pair<int,int>>& bounds, int available, int padding) {
    const int limit = std::max(0, available);
    if (bounds.empty()) return 0;
    long long total = std::max(0, padding);
    for (const auto& [left, right] : bounds) {
        total += std::max(0LL, static_cast<long long>(right) - left);
        if (total >= limit) return limit;
    }
    return static_cast<int>(std::min(total, static_cast<long long>(limit)));
}
// The view contains indices, never track-handle identities: duplicates stay distinct.
inline std::vector<std::size_t> move_order(std::size_t count, std::size_t from, std::size_t to) {
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    if (from >= count || to >= count || from == to) return order;
    const auto item = order[from];
    order.erase(order.begin() + from);
    order.insert(order.begin() + to, item);
    return order;
}
inline std::vector<std::size_t> visible_order(const std::vector<std::size_t>& rows,
                                             const std::vector<std::size_t>& sorted,
                                             std::size_t count) {
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), 0);
    if (rows.size() != sorted.size()) return order;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i] >= count || sorted[i] >= rows.size()) {
            std::iota(order.begin(), order.end(), 0); return order;
        }
        order[rows[i]] = rows[sorted[i]];
    }
    return order;
}
inline std::vector<std::size_t> nudge_order(std::vector<bool> selected, int direction) {
    std::vector<std::size_t> order(selected.size());
    std::iota(order.begin(),order.end(),0);
    auto swap_at = [&](std::size_t a, std::size_t b) {
        std::swap(order[a],order[b]); bool value=selected[a]; selected[a]=selected[b]; selected[b]=value;
    };
    if (direction<0) {
        for (std::size_t i=1;i<order.size();++i) if (selected[i] && !selected[i-1]) swap_at(i,i-1);
    } else {
        for (std::size_t i=order.size();i>1;--i) if (selected[i-2] && !selected[i-1]) swap_at(i-2,i-1);
    }
    return order;
}
inline std::vector<std::size_t> drop_order(const std::vector<bool>& selected, std::size_t before) {
    std::vector<std::size_t> order, moved;
    before=std::min(before,selected.size());
    for(std::size_t i=0;i<selected.size();++i) if(selected[i]) moved.push_back(i);
    for(std::size_t i=0;i<=selected.size();++i) {
        if (i==before) order.insert(order.end(),moved.begin(),moved.end());
        if (i<selected.size() && !selected[i]) order.push_back(i);
    }
    return order;
}

}
