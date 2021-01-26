#include <stdint.h>
#include <stddef.h>
#include <sys/port_io.h>
#include <dev/vga_textmode.h>
#include <dev/dev.h>
#include <mm/vmm.h>
#include <lib/resource.h>

#define VIDEO_BOTTOM ((VD_ROWS * VD_COLS) - 1)
#define VD_COLS (80 * 2)
#define VD_ROWS 25

static void escape_parse(char c);

static char *video_mem = (char *)(0xb8000 + MEM_PHYS_OFFSET);
static size_t cursor_offset = 0;
static int cursor_status = 1;
static uint8_t text_palette = 0x07;
static uint8_t cursor_palette = 0x70;
static int escape = 0;
static int esc_value0 = 0;
static int esc_value1 = 0;
static int *esc_value = &esc_value0;
static int esc_default0 = 1;
static int esc_default1 = 1;
static int *esc_default = &esc_default0;

static ssize_t vga_textmode_write(struct resource *this, const void *buf, off_t off, size_t count) {
    for (size_t i = 0; i < count; i++)
        text_putchar(((char*)buf)[i]);
    return count;
}

bool vga_textmode_init(void) {
    outb(0x3d4, 0x0a);
    outb(0x3d5, 0x20);
    text_clear();

    struct resource *vga_textmode = resource_create(sizeof(struct resource));

    vga_textmode->write = vga_textmode_write;
    vga_textmode->st.st_mode = S_IFCHR | 0666;

    dev_add_new(vga_textmode, "vga_textmode");

    return true;
}

static void clear_cursor(void) {
    video_mem[cursor_offset + 1] = text_palette;
    return;
}

static void draw_cursor(void) {
    if (cursor_status) {
        video_mem[cursor_offset + 1] = cursor_palette;
    }
    return;
}

static void scroll(void) {
    // move the text up by one row
    for (size_t i = 0; i <= VIDEO_BOTTOM - VD_COLS; i++)
        video_mem[i] = video_mem[i + VD_COLS];
    // clear the last line of the screen
    for (size_t i = VIDEO_BOTTOM; i > VIDEO_BOTTOM - VD_COLS; i -= 2) {
        video_mem[i] = text_palette;
        video_mem[i - 1] = ' ';
    }
    return;
}

void text_clear(void) {
    clear_cursor();
    for (size_t i = 0; i < VIDEO_BOTTOM; i += 2) {
        video_mem[i] = ' ';
        video_mem[i + 1] = text_palette;
    }
    cursor_offset = 0;
    draw_cursor();
    return;
}

static void text_clear_no_move(void) {
    clear_cursor();
    for (size_t i = 0; i < VIDEO_BOTTOM; i += 2) {
        video_mem[i] = ' ';
        video_mem[i + 1] = text_palette;
    }
    draw_cursor();
    return;
}

void text_enable_cursor(void) {
    cursor_status = 1;
    draw_cursor();
    return;
}

void text_disable_cursor(void) {
    cursor_status = 0;
    clear_cursor();
    return;
}

void text_putchar(char c) {
    if (escape) {
        escape_parse(c);
        return;
    }
    switch (c) {
        case 0x00:
            break;
        case 0x1B:
            escape = 1;
            return;
        case 0x0A:
            if (text_get_cursor_pos_y() == (VD_ROWS - 1)) {
                clear_cursor();
                scroll();
                text_set_cursor_pos(0, (VD_ROWS - 1));
            } else {
                text_set_cursor_pos(0, (text_get_cursor_pos_y() + 1));
            }
            break;
        case 0x08:
            if (cursor_offset) {
                clear_cursor();
                cursor_offset -= 2;
                video_mem[cursor_offset] = ' ';
                draw_cursor();
            }
            break;
        default:
            clear_cursor();
            video_mem[cursor_offset] = c;
            if (cursor_offset >= (VIDEO_BOTTOM - 1)) {
                scroll();
                cursor_offset = VIDEO_BOTTOM - (VD_COLS - 1);
            } else
                cursor_offset += 2;
            draw_cursor();
    }
    return;
}

static uint8_t ansi_colours[] = { 0, 4, 2, 6, 1, 5, 3, 7 };

static void sgr(void) {

    if (esc_value0 >= 30 && esc_value0 <= 37) {
        uint8_t pal = text_get_text_palette();
        pal = (pal & 0xf0) + ansi_colours[esc_value0 - 30];
        text_set_text_palette(pal);
        return;
    }

    if (esc_value0 >= 40 && esc_value0 <= 47) {
        uint8_t pal = text_get_text_palette();
        pal = (pal & 0x0f) + ansi_colours[esc_value0 - 40] * 0x10;
        text_set_text_palette(pal);
        return;
    }

    return;
}

static void escape_parse(char c) {

    if (c >= '0' && c <= '9') {
        *esc_value *= 10;
        *esc_value += c - '0';
        *esc_default = 0;
        return;
    }

    switch (c) {
        case '[':
            return;
        case ';':
            esc_value = &esc_value1;
            esc_default = &esc_default1;
            return;
        case 'A':
            if (esc_default0)
                esc_value0 = 1;
            if (esc_value0 > text_get_cursor_pos_y())
                esc_value0 = text_get_cursor_pos_y();
            text_set_cursor_pos(text_get_cursor_pos_x(),
                                text_get_cursor_pos_y() - esc_value0);
            break;
        case 'B':
            if (esc_default0)
                esc_value0 = 1;
            if ((text_get_cursor_pos_y() + esc_value0) > (VD_ROWS - 1))
                esc_value0 = (VD_ROWS - 1) - text_get_cursor_pos_y();
            text_set_cursor_pos(text_get_cursor_pos_x(),
                                text_get_cursor_pos_y() + esc_value0);
            break;
        case 'C':
            if (esc_default0)
                esc_value0 = 1;
            if ((text_get_cursor_pos_x() + esc_value0) > (VD_COLS / 2 - 1))
                esc_value0 = (VD_COLS / 2 - 1) - text_get_cursor_pos_x();
            text_set_cursor_pos(text_get_cursor_pos_x() + esc_value0,
                                text_get_cursor_pos_y());
            break;
        case 'D':
            if (esc_default0)
                esc_value0 = 1;
            if (esc_value0 > text_get_cursor_pos_x())
                esc_value0 = text_get_cursor_pos_x();
            text_set_cursor_pos(text_get_cursor_pos_x() - esc_value0,
                                text_get_cursor_pos_y());
            break;
        case 'H':
            esc_value0--;
            esc_value1--;
            if (esc_default0)
                esc_value0 = 0;
            if (esc_default1)
                esc_value1 = 0;
            if (esc_value1 >= (VD_COLS / 2))
                esc_value1 = (VD_COLS / 2) - 1;
            if (esc_value0 >= VD_ROWS)
                esc_value0 = VD_ROWS - 1;
            text_set_cursor_pos(esc_value1, esc_value0);
            break;
        case 'm':
            sgr();
            break;
        case 'J':
            switch (esc_value0) {
                case 2:
                    text_clear_no_move();
                    break;
                default:
                    break;
            }
            break;
        default:
            escape = 0;
            text_putchar('?');
            break;
    }

    esc_value = &esc_value0;
    esc_value0 = 0;
    esc_value1 = 0;
    esc_default = &esc_default0;
    esc_default0 = 1;
    esc_default1 = 1;
    escape = 0;

    return;
}

void text_set_cursor_palette(uint8_t c) {
    cursor_palette = c;
    draw_cursor();
    return;
}

uint8_t text_get_cursor_palette(void) {
    return cursor_palette;
}

void text_set_text_palette(uint8_t c) {
    text_palette = c;
    return;
}

uint8_t text_get_text_palette(void) {
    return text_palette;
}

int text_get_cursor_pos_x(void) {
    return (cursor_offset % VD_COLS) / 2;
}

int text_get_cursor_pos_y(void) {
    return cursor_offset / VD_COLS;
}

void text_set_cursor_pos(int x, int y) {
    clear_cursor();
    cursor_offset = y * VD_COLS + x * 2;
    draw_cursor();
    return;
}
