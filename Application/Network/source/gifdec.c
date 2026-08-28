#include "gifdec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

#define MIN(A, B) ((A) < (B) ? (A) : (B))
#define MAX(A, B) ((A) > (B) ? (A) : (B))

typedef struct Entry {
    uint16_t length;
    uint16_t prefix;
    uint8_t  suffix;
} Entry;

typedef struct Table {
    int bulk;
    int nentries;
    Entry *entries;
} Table;

static void gif_read(gd_GIF *gif, void *dst, size_t n)
{
    if (gif->pos + n <= gif->size) {
        memcpy(dst, gif->data + gif->pos, n);
        gif->pos += n;
    }
}

static void gif_skip(gd_GIF *gif, size_t n)
{
    if (gif->pos + n <= gif->size)
        gif->pos += n;
}

static uint16_t read_num(gd_GIF *gif)
{
    const uint8_t *p = gif->data + gif->pos;
    gif->pos += 2;
    return p[0] + (((uint16_t) p[1]) << 8);
}

static uint16_t read_num_raw(const uint8_t *data, size_t *pos)
{
    uint16_t val = data[*pos] + (((uint16_t) data[*pos + 1]) << 8);
    *pos += 2;
    return val;
}

gd_GIF *gd_open_gif(const uint8_t *data, size_t size)
{
    uint8_t sigver[3];
    uint16_t width, height, depth;
    uint8_t fdsz, bgidx, aspect;
    int i;
    uint8_t *bgcolor;
    int gct_sz;
    gd_GIF *gif;
    size_t pos = 0;

    if (!data || size < 13)
        return NULL;

    memcpy(sigver, data + pos, 3); pos += 3;
    if (memcmp(sigver, "GIF", 3) != 0) {
        fprintf(stderr, "invalid signature\n");
        return NULL;
    }
    memcpy(sigver, data + pos, 3); pos += 3;
    if (memcmp(sigver, "89a", 3) != 0) {
        fprintf(stderr, "invalid version\n");
        return NULL;
    }
    width  = read_num_raw(data, &pos);
    height = read_num_raw(data, &pos);
    fdsz = data[pos++];
    if (!(fdsz & 0x80)) {
        fprintf(stderr, "no global color table\n");
        return NULL;
    }
    depth = ((fdsz >> 4) & 7) + 1;
    gct_sz = 1 << ((fdsz & 0x07) + 1);
    bgidx = data[pos++];
    aspect = data[pos++];
    (void)aspect;
    gif = heap_caps_calloc(1, sizeof(*gif), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gif) return NULL;
    gif->data = data;
    gif->size = size;
    gif->pos = pos;
    gif->width  = width;
    gif->height = height;
    gif->depth  = depth;
    gif->gct.size = gct_sz;
    gif_read(gif, gif->gct.colors, 3 * gif->gct.size);
    gif->palette = &gif->gct;
    gif->bgindex = bgidx;
    gif->frame = heap_caps_calloc(4, width * height, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gif->frame) {
        free(gif);
        return NULL;
    }
    gif->canvas = &gif->frame[width * height];
    if (gif->bgindex)
        memset(gif->frame, gif->bgindex, gif->width * gif->height);
    bgcolor = &gif->palette->colors[gif->bgindex*3];
    if (bgcolor[0] || bgcolor[1] || bgcolor[2])
        for (i = 0; i < gif->width * gif->height; i++)
            memcpy(&gif->canvas[i*3], bgcolor, 3);
    gif->anim_start = gif->pos;
    return gif;
}

static void
discard_sub_blocks(gd_GIF *gif)
{
    uint8_t size;

    do {
        gif_read(gif, &size, 1);
        gif_skip(gif, size);
    } while (size);
}

static void
read_plain_text_ext(gd_GIF *gif)
{
    if (gif->plain_text) {
        uint16_t tx, ty, tw, th;
        uint8_t cw, ch, fg, bg;
        size_t sub_block;
        gif_skip(gif, 1); /* block size = 12 */
        tx = read_num(gif);
        ty = read_num(gif);
        tw = read_num(gif);
        th = read_num(gif);
        gif_read(gif, &cw, 1);
        gif_read(gif, &ch, 1);
        gif_read(gif, &fg, 1);
        gif_read(gif, &bg, 1);
        sub_block = gif->pos;
        gif->plain_text(gif, tx, ty, tw, th, cw, ch, fg, bg);
        gif->pos = sub_block;
    } else {
        gif_skip(gif, 13);
    }
    discard_sub_blocks(gif);
}

static void
read_graphic_control_ext(gd_GIF *gif)
{
    uint8_t rdit;

    gif_skip(gif, 1); /* block size (always 0x04) */
    gif_read(gif, &rdit, 1);
    gif->gce.disposal = (rdit >> 2) & 3;
    gif->gce.input = rdit & 2;
    gif->gce.transparency = rdit & 1;
    gif->gce.delay = read_num(gif);
    gif_read(gif, &gif->gce.tindex, 1);
    gif_skip(gif, 1); /* block terminator */
}

static void
read_comment_ext(gd_GIF *gif)
{
    if (gif->comment) {
        size_t sub_block = gif->pos;
        gif->comment(gif);
        gif->pos = sub_block;
    }
    discard_sub_blocks(gif);
}

static void
read_application_ext(gd_GIF *gif)
{
    char app_id[8];
    char app_auth_code[3];

    gif_skip(gif, 1); /* block size (always 0x0B) */
    gif_read(gif, app_id, 8);
    gif_read(gif, app_auth_code, 3);
    if (!strncmp(app_id, "NETSCAPE", sizeof(app_id))) {
        gif_skip(gif, 2); /* block size (0x03) + constant byte (0x01) */
        gif->loop_count = read_num(gif);
        gif_skip(gif, 1); /* block terminator */
    } else if (gif->application) {
        size_t sub_block = gif->pos;
        gif->application(gif, app_id, app_auth_code);
        gif->pos = sub_block;
        discard_sub_blocks(gif);
    } else {
        discard_sub_blocks(gif);
    }
}

static void
read_ext(gd_GIF *gif)
{
    uint8_t label;

    gif_read(gif, &label, 1);
    switch (label) {
    case 0x01:
        read_plain_text_ext(gif);
        break;
    case 0xF9:
        read_graphic_control_ext(gif);
        break;
    case 0xFE:
        read_comment_ext(gif);
        break;
    case 0xFF:
        read_application_ext(gif);
        break;
    default:
        fprintf(stderr, "unknown extension: %02X\n", label);
    }
}

static Table *
new_table(int key_size)
{
    int key;
    int init_bulk = MAX(1 << (key_size + 1), 0x100);
    Table *table = heap_caps_malloc(sizeof(*table) + sizeof(Entry) * init_bulk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (table) {
        table->bulk = init_bulk;
        table->nentries = (1 << key_size) + 2;
        table->entries = (Entry *) &table[1];
        for (key = 0; key < (1 << key_size); key++)
            table->entries[key] = (Entry) {1, 0xFFF, key};
    }
    return table;
}

/* Add table entry. Return value:
 *  0 on success
 *  +1 if key size must be incremented after this addition
 *  -1 if could not realloc table */
static int
add_entry(Table **tablep, uint16_t length, uint16_t prefix, uint8_t suffix)
{
    Table *table = *tablep;
    if (table->nentries == table->bulk) {
        table->bulk *= 2;
        table = heap_caps_realloc(table, sizeof(*table) + sizeof(Entry) * table->bulk, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!table) return -1;
        table->entries = (Entry *) &table[1];
        *tablep = table;
    }
    table->entries[table->nentries] = (Entry) {length, prefix, suffix};
    table->nentries++;
    if ((table->nentries & (table->nentries - 1)) == 0)
        return 1;
    return 0;
}

static uint16_t
get_key(gd_GIF *gif, int key_size, uint8_t *sub_len, uint8_t *shift, uint8_t *byte)
{
    int bits_read;
    int rpad;
    int frag_size;
    uint16_t key;

    key = 0;
    for (bits_read = 0; bits_read < key_size; bits_read += frag_size) {
        rpad = (*shift + bits_read) % 8;
        if (rpad == 0) {
            if (*sub_len == 0) {
                gif_read(gif, sub_len, 1);
                if (*sub_len == 0)
                    return 0x1000;
            }
            gif_read(gif, byte, 1);
            (*sub_len)--;
        }
        frag_size = MIN(key_size - bits_read, 8 - rpad);
        key |= ((uint16_t) ((*byte) >> rpad)) << bits_read;
    }
    key &= (1 << key_size) - 1;
    *shift = (*shift + key_size) % 8;
    return key;
}

/* Compute output index of y-th input line, in frame of height h. */
static int
interlaced_line_index(int h, int y)
{
    int p; /* number of lines in current pass */

    p = (h - 1) / 8 + 1;
    if (y < p) /* pass 1 */
        return y * 8;
    y -= p;
    p = (h - 5) / 8 + 1;
    if (y < p) /* pass 2 */
        return y * 8 + 4;
    y -= p;
    p = (h - 3) / 4 + 1;
    if (y < p) /* pass 3 */
        return y * 4 + 2;
    y -= p;
    /* pass 4 */
    return y * 2 + 1;
}

static int
read_image_data(gd_GIF *gif, int interlace)
{
    uint8_t sub_len, shift, byte;
    int init_key_size, key_size, table_is_full = 0;
    int frm_off, frm_size, str_len = 0, i, p, x, y;
    uint16_t key, clear, stop;
    int ret;
    Table *table;
    Entry entry = {0};
    size_t start, end;

    gif_read(gif, &byte, 1);
    key_size = (int) byte;
    if (key_size < 2 || key_size > 8)
        return -1;
    
    start = gif->pos;
    discard_sub_blocks(gif);
    end = gif->pos;
    gif->pos = start;
    clear = 1 << key_size;
    stop = clear + 1;
    table = new_table(key_size);
    key_size++;
    init_key_size = key_size;
    sub_len = shift = 0;
    key = get_key(gif, key_size, &sub_len, &shift, &byte); /* clear code */
    frm_off = 0;
    ret = 0;
    frm_size = gif->fw*gif->fh;
    while (frm_off < frm_size) {
        if (key == clear) {
            key_size = init_key_size;
            table->nentries = (1 << (key_size - 1)) + 2;
            table_is_full = 0;
        } else if (!table_is_full) {
            ret = add_entry(&table, str_len + 1, key, entry.suffix);
            if (ret == -1) {
                heap_caps_free(table);
                return -1;
            }
            if (table->nentries == 0x1000) {
                ret = 0;
                table_is_full = 1;
            }
        }
        key = get_key(gif, key_size, &sub_len, &shift, &byte);
        if (key == clear) continue;
        if (key == stop || key == 0x1000) break;
        if (ret == 1) key_size++;
        entry = table->entries[key];
        str_len = entry.length;
        for (i = 0; i < str_len; i++) {
            p = frm_off + entry.length - 1;
            x = p % gif->fw;
            y = p / gif->fw;
            if (interlace)
                y = interlaced_line_index((int) gif->fh, y);
            gif->frame[(gif->fy + y) * gif->width + gif->fx + x] = entry.suffix;
            if (entry.prefix == 0xFFF)
                break;
            else
                entry = table->entries[entry.prefix];
        }
        frm_off += str_len;
        if (key < table->nentries - 1 && !table_is_full)
            table->entries[table->nentries - 1].suffix = entry.suffix;
    }
    heap_caps_free(table);
    if (key == stop)
        gif_read(gif, &sub_len, 1);
    gif->pos = end;
    return 0;
}

/* Read image.
 *  Return 0 on success or -1 on out-of-memory (w.r.t. LZW code table). */
static int
read_image(gd_GIF *gif)
{
    uint8_t fisrz;
    int interlace;

    gif->fx = read_num(gif);
    gif->fy = read_num(gif);
    
    if (gif->fx >= gif->width || gif->fy >= gif->height)
        return -1;
    
    gif->fw = read_num(gif);
    gif->fh = read_num(gif);
    
    gif->fw = MIN(gif->fw, gif->width - gif->fx);
    gif->fh = MIN(gif->fh, gif->height - gif->fy);
    
    gif_read(gif, &fisrz, 1);
    interlace = fisrz & 0x40;
    if (fisrz & 0x80) {
        gif->lct.size = 1 << ((fisrz & 0x07) + 1);
        gif_read(gif, gif->lct.colors, 3 * gif->lct.size);
        gif->palette = &gif->lct;
    } else
        gif->palette = &gif->gct;
    return read_image_data(gif, interlace);
}

static void
render_frame_rect(gd_GIF *gif, uint8_t *buffer)
{
    int i, j, k;
    uint8_t index, *color;
    i = gif->fy * gif->width + gif->fx;
    for (j = 0; j < gif->fh; j++) {
        for (k = 0; k < gif->fw; k++) {
            index = gif->frame[(gif->fy + j) * gif->width + gif->fx + k];
            color = &gif->palette->colors[index*3];
            if (!gif->gce.transparency || index != gif->gce.tindex)
                memcpy(&buffer[(i+k)*3], color, 3);
        }
        i += gif->width;
    }
}

static void
dispose(gd_GIF *gif)
{
    int i, j, k;
    uint8_t *bgcolor;
    switch (gif->gce.disposal) {
    case 2: /* Restore to background color. */
        bgcolor = &gif->palette->colors[gif->bgindex*3];
        i = gif->fy * gif->width + gif->fx;
        for (j = 0; j < gif->fh; j++) {
            for (k = 0; k < gif->fw; k++)
                memcpy(&gif->canvas[(i+k)*3], bgcolor, 3);
            i += gif->width;
        }
        break;
    case 3: /* Restore to previous, i.e., don't update canvas.*/
        break;
    default:
        /* Add frame non-transparent pixels to canvas. */
        render_frame_rect(gif, gif->canvas);
    }
}

int
gd_get_frame(gd_GIF *gif)
{
    char sep;

    dispose(gif);
    gif_read(gif, &sep, 1);
    while (sep != ',') {
        if (sep == ';')
            return 0;
        if (sep == '!')
            read_ext(gif);
        else return -1;
        gif_read(gif, &sep, 1);
    }
    if (read_image(gif) == -1)
        return -1;
    return 1;
}

void
gd_render_frame(gd_GIF *gif, uint8_t *buffer)
{
    memcpy(buffer, gif->canvas, gif->width * gif->height * 3);
    render_frame_rect(gif, buffer);
}

int
gd_is_bgcolor(gd_GIF *gif, uint8_t color[3])
{
    return !memcmp(&gif->palette->colors[gif->bgindex*3], color, 3);
}

void
gd_rewind(gd_GIF *gif)
{
    gif->pos = gif->anim_start;
}

void
gd_close_gif(gd_GIF *gif)
{
    heap_caps_free(gif->frame);
    heap_caps_free(gif);
}

uint8_t *
gd_encode_raw(gd_GIF *gif, size_t *out_size)
{
    size_t frame_size, palette_size, total;
    int n_frames, f;
    uint8_t *buf;

    n_frames = 0;
    while (gd_get_frame(gif))
        n_frames++;

    if (n_frames == 0) {
        *out_size = 0;
        gd_rewind(gif);
        return NULL;
    }

    frame_size = gif->width * gif->height;
    palette_size = 256 * 3;
    total = palette_size + n_frames * frame_size;
    buf = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        *out_size = 0;
        gd_rewind(gif);
        return NULL;
    }
    *out_size = total;

    memcpy(buf, gif->palette->colors, palette_size);

    gd_rewind(gif);
    f = 0;
    while (gd_get_frame(gif) && f < n_frames) {
        memcpy(buf + palette_size + f * frame_size, gif->frame, frame_size);
        f++;
    }

    return buf;
}