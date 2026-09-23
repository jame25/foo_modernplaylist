#pragma once
#include <SDK/foobar2000.h>
namespace modern_playlist {
enum class special_playlist { library, history, queue };
bool special_enabled(special_playlist kind);
void toggle_special(special_playlist kind);
bool special_reserved(t_size playlist);
bool library_pinned(t_size playlist);
}
