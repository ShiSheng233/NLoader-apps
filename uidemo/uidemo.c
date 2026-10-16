/*
 * uidemo: every kind of row the camera's own menus draw, on NLoader's page
 * (foreground program, ABI 6) -- the SDK's example of ui_rows, of a plain
 * ui_list as the camera's choice page, of ui_input, the camera's keyboard,
 * and of ui_caution, its caution box.
 *
 *   Entries                  >   OK opens a page of plain entries
 *   Switch                (ON)   the camera's toggle switch: OK flips it
 *   Colour               Red >   OK opens the camera's choice page
 *   This line is text            the cursor passes it
 *   Greyed entry             >   drawn greyed; the cursor passes it too
 *   Name               NIKON >   OK opens the camera's keyboard
 *   Reset                    >   a question in the caution box: Yes / No
 *   About                    >   a message in the caution box: OK
 *
 * Back leaves. The state lives on the stack: a program with no writable
 * static data gets one memory region instead of two, and small heap
 * blocks are best avoided on this body. A constant table of pointers is
 * writable static data too (it needs relocating), so tables of texts are
 * filled at run time, through helpers the compiler cannot fold back into
 * such a table.
 */
#include "nlr.h"

/* The compiler may emit these for the row table's initialisation. */
void *memset(void *dst, int c, unsigned long n)
{
    unsigned char *d = dst;
    while (n--)
        *d++ = (unsigned char)c;
    return dst;
}

void *memcpy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

#define NAME_MAX 32u
#define NOTE_MAX 48u

struct demo {
    const struct nlr_api *api;
    int on;                   /* the switch */
    uint32_t colour;          /* the choice */
    char name[NAME_MAX];      /* the keyboard's text */
    char note[NOTE_MAX];      /* what the last OK did, or empty */
};

/* Three texts into a table on the caller's stack. */
__attribute__((noinline)) static void three(const char **t, const char *a,
                                            const char *b, const char *c)
{
    t[0] = a;
    t[1] = b;
    t[2] = c;
}

/* One row of the page. */
__attribute__((noinline)) static void row(struct nlr_row *r, uint32_t kind,
                                          uint32_t flags, const char *label,
                                          const char *value)
{
    r->kind = kind;
    r->flags = flags;
    r->label = label;
    r->value = value;
}

/* Copy `s` into `d` (`size` bytes, always terminated), after `at` bytes
 * already there; returns the new length. */
static uint32_t put(char *d, uint32_t size, uint32_t at, const char *s)
{
    while (s && *s && at + 1u < size)
        d[at++] = *s++;
    d[at] = 0;
    return at;
}

/* A page of plain entries: OK on one says which. */
static void entries(struct demo *d)
{
    const char *rows[3];
    three(rows, "First entry", "Second entry", "Third entry");
    int32_t r = d->api->ui_list("Entries", rows, 3, 0);
    if (r >= 0 && r < 3)
        put(d->note, NOTE_MAX, put(d->note, NOTE_MAX, 0, "Chose: "), rows[r]);
}

/* The camera's choice page: the choices, the cursor on the current one. */
static void colour(struct demo *d)
{
    const char *colours[3];
    three(colours, "Red", "Green", "Blue");
    int32_t r = d->api->ui_list("Colour", colours, 3, d->colour);
    if (r >= 0 && r < 3)
        d->colour = (uint32_t)r;
}

/* A question: Yes puts everything back as it started. */
static void reset(struct demo *d)
{
    int32_t r = d->api->ui_caution("Put the switch, colour and name back as they were?",
                                   NLR_CAUTION_YES_NO);
    if (r == 1) {
        d->on = 1;
        d->colour = 0;
        put(d->name, NAME_MAX, 0, "NIKON");
        put(d->note, NOTE_MAX, 0, "Reset");
    } else if (r == 0) {
        put(d->note, NOTE_MAX, 0, "Kept as they are");
    } else if (r == -NLR_API_ENOSYS) {
        put(d->note, NOTE_MAX, 0, "No caution box on this loader");
    }
}

/* A message: one button. */
static void about(struct demo *d)
{
    if (d->api->ui_caution("Every row here is drawn by the camera itself.",
                           NLR_CAUTION_OK) == -NLR_API_ENOSYS)
        put(d->note, NOTE_MAX, 0, "No caution box on this loader");
}

/* The camera's keyboard, starting from the current text. */
static void name(struct demo *d)
{
    char text[NAME_MAX];
    put(text, NAME_MAX, 0, d->name);
    int32_t r = d->api->ui_input("Name", text, NAME_MAX, NLR_INPUT_ASCII);
    if (r >= 0)
        put(d->name, NAME_MAX, 0, text);
    else if (r == -NLR_API_ENOSYS)
        put(d->note, NOTE_MAX, 0, "No keyboard on this loader");
}

int32_t nlr_app_main(const struct nlr_api *api)
{
    if (!api || api->abi < 6u || api->abi > NLR_ABI_VERSION || !api->ui_rows ||
        !api->ui_list)
        return 1;
    const char *colours[3];
    three(colours, "Red", "Green", "Blue");
    struct demo d;
    d.api = api;
    d.on = 1;
    d.colour = 0;
    put(d.name, NAME_MAX, 0, "NIKON");
    d.note[0] = 0;
    uint32_t selected = 0;
    for (;;) {
        struct nlr_row rows[9];
        row(&rows[0], NLR_ROW_ENTRY, 0, "Entries", 0);
        row(&rows[1], NLR_ROW_SWITCH, d.on ? NLR_ROW_ON : 0, "Switch", 0);
        row(&rows[2], NLR_ROW_SELECT, 0, "Colour", colours[d.colour]);
        row(&rows[3], NLR_ROW_TEXT, 0, "This line is text", 0);
        row(&rows[4], NLR_ROW_ENTRY, NLR_ROW_GREYED, "Greyed entry", 0);
        row(&rows[5], NLR_ROW_SELECT, 0, "Name", d.name);
        row(&rows[6], NLR_ROW_ENTRY, 0, "Reset", 0);
        row(&rows[7], NLR_ROW_ENTRY, 0, "About", 0);
        row(&rows[8], NLR_ROW_TEXT, 0, d.note, 0);
        uint32_t count = d.note[0] ? 9u : 8u;
        int32_t r = api->ui_rows("UI demo", rows, count, selected);
        if (r == NLR_LIST_BACK)
            return 0;
        if (r < 0)
            return 2;
        selected = (uint32_t)r;
        d.note[0] = 0;
        switch (r) {
        case 0: entries(&d); break;
        case 1: d.on = !d.on; break;   /* switches flip in place */
        case 2: colour(&d); break;
        case 5: name(&d); break;
        case 6: reset(&d); break;
        case 7: about(&d); break;
        default: break;                /* text and greyed rows are never chosen */
        }
    }
}
