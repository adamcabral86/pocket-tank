/* pt_nvs.h — which NVS partition the tank keeps its saves in.
 *
 * On the Waveshare 1.69 watch the flash is shared with XR TAK (dual boot), and
 * the tank gets a partition of its own, "pt_nvs", so neither app's housekeeping
 * - an erase on a full partition, say - can touch the other's settings. Every
 * other board keeps the default partition, as before. */
#ifndef PT_NVS_H
#define PT_NVS_H
#include "sdkconfig.h"
#include "nvs.h"

#if CONFIG_POCKET_TANK_BOARD_WS169
#define PT_NVS_PART "pt_nvs"
#else
#define PT_NVS_PART NVS_DEFAULT_PART_NAME
#endif

static inline esp_err_t pt_nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *handle) {
    return nvs_open_from_partition(PT_NVS_PART, ns, mode, handle);
}
#endif
