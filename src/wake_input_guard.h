#ifndef __WAKE_INPUT_GUARD_H__
#define __WAKE_INPUT_GUARD_H__

#ifdef __cplusplus
extern "C" {
#endif

void wake_input_guard_set_sleeping(int sleeping);
int wake_input_guard_filter_pointer(int button_state);
int wake_input_guard_take_wake_request(void);

#ifdef __cplusplus
}
#endif

#endif  // __WAKE_INPUT_GUARD_H__
