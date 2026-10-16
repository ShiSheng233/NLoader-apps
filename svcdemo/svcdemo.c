/* SVCDEMO: a service (ABI 7): an extension that starts a task of its own.
 * The task appends a line to APPS/SVCDEMO.LOG every two seconds -- the
 * time, a counter and whether its memory and file calls went through --
 * for as long as the camera runs, whatever foreground program is open:
 * its calls are its own (the loader knows its stack), so an application
 * ending never closes its files or frees its memory.
 *
 * A resident program keeps no writable static data (the loader would give
 * it a second block for the boot): the task's state lives on its stack. */
#include "nlr.h"

static void put_dec(char *out, uint32_t *at, uint32_t v)
{
    char d[12];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        out[(*at)++] = d[--n];
}

static void run(void *arg)
{
    const struct nlr_api *api = arg;
    for (uint32_t count = 0;; count++) {
        char line[64];
        uint32_t at = 0;
        put_dec(line, &at, api->time_ms());
        line[at++] = ' ';
        put_dec(line, &at, count);
        void *block = api->mem_alloc(4096, 0);
        line[at++] = ' ';
        line[at++] = block ? 'm' : '-';
        int fd = api->fs_open("APPS/SVCDEMO.LOG", NLR_OPEN_UPDATE);
        if (fd < 0)
            fd = api->fs_open("APPS/SVCDEMO.LOG", NLR_OPEN_WRITE);
        line[at++] = fd >= 0 ? 'f' : '-';
        line[at++] = '\n';
        if (fd >= 0) {
            api->fs_seek(fd, 0, NLR_SEEK_END);
            api->fs_write(fd, line, at);
            api->fs_close(fd);
        }
        if (block)
            api->mem_free(block);
        api->sleep_ms(2000);
    }
}

int32_t nlr_extension_init(const struct nlr_api *api)
{
    if (!api || api->abi < 7u)
        return 0;
    int32_t task = api->task_start(run, (void *)api, 32 * 1024, 0);
    api->log("svcdemo task %d", (int)task);
    return 0;
}
