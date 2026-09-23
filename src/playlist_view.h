#pragma once
#include <SDK/foobar2000.h>
namespace modern_playlist {
inline constexpr GUID element_id = {0x77d7a836,0x4861,0x4cea,{0x94,0x8b,0x57,0x08,0x83,0x66,0x96,0x51}};
ui_element_instance::ptr create_columns_view(HWND parent, ui_element_config::ptr config);
}
