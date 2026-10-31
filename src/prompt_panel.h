#ifndef __PROMPT_PANEL_H__
#define __PROMPT_PANEL_H__

#include "lvgl/lvgl.h"
#include "websocket_client.h"
#include "notify_consumer.h"
#include "button_container.h"
#include "numpad.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>

class PrintStatusPanel;

struct SharedButton {
    SharedButton(lv_obj_t *bbtn) : btn(bbtn) {};
    lv_obj_t *btn;
};

class PromptPanel : public NotifyConsumer {
    public:
        PromptPanel(KWebSocketClient &ws, std::mutex &lock, lv_obj_t *parent, PrintStatusPanel &print_status_panel);
        ~PromptPanel();

        void handle_macro_response(json &j);
        void consume(json &j);

        lv_obj_t *get_container();
        void handle_callback(lv_event_t *event);
        void handle_step(lv_event_t *event);

        static void _handle_callback(lv_event_t *event) {
            PromptPanel *panel = (PromptPanel*)event->user_data;
            panel->handle_callback(event);
        };

        static void _handle_step(lv_event_t *event) {
            PromptPanel *panel = (PromptPanel*)event->user_data;
            panel->handle_step(event);
        };

        void foreground();
        void background();

    private:
        // dialogs klipper opens off printer status rather than action:prompt
        enum class KlipperDialog { NONE, MANUAL_PROBE, SCREWS_TILT };

        void fit_box();
        void open_numpad(std::string cmd);
        lv_obj_t *add_button(lv_obj_t *parent, const std::string &text, const std::string &cmd, const std::string &type);
        lv_obj_t *add_text(const std::string &text);
        void open(KlipperDialog d, const std::string &title);
        void clear();
        void close();
        void update_manual_probe(json &mp);
        void show_screws_tilt(json &st);

        KWebSocketClient &ws;
        PrintStatusPanel &print_status_panel;
        bool restore_print_status_foreground_ = false;
        KlipperDialog klipper_dialog = KlipperDialog::NONE;
        // manual probe: the step the - and + buttons move by
        std::string probe_step = "0.1";
        lv_obj_t *info_label = NULL;
        lv_obj_t *z_min_label = NULL;
        lv_obj_t *z_max_label = NULL;
        lv_obj_t *promptpanel_cont;
        lv_obj_t *prompt_cont;
        lv_obj_t *box;
        lv_obj_t *flex;
        lv_obj_t *header;
        lv_obj_t *button_group_cont = NULL;
        lv_obj_t *footer_cont;
        // a button whose command has a NAME=NUMPAD param asks for a number
        Numpad numpad;
        std::string numpad_cmd;
        bool prompt_shown = false;
        lv_obj_t *error_label = NULL;
        // a klipper error or M117 message shown while no prompt is up
        lv_obj_t *error_banner = NULL;
        bool banner_is_error = false;
        void show_banner(const std::string &text, bool error);
        void update_display_message(const std::string &message);

};

#endif // __PROMPT_PANEL_H__
