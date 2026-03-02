/**
 * Tests for tiff_predictor_2.c
 *
 * Compile natively (not Wasm) and run:
 *   zig build test
 *
 * #include-ing the .c file gives us access to static functions
 * (find_int, cumsum_rows, diff_rows). The Wasm ABI wrappers pack a pointer
 * into 32 bits, which only works on wasm32 — so we test the core logic
 * directly instead.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tiff_predictor_2.c"

/* ---- find_int tests ---------------------------------------------------- */

static void test_find_int_basic(void) {
    const char *json = "{\"width\": 10}";
    assert(find_int((const uint8_t *)json, (int32_t)strlen(json), "\"width\"") == 10);
    printf("  PASS  find_int basic\n");
}

static void test_find_int_multiple_keys(void) {
    const char *json = "{\"bytes_per_sample\": 2, \"width\": 256}";
    int32_t len = (int32_t)strlen(json);
    assert(find_int((const uint8_t *)json, len, "\"bytes_per_sample\"") == 2);
    assert(find_int((const uint8_t *)json, len, "\"width\"") == 256);
    printf("  PASS  find_int multiple keys\n");
}

static void test_find_int_missing_key(void) {
    const char *json = "{\"width\": 10}";
    assert(find_int((const uint8_t *)json, (int32_t)strlen(json), "\"height\"") == 0);
    printf("  PASS  find_int missing key\n");
}

static void test_find_int_empty(void) {
    const char *json = "{}";
    assert(find_int((const uint8_t *)json, (int32_t)strlen(json), "\"width\"") == 0);
    printf("  PASS  find_int empty config\n");
}

/* ---- cumsum_rows tests ------------------------------------------------- */

static void test_cumsum_bps1(void) {
    /*
     * 1 row, width=4, bps=1
     * Differenced: [10, 5, 3, 1]  →  Cumsum: [10, 15, 18, 19]
     */
    uint8_t buf[]      = {10, 5, 3, 1};
    uint8_t expected[] = {10, 15, 18, 19};

    cumsum_rows(buf, 4, 1, 1);
    assert(memcmp(buf, expected, 4) == 0);
    printf("  PASS  cumsum bps=1\n");
}

static void test_cumsum_bps1_wrapping(void) {
    /*
     * bps=1: values wrap at 256 (uint8_t).
     * [200, 100] → [200, 44]  (200+100 = 300, mod 256 = 44)
     */
    uint8_t buf[] = {200, 100};

    cumsum_rows(buf, 2, 1, 1);
    assert(buf[0] == 200);
    assert(buf[1] == (uint8_t)(200 + 100));
    printf("  PASS  cumsum bps=1 wrapping\n");
}

static void test_cumsum_bps2(void) {
    /*
     * 1 row, width=3, bps=2
     * [1000, 500, 200] → [1000, 1500, 1700]
     */
    uint16_t buf[]      = {1000, 500, 200};
    uint16_t expected[] = {1000, 1500, 1700};

    cumsum_rows((uint8_t *)buf, 3, 1, 2);
    assert(memcmp(buf, expected, 6) == 0);
    printf("  PASS  cumsum bps=2\n");
}

static void test_cumsum_bps4(void) {
    /*
     * 1 row, width=3, bps=4
     * [100000, 50000, 25000] → [100000, 150000, 175000]
     */
    uint32_t buf[]      = {100000, 50000, 25000};
    uint32_t expected[] = {100000, 150000, 175000};

    cumsum_rows((uint8_t *)buf, 3, 1, 4);
    assert(memcmp(buf, expected, 12) == 0);
    printf("  PASS  cumsum bps=4\n");
}

static void test_cumsum_multirow(void) {
    /*
     * 2 rows, width=3, bps=1 — rows decoded independently.
     * Row 0: [10, 5, 3] → [10, 15, 18]
     * Row 1: [20, 2, 1] → [20, 22, 23]
     */
    uint8_t buf[]      = {10, 5, 3, 20, 2, 1};
    uint8_t expected[] = {10, 15, 18, 20, 22, 23};

    cumsum_rows(buf, 3, 2, 1);
    assert(memcmp(buf, expected, 6) == 0);
    printf("  PASS  cumsum multi-row\n");
}

static void test_cumsum_single_col(void) {
    /*
     * width=1: nothing to accumulate, buffer should be unchanged.
     */
    uint8_t buf[] = {42, 99};
    uint8_t expected[] = {42, 99};

    cumsum_rows(buf, 1, 2, 1);
    assert(memcmp(buf, expected, 2) == 0);
    printf("  PASS  cumsum single column\n");
}

/* ---- diff_rows tests --------------------------------------------------- */

static void test_diff_bps1(void) {
    /*
     * 1 row, width=4, bps=1
     * Original: [10, 15, 18, 19]  →  Differenced: [10, 5, 3, 1]
     */
    uint8_t buf[]      = {10, 15, 18, 19};
    uint8_t expected[] = {10, 5, 3, 1};

    diff_rows(buf, 4, 1, 1);
    assert(memcmp(buf, expected, 4) == 0);
    printf("  PASS  diff bps=1\n");
}

static void test_diff_bps1_wrapping(void) {
    /*
     * bps=1: values wrap at 256 (uint8_t).
     * [200, 44] → [200, 100]  (44 - 200 = -156, mod 256 = 100)
     */
    uint8_t buf[] = {200, 44};

    diff_rows(buf, 2, 1, 1);
    assert(buf[0] == 200);
    assert(buf[1] == (uint8_t)(44 - 200));
    printf("  PASS  diff bps=1 wrapping\n");
}

static void test_diff_bps2(void) {
    /*
     * 1 row, width=3, bps=2
     * [1000, 1500, 1700] → [1000, 500, 200]
     */
    uint16_t buf[]      = {1000, 1500, 1700};
    uint16_t expected[] = {1000, 500, 200};

    diff_rows((uint8_t *)buf, 3, 1, 2);
    assert(memcmp(buf, expected, 6) == 0);
    printf("  PASS  diff bps=2\n");
}

static void test_diff_bps4(void) {
    /*
     * 1 row, width=3, bps=4
     * [100000, 150000, 175000] → [100000, 50000, 25000]
     */
    uint32_t buf[]      = {100000, 150000, 175000};
    uint32_t expected[] = {100000, 50000, 25000};

    diff_rows((uint8_t *)buf, 3, 1, 4);
    assert(memcmp(buf, expected, 12) == 0);
    printf("  PASS  diff bps=4\n");
}

static void test_diff_multirow(void) {
    /*
     * 2 rows, width=3, bps=1 — rows encoded independently.
     * Row 0: [10, 15, 18] → [10, 5, 3]
     * Row 1: [20, 22, 23] → [20, 2, 1]
     */
    uint8_t buf[]      = {10, 15, 18, 20, 22, 23};
    uint8_t expected[] = {10, 5, 3, 20, 2, 1};

    diff_rows(buf, 3, 2, 1);
    assert(memcmp(buf, expected, 6) == 0);
    printf("  PASS  diff multi-row\n");
}

static void test_diff_single_col(void) {
    /*
     * width=1: nothing to difference, buffer should be unchanged.
     */
    uint8_t buf[] = {42, 99};
    uint8_t expected[] = {42, 99};

    diff_rows(buf, 1, 2, 1);
    assert(memcmp(buf, expected, 2) == 0);
    printf("  PASS  diff single column\n");
}

/* ---- roundtrip tests --------------------------------------------------- */

static void test_roundtrip_encode_decode(void) {
    /*
     * Verify that diff_rows and cumsum_rows are perfect inverses.
     */
    uint16_t original[] = {1000, 1500, 1700, 2000, 2100, 2300};
    uint16_t buf[6];

    /* encode then decode */
    memcpy(buf, original, 12);
    diff_rows((uint8_t *)buf, 3, 2, 2);
    cumsum_rows((uint8_t *)buf, 3, 2, 2);
    assert(memcmp(buf, original, 12) == 0);

    /* decode then encode */
    memcpy(buf, original, 12);
    cumsum_rows((uint8_t *)buf, 3, 2, 2);
    diff_rows((uint8_t *)buf, 3, 2, 2);
    assert(memcmp(buf, original, 12) == 0);

    printf("  PASS  roundtrip encode/decode\n");
}

/* ---- decode config-validation tests ------------------------------------ */

static void test_decode_invalid_config(void) {
    uint8_t data[] = {1, 2, 3, 4};

    /* bps = 0 → should return 0 */
    const char *cfg1 = "{\"bytes_per_sample\": 0, \"width\": 4}";
    assert(decode(data, 4, (const uint8_t *)cfg1, (int32_t)strlen(cfg1)) == 0);

    /* width = 0 → should return 0 */
    const char *cfg2 = "{\"bytes_per_sample\": 1, \"width\": 0}";
    assert(decode(data, 4, (const uint8_t *)cfg2, (int32_t)strlen(cfg2)) == 0);

    /* missing keys entirely */
    const char *cfg3 = "{}";
    assert(decode(data, 4, (const uint8_t *)cfg3, (int32_t)strlen(cfg3)) == 0);

    printf("  PASS  decode invalid config\n");
}

/* ---- encode config-validation tests ------------------------------------ */

static void test_encode_invalid_config(void) {
    uint8_t data[] = {1, 2, 3, 4};

    /* bps = 0 → should return 0 */
    const char *cfg1 = "{\"bytes_per_sample\": 0, \"width\": 4}";
    assert(encode(data, 4, (const uint8_t *)cfg1, (int32_t)strlen(cfg1)) == 0);

    /* width = 0 → should return 0 */
    const char *cfg2 = "{\"bytes_per_sample\": 1, \"width\": 0}";
    assert(encode(data, 4, (const uint8_t *)cfg2, (int32_t)strlen(cfg2)) == 0);

    /* missing keys entirely */
    const char *cfg3 = "{}";
    assert(encode(data, 4, (const uint8_t *)cfg3, (int32_t)strlen(cfg3)) == 0);

    printf("  PASS  encode invalid config\n");
}

/* ---- main -------------------------------------------------------------- */

int main(void) {
    printf("find_int:\n");
    test_find_int_basic();
    test_find_int_multiple_keys();
    test_find_int_missing_key();
    test_find_int_empty();

    printf("cumsum_rows:\n");
    test_cumsum_bps1();
    test_cumsum_bps1_wrapping();
    test_cumsum_bps2();
    test_cumsum_bps4();
    test_cumsum_multirow();
    test_cumsum_single_col();

    printf("diff_rows:\n");
    test_diff_bps1();
    test_diff_bps1_wrapping();
    test_diff_bps2();
    test_diff_bps4();
    test_diff_multirow();
    test_diff_single_col();

    printf("roundtrip:\n");
    test_roundtrip_encode_decode();

    printf("decode:\n");
    test_decode_invalid_config();

    printf("encode:\n");
    test_encode_invalid_config();

    printf("\nAll tests passed.\n");
    return 0;
}
