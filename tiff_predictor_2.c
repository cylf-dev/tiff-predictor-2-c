/**
 * Wasm codec: TIFF horizontal differencing predictor (Predictor=2).
 *
 * Encoding applies row-wise differencing; decoding undoes it via cumulative
 * sum.  Built as a WASI reactor — uses libc for allocation and string
 * handling.
 *
 * Uses the chonkle core ABI port-map wire format.
 *
 * Exports:
 *   alloc(size)             -> ptr
 *   dealloc(ptr, size)      -> void
 *   decode(pm_ptr, pm_len)  -> (out_ptr << 32) | out_len
 *   encode(pm_ptr, pm_len)  -> (out_ptr << 32) | out_len
 */

#include "core_abi.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ---- ABI: allocation --------------------------------------------------- */

__attribute__((export_name("alloc")))
int32_t codec_alloc(int32_t size) {
    void *ptr = malloc((size_t)size);
    return (int32_t)(uintptr_t)ptr;
}

__attribute__((export_name("dealloc")))
void codec_dealloc(int32_t ptr, int32_t size) {
    (void)size;
    free((void *)(uintptr_t)ptr);
}

/* ---- port value parsing ------------------------------------------------ */

/**
 * Parse an unsigned integer from a port's data bytes.
 * Port data for integer constants is UTF-8 digits (e.g. "2", "1024").
 */
static int parse_int_port(const uint8_t *data, uint32_t len) {
    int result = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (data[i] >= '0' && data[i] <= '9')
            result = result * 10 + (data[i] - '0');
    }
    return result;
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

/* ---- transform (shared encode/decode logic) ---------------------------- */

static int64_t transform(int32_t pm_ptr, int32_t pm_len, int is_encode) {
    uint8_t *input_buf = (uint8_t *)(uintptr_t)pm_ptr;
    core_abi_port_map_t pm = core_abi_parse_port_map(input_buf, (uint32_t)pm_len);

    if (pm.count == 0) {
        free(input_buf);
        return CORE_ABI_ERROR;
    }

    const core_abi_port_t *bytes_port = core_abi_find_port(&pm, "bytes");
    const core_abi_port_t *bps_port   = core_abi_find_port(&pm, "bytes_per_sample");
    const core_abi_port_t *width_port = core_abi_find_port(&pm, "width");

    if (!bytes_port || !bps_port || !width_port) {
        core_abi_free_port_map(&pm);
        free(input_buf);
        return CORE_ABI_ERROR;
    }

    int bps   = parse_int_port(bps_port->data, bps_port->data_len);
    int width = parse_int_port(width_port->data, width_port->data_len);

    if (bps <= 0 || width <= 0) {
        core_abi_free_port_map(&pm);
        free(input_buf);
        return CORE_ABI_ERROR;
    }

    uint32_t data_len = bytes_port->data_len;
    int height = (int)(data_len / (uint32_t)(width * bps));

    uint8_t *out = malloc(data_len);
    if (!out) {
        core_abi_free_port_map(&pm);
        free(input_buf);
        return CORE_ABI_ERROR;
    }
    memcpy(out, bytes_port->data, data_len);

    core_abi_free_port_map(&pm);
    free(input_buf);

    if (is_encode)
        diff_rows(out, width, height, bps);
    else
        cumsum_rows(out, width, height, bps);

    /* Build output port-map with a single "bytes" port. */
    core_abi_port_t out_entry;
    out_entry.name = "bytes";
    out_entry.name_len = 5;
    out_entry.data = out;
    out_entry.data_len = data_len;

    core_abi_port_map_t out_pm;
    out_pm.entries = &out_entry;
    out_pm.count = 1;

    uint32_t ser_len;
    uint8_t *ser_buf = core_abi_serialize_port_map(&out_pm, &ser_len);
    free(out);

    if (!ser_buf) return CORE_ABI_ERROR;

    return core_abi_pack_result((uint32_t)(uintptr_t)ser_buf, ser_len);
}

/* ---- Wasm ABI entry points --------------------------------------------- */

__attribute__((export_name("encode")))
int64_t encode(int32_t pm_ptr, int32_t pm_len) {
    return transform(pm_ptr, pm_len, 1);
}

__attribute__((export_name("decode")))
int64_t decode(int32_t pm_ptr, int32_t pm_len) {
    return transform(pm_ptr, pm_len, 0);
}
