#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cwctype>
#endif
namespace modern_playlist {
inline constexpr unsigned search_delay_ms=500, incremental_idle_ms=1000;
struct search_settings {
    bool visible=true, group_key=false, locate=false;
    unsigned field=0, scope=0;
    uint32_t color=0x0066d9ff; // COLORREF: warm yellow
};
struct text_match { size_t start, length; };
inline bool equal_search_text(const wchar_t* a,const wchar_t* b,size_t length) {
#ifdef _WIN32
    return CompareStringOrdinal(a,int(length),b,int(length),TRUE)==CSTR_EQUAL;
#else
    for(size_t i=0;i<length;++i) if(std::towlower(a[i])!=std::towlower(b[i])) return false;
    return true;
#endif
}
inline std::vector<text_match> search_matches(const std::wstring& text,const std::vector<std::wstring>& terms) {
    std::vector<text_match> matches;
    for(size_t i=0;i<text.size();) {
        size_t length=0;
        for(const auto& term:terms) if(!term.empty() && term.size()<=text.size()-i &&
            equal_search_text(text.data()+i,term.data(),term.size())) length=std::max(length,term.size());
        if(length) { matches.push_back({i,length}); i+=length; } else ++i;
    }
    return matches;
}
// Only highlight literal free-text queries. The SDK owns the full query grammar;
// guessing positive operands in NOT/OR/title-format expressions is misleading.
inline std::vector<std::wstring> literal_search_terms(const std::wstring& query) {
    std::vector<std::wstring> terms;
    for(size_t i=0;i<query.size();) {
        if(query[i]==L' ' || query[i]==L'\t') { ++i; continue; }
        std::wstring word; bool quoted=query[i]==L'"';
        if(quoted) {
            ++i; while(i<query.size() && query[i]!=L'"') word+=query[i++];
            if(i==query.size()) return {}; ++i;
        } else {
            while(i<query.size() && query[i]!=L' ' && query[i]!=L'\t') word+=query[i++];
            if(word.find_first_of(L"()%$\"\r\n")!=std::wstring::npos) return {};
            for(const auto* op:{L"AND",L"OR",L"NOT",L"IS",L"HAS",L"GREATER",L"LESS",L"EQUAL",L"PRESENT",L"MISSING",L"BEFORE",L"AFTER",L"SINCE",L"DURING",L"ALL"})
                if(word==op) return {};
        }
        if(!word.empty()) terms.push_back(std::move(word));
    }
    return terms;
}
struct incremental_search {
    std::wstring text;
    uint64_t last_input=0;
    void clear() { text.clear(); last_input=0; }
    bool expired(uint64_t now) const { return !text.empty() && now-last_input>=incremental_idle_ms; }
    void input(wchar_t ch,uint64_t now) {
        if(expired(now)) clear();
        last_input=now;
        if(ch==L'\b') {
            if(!text.empty()) {
                const auto tail=text.back(); text.pop_back();
                if(tail>=0xdc00 && tail<=0xdfff && !text.empty() && text.back()>=0xd800 && text.back()<=0xdbff) text.pop_back();
            }
        } else if(ch>=L' ' && ch!=0x7f && text.size()<256) text+=ch;
    }
};
}
