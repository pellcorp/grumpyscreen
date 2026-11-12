/* klipper_temp_filter.h  (C99, header-only)
 * Detects Klipper TEMPERATURE_WAIT temperature reports like:
 *   "ok T:200.0 /200.0 B:60.0 /60.0 @:0 B@:0"
 * so your client can ignore them.
 */
#ifndef KLIPPER_TEMP_FILTER_H
#define KLIPPER_TEMP_FILTER_H

#include <stddef.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>

static inline bool kf_is_digit(char c) { return c >= '0' && c <= '9'; }

static inline size_t kf_skip_ws(const char *s, size_t len, size_t i) {
    while (i < len && isspace((unsigned char)s[i])) {
       ++i;
    }
    return i;
}

static inline bool kf_contains(const char *s, size_t len, const char *sub) {
    size_t m = strlen(sub);
    if (m == 0 || m > len) {
       return false;
    }
    /* naive scan; short strings so it’s fine */
    for (size_t i = 0; i + m <= len; ++i) {
       if (memcmp(s + i, sub, m) == 0) {
          return true;
       }
    }
    return false;
}

static inline void kf_trim(const char *s, size_t len, size_t *out_start, size_t *out_len) {
    size_t i = 0, j = len;
    while (i < j && isspace((unsigned char)s[i])) {
       ++i;
    }
    while (j > i && isspace((unsigned char)s[j - 1])) {
       --j;
    }
    *out_start = i;
    *out_len = j - i;
}

/* number := [-+]? digits [ . digits ]? */
static inline bool kf_parse_number(const char *s, size_t len, size_t *io) {
    size_t i = *io, start = i;
    if (i < len && (s[i] == '-' || s[i] == '+')) {
       ++i;
    }
    bool had_digit = false;
    while (i < len && kf_is_digit(s[i])) { ++i; had_digit = true; }
    if (i < len && s[i] == '.') {
        ++i;
        while (i < len && kf_is_digit(s[i])) { ++i; had_digit = true; }
    }
    if (!had_digit || i == start) {
       return false;
    }
    *io = i;
    return true;
}

/* temp_pair := number [ '/' number ]? (second number optional) */
static inline bool kf_parse_temp_pair(const char *s, size_t len, size_t *io) {
    if (!kf_parse_number(s, len, io)) {
       return false;
    }
    size_t i = *io;
    if (i < len && s[i] == '/') {
        size_t save = ++i;
        if (!kf_parse_number(s, len, &i)) { /* allow lone number */ i = save; }
        *io = i;
    }
    return true;
}

/* Parse one token:
   - T or T<d> : <temp_pair>
   - B: <temp_pair>
   - C: <temp_pair>
   - @: <number>
   - B@: <number>
*/
static inline bool kf_parse_token(const char *s, size_t len, size_t *io, bool *saw_extruder_like) {
    size_t i = *io, start = i;

    /* T / T<digit> : */
    if (i < len && (s[i] == 'T' || s[i] == 't')) {
        ++i;
        if (i < len && kf_is_digit(s[i])) {
           ++i;
        } /* T0, T1… */
        if (i >= len || s[i] != ':') { *io = start; return false; }
        ++i;
        if (!kf_parse_temp_pair(s, len, &i)) { *io = start; return false; }
        *io = i;
        *saw_extruder_like = true;
        return true;
    }

    /* B: */
    if (i + 1 < len && (s[i] == 'B' || s[i] == 'b') && s[i + 1] == ':') {
        i += 2;
        if (!kf_parse_temp_pair(s, len, &i)) { *io = start; return false; }
        *io = i;
        return true;
    }

    /* C: (chamber) */
    if (i + 1 < len && (s[i] == 'C' || s[i] == 'c') && s[i + 1] == ':') {
        i += 2;
        if (!kf_parse_temp_pair(s, len, &i)) { *io = start; return false; }
        *io = i;
        return true;
    }

    /* @: power */
    if (i + 1 < len && s[i] == '@' && s[i + 1] == ':') {
        i += 2;
        if (!kf_parse_number(s, len, &i)) { *io = start; return false; }
        *io = i;
        return true;
    }

    /* B@: power */
    if (i + 2 < len && (s[i] == 'B' || s[i] == 'b') && s[i + 1] == '@' && s[i + 2] == ':') {
        i += 3;
        if (!kf_parse_number(s, len, &i)) { *io = start; return false; }
        *io = i;
        return true;
    }

    *io = start;
    return false;
}

/* Core detector on raw buffer */
static inline bool klipper_is_temp_report_buf(const char *buf, size_t len) {
    if (!buf || len == 0) {
       return false;
    }

    size_t off = 0, tlen = 0;
    kf_trim(buf, len, &off, &tlen);
    if (tlen == 0) {
       return false;
    }
    const char *s = buf + off;

    /* Fast reject: typical tokens that appear in temp lines */
    if (!kf_contains(s, tlen, "T:") &&
        !kf_contains(s, tlen, "T0:") &&
        !kf_contains(s, tlen, "B:") &&
        !kf_contains(s, tlen, "@:") &&
        !kf_contains(s, tlen, "B@:")) {
       return false;
    }

    size_t i = 0;

    /* Optional leading "ok" */
    if (tlen >= 2 &&
        (s[0] == 'o' || s[0] == 'O') &&
        (s[1] == 'k' || s[1] == 'K')) {
        i = 2;
        if (i < tlen && isspace((unsigned char)s[i])) {
           ++i;
        }
    }

    bool saw_extruder_like = false;

    while (1) {
        i = kf_skip_ws(s, tlen, i);
        if (i >= tlen) {
           break;
        }

        size_t before = i;
        if (!kf_parse_token(s, tlen, &i, &saw_extruder_like)) {
            /* Unknown alpha token (e.g., "echo:", "Error:") => not a pure temp line */
            if (isalpha((unsigned char)s[before])) {
               return false;
            }

            /* Skip non-alpha separators so we can continue scanning */
            size_t tmp = before;
            while (tmp < tlen &&
                   !isspace((unsigned char)s[tmp]) &&
                   !isalpha((unsigned char)s[tmp])) {
                ++tmp;
            }
            if (tmp == before) {
               return false;
            } /* stuck */
            i = tmp;
        }
    }

    /* Must see at least one T/T0/T1… to classify as temp report */
    return saw_extruder_like;
}

/* Convenience for NUL-terminated strings */
static inline bool klipper_is_temp_report(const char *cstr) {
    if (!cstr) {
       return false;
    }
    return klipper_is_temp_report_buf(cstr, strlen(cstr));
}

#endif /* KLIPPER_TEMP_FILTER_H */
