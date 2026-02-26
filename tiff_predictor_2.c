/**
 * WASM codec: TIFF horizontal differencing predictor (Predictor=2).
 *
 * Encoding applies row-wise differencing; decoding undoes it via cumulative
 * sum.  Built as a WASI reactor — uses libc for allocation and string
 * handling.
 *
 * Exports (Generic WASM Codec ABI):
 *   alloc(size)                            -> ptr
 *   dealloc(ptr, size)                     -> void
 *   decode(input_ptr, input_len,
 *          config_ptr, config_len)         -> (out_ptr << 32) | out_len
 *   encode(input_ptr, input_len,
 *          config_ptr, config_len)         -> (out_ptr << 32) | out_len
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- ABI: allocation --------------------------------------------------- */

__attribute__((export_name("alloc")))
void *alloc(int32_t size) {
    return malloc((size_t)size);
}

__attribute__((export_name("dealloc")))
void dealloc(void *ptr, int32_t size) {
    (void)size;
    free(ptr);
}

/* ---- config parsing ---------------------------------------------------- */

/**
 * Find an integer value for a given key in a JSON-ish config string.
 * Allocates a null-terminated copy so we can use strstr/strtol.
 */
static int find_int(const uint8_t *json, int32_t len, const char *key) {
    char *buf = malloc((size_t)len + 1);
    if (!buf) return 0;
    memcpy(buf, json, (size_t)len);
    buf[len] = '\0';

    char *found = strstr(buf, key);
    if (!found) { free(buf); return 0; }

    char *colon = strchr(found, ':');
    if (!colon) { free(buf); return 0; }

    int val = (int)strtol(colon + 1, NULL, 10);
    free(buf);
    return val;
}

/* ---- decode core ------------------------------------------------------- */

/**
 * Undo horizontal differencing in-place: cumulative sum per row.
 * buf is modified in place; width is in samples, height in rows.
 */
static void cumsum_rows(uint8_t *buf, int width, int height, int bps) {
    for (int row = 0; row < height; row++) {
        int base = row * width * bps;

        if (bps == 1) {
            uint8_t *s = buf + base;
            for (int col = 1; col < width; col++)
                s[col] += s[col - 1];
        } else if (bps == 2) {
            uint16_t *s = (uint16_t *)(buf + base);
            for (int col = 1; col < width; col++)
                s[col] += s[col - 1];
        } else if (bps == 4) {
            uint32_t *s = (uint32_t *)(buf + base);
            for (int col = 1; col < width; col++)
                s[col] += s[col - 1];
        }
    }
}

/* ---- encode core ------------------------------------------------------- */

/**
 * Apply horizontal differencing in-place: row-wise differences.
 * buf is modified in place; width is in samples, height in rows.
 * Iterates backwards so each subtraction reads the original predecessor.
 */
static void diff_rows(uint8_t *buf, int width, int height, int bps) {
    for (int row = 0; row < height; row++) {
        int base = row * width * bps;

        if (bps == 1) {
            uint8_t *s = buf + base;
            for (int col = width - 1; col > 0; col--)
                s[col] -= s[col - 1];
        } else if (bps == 2) {
            uint16_t *s = (uint16_t *)(buf + base);
            for (int col = width - 1; col > 0; col--)
                s[col] -= s[col - 1];
        } else if (bps == 4) {
            uint32_t *s = (uint32_t *)(buf + base);
            for (int col = width - 1; col > 0; col--)
                s[col] -= s[col - 1];
        }
    }
}

/* ---- decode (WASM ABI entry point) ------------------------------------- */

__attribute__((export_name("decode")))
int64_t decode(const uint8_t *input, int32_t input_len,
               const uint8_t *config, int32_t config_len) {

    int bps   = find_int(config, config_len, "\"bytes_per_sample\"");
    int width = find_int(config, config_len, "\"width\"");

    if (bps <= 0 || width <= 0) return 0;

    int height = input_len / (width * bps);

    uint8_t *out = malloc((size_t)input_len);
    if (!out) return 0;
    memcpy(out, input, (size_t)input_len);

    cumsum_rows(out, width, height, bps);

    return ((int64_t)(uintptr_t)out << 32) | (int64_t)input_len;
}

/* ---- encode (WASM ABI entry point) ------------------------------------- */

__attribute__((export_name("encode")))
int64_t encode(const uint8_t *input, int32_t input_len,
               const uint8_t *config, int32_t config_len) {

    int bps   = find_int(config, config_len, "\"bytes_per_sample\"");
    int width = find_int(config, config_len, "\"width\"");

    if (bps <= 0 || width <= 0) return 0;

    int height = input_len / (width * bps);

    uint8_t *out = malloc((size_t)input_len);
    if (!out) return 0;
    memcpy(out, input, (size_t)input_len);

    diff_rows(out, width, height, bps);

    return ((int64_t)(uintptr_t)out << 32) | (int64_t)input_len;
}
