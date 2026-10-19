#include "install.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HOOKS 5u

static int fail_hook;
static uint32_t fail_unhook_mask;
static uint32_t hook_calls;
static uint32_t unhook_calls;
static uint32_t hooked[HOOKS];
static uint32_t unhooked[HOOKS];
static volatile struct heraw_install_state *observed_state;
static int premature_ready;
static int missing_stop;

static int mock_hook(void *context, uint32_t index)
{
    (void)context;
    uint32_t call = hook_calls++;
    hooked[call] = index;
    if (observed_state->ready)
        premature_ready = 1;
    return (int)call == fail_hook ? -123 : 0;
}

static int mock_unhook(void *context, uint32_t index)
{
    (void)context;
    uint32_t call = unhook_calls++;
    unhooked[call] = index;
    if (!observed_state->stop)
        missing_stop = 1;
    return (fail_unhook_mask & (1u << call)) ? -77 - (int)call : 0;
}

static int check(int condition, const char *message)
{
    if (condition)
        return 0;
    fprintf(stderr, "FAIL: %s\n", message);
    return 1;
}

static int one_case(int hook_failure, uint32_t rollback_failures)
{
    struct heraw_install_state state = {0};
    observed_state = &state;
    fail_hook = hook_failure;
    fail_unhook_mask = rollback_failures;
    hook_calls = 0;
    unhook_calls = 0;
    premature_ready = 0;
    missing_stop = 0;
    memset(hooked, 0, sizeof hooked);
    memset(unhooked, 0, sizeof unhooked);

    int result = heraw_install_hooks(&state, 0, HOOKS,
                                     mock_hook, mock_unhook);
    uint32_t installed = hook_failure < 0 ? HOOKS : (uint32_t)hook_failure;
    int failed = 0;
    failed |= check(hook_calls ==
                        (hook_failure < 0 ? HOOKS : (uint32_t)hook_failure + 1u),
                    "hook call count");
    for (uint32_t n = 0; n < hook_calls; n++)
        failed |= check(hooked[n] == n, "hook order");
    failed |= check(!premature_ready, "ready is published after all hooks");
    if (hook_failure < 0) {
        failed |= check(result == 0, "successful install result");
        failed |= check(state.ready == 1u && state.stop == 0u,
                        "successful install publication");
        failed |= check(unhook_calls == 0u && state.rollback == 0,
                        "successful install does not roll back");
        return failed;
    }
    failed |= check(result == -123, "failed install preserves hook error");
    failed |= check(state.ready == 0u && state.stop == 1u,
                    "failed install remains dormant and stops worker");
    failed |= check(unhook_calls == installed,
                    "failed install rollback count");
    failed |= check(!missing_stop, "worker stop is published before rollback");
    for (uint32_t n = 0; n < installed; n++)
        failed |= check(
            unhooked[n] == installed - 1u - n,
            "rollback order");
    int32_t rollback = 0;
    for (uint32_t n = 0; n < installed; n++) {
        if (rollback_failures & (1u << n)) {
            rollback = -77 - (int32_t)n;
            break;
        }
    }
    failed |= check(state.rollback == rollback, "first rollback result");
    return failed;
}

int main(void)
{
    int failed = one_case(-1, 0u);
    for (uint32_t failure = 0; failure < HOOKS; failure++) {
        failed |= one_case((int)failure, 0u);
        if (failure > 0) {
            failed |= one_case((int)failure, 1u << (failure - 1u));
            failed |= one_case((int)failure,
                               (1u << failure) - 1u);
        }
    }
    if (!failed)
        puts("heraw install transaction: ok");
    return failed ? 1 : 0;
}
