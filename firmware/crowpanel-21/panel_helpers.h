#pragma once
// Umbrella header named in crowpanel-21.yaml (`esphome: includes`). The logic lives in:
//   panel_feed.h  fixed-size feed model and parser
//   panel_nav.h   navigation, idle-home, red auto-switch, backlight
//   panel_view.h  text and colours of one screen
//   panel_ui.h    LVGL widgets (firmware only)
#include <initializer_list>
#include "panel_ui.h"

namespace panel {
// The panel's one feed copy and navigation state (declared here because ESPHome emits globals before the includes).
inline Feed g_feed;
inline Nav g_nav;
}  // namespace panel
