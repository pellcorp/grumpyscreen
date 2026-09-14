#ifndef __PROMPT_PANEL_H__
#define __PROMPT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"

#include <mutex>
#include <string>
#include <vector>

// Klipper's action:prompt protocol (prompt_begin/text/button/.../show/end),
// rendered as the same popout dialog the rest of the UI uses for questions:
// title banner, message, a row of keys per button group. It sits over
// whatever screen is up and is gone the moment a key is tapped or the macro
// ends the prompt, so nothing has to be hidden or brought back.
class PromptPanel : public NotifyConsumer {
    public:
        PromptPanel(KWebSocketClient &ws, std::mutex &lock);
        ~PromptPanel();

        void handle_macro_response(json &j);
        void consume(json &j);

    private:
        void show();
        void close();
        void on_button(uint32_t idx);
        static void _on_button(lv_obj_t *, uint32_t idx, void *user_data) {
            ((PromptPanel*)user_data)->on_button(idx);
        }

        KWebSocketClient &ws;
        lv_obj_t *mbox = NULL;

        // the prompt being assembled between prompt_begin and prompt_show
        std::string title;
        std::string text;
        std::vector<std::string> labels;    // "\n" entries break the key row
        std::vector<std::string> commands;  // one per label that is not "\n"
        int danger_idx = -1;                // the button klipper styled "error"
        // lv_msgbox keeps this pointer array, so it lives as long as the box
        std::vector<const char *> button_map;
};

#endif // __PROMPT_PANEL_H__
