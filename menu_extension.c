/* Minimal deferred resource-extension example: the worker queues one row;
 * the menu task publishes it on the next page enter. No card writes and no
 * firmware addresses or runtime text-slot allocation are involved. */
#include "nlr.h"

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 2u)
        return -NLR_API_EINVAL;
    if (!api->menu_register)
        return -NLR_API_ENOSYS;

    /* Z8 C3.11 Setup window 663, stock index 42 / item 0x3894:
     * n-research/analysis/reports/menu-ui/ui_definitions/menu_items.csv
     * proves string ID 0x379 is "Firmware version". 0x7ffd identifies only
     * this injected test row; it does not claim a runtime string slot. */
    const struct nlr_menu_test_item item = {
        .label_id = 0x379u,
        .item_id = 0x7ffdu,
    };
    int result = api->menu_register(&item);
    return result <= 0 ? result : -NLR_API_EIO;
}
