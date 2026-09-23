#pragma once
#include <algorithm>
#include <vector>
#include <numeric>
namespace modern_playlist {
// One pixel coordinate system for painting, hit testing, scrolling and drops.
struct manager_geometry {
    std::vector<int> widths;
    int offset=0, viewport=0;
    int total() const { int value=0; for(auto width:widths) value+=width; return value; }
    void clamp() { offset=std::clamp(offset,0,std::max(0,total()-viewport)); }
    int left(int index) const { int x=-offset; for(int i=0;i<index;++i) x+=widths[i]; return x; }
    int hit(int x) const {
        if(x<0 || x>=viewport) return -1;
        int edge=-offset;
        for(size_t i=0;i<widths.size();++i) { edge+=widths[i]; if(x<edge) return int(i); }
        return -1;
    }
    int insertion(int x) const {
        int edge=-offset;
        for(size_t i=0;i<widths.size();++i) { if(x<edge+widths[i]/2) return int(i); edge+=widths[i]; }
        return int(widths.size());
    }
    void reveal(int index) {
        if(index<0 || size_t(index)>=widths.size()) return;
        const int x=left(index);
        if(x<0) offset+=x;
        else if(x+widths[index]>viewport) offset+=x+widths[index]-viewport;
        clamp();
    }
};
inline int manager_drop_destination(int from,int before,int count,int pinned=-1) {
    if(from<0 || from>=count || from==pinned || before<0 || before>count) return -1;
    const int to=before>from?before-1:before;
    return pinned==0?std::max(1,to):to;
}
// Stable sorting keeps duplicate names in their existing order; the pinned
// library is excluded from the permutation's sortable range.
template<class Less> std::vector<size_t> manager_name_order(size_t count,bool pinned,Less less) {
    std::vector<size_t> order(count); std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin()+(pinned && count?1:0),order.end(),less);
    return order;
}

}
