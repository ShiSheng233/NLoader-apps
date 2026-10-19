#include "install.h"

static void install_publish(volatile struct heraw_install_state *state)
{
    __atomic_store_n((uint32_t *)&state->ready, 1u, __ATOMIC_RELEASE);
}

static int install_rollback(volatile struct heraw_install_state *state,
                            void *context, heraw_install_op detach,
                            uint32_t installed, int install_result)
{
    /* Every live handler sees ready == 0 and is a transparent pass-through.
     * Restore attached sites in reverse order. A restoration failure is
     * retained for the boot log; NLoader keeps this extension's code resident
     * even when nlr_extension_init returns the installation error. */
    __atomic_store_n((uint32_t *)&state->stop, 1u, __ATOMIC_RELEASE);
    while (installed) {
        int undone = detach(context, --installed);
        if (undone != 0 && state->rollback == 0)
            state->rollback = undone;
    }
    return install_result;
}

int heraw_install_hooks(volatile struct heraw_install_state *state,
                        void *context, uint32_t count,
                        heraw_install_op attach, heraw_install_op detach)
{
    uint32_t installed = 0;
    while (installed < count) {
        int result = attach(context, installed);
        if (result != 0)
            return install_rollback(state, context, detach, installed, result);
        installed++;
    }
    install_publish(state);
    return 0;
}
