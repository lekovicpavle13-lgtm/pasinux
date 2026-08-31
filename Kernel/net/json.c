#include "json.h"

#include <stdint.h>

static uint16_t json_strlen(const char* s) {
    uint16_t n = 0u;
    while (s && s[n]) ++n;
    return n;
}

void json_begin(char* buf, uint16_t* pos) {
    buf[*pos] = '{';
    (*pos)++;
}

void json_end(char* buf, uint16_t* pos) {
    buf[*pos] = '}';
    (*pos)++;
    buf[*pos] = '\0';
}

void json_key(char* buf, uint16_t* pos, const char* key) {
    buf[*pos] = '"';
    (*pos)++;
    uint16_t len = json_strlen(key);
    for (uint16_t i = 0u; i < len; ++i) {
        buf[*pos] = key[i];
        (*pos)++;
    }
    buf[*pos] = '"';
    (*pos)++;
}

void json_string_val(char* buf, uint16_t* pos, const char* val) {
    buf[*pos] = '"';
    (*pos)++;
    uint16_t len = json_strlen(val);
    for (uint16_t i = 0u; i < len; ++i) {
        buf[*pos] = val[i];
        (*pos)++;
    }
    buf[*pos] = '"';
    (*pos)++;
}

void json_number_val(char* buf, uint16_t* pos, uint32_t val) {
    char tmp[12];
    uint16_t i = 0u;
    if (val == 0u) {
        tmp[i++] = '0';
    } else {
        while (val > 0u && i < 11u) {
            tmp[i++] = (char)('0' + (val % 10u));
            val /= 10u;
        }
    }
    /* Reverse */
    while (i > 0u) {
        buf[*pos] = tmp[--i];
        (*pos)++;
    }
}

void json_string(char* buf, uint16_t* pos, const char* key, const char* val) {
    json_key(buf, pos, key);
    buf[*pos] = ':';
    (*pos)++;
    json_string_val(buf, pos, val);
}

void json_number(char* buf, uint16_t* pos, const char* key, uint32_t val) {
    json_key(buf, pos, key);
    buf[*pos] = ':';
    (*pos)++;
    json_number_val(buf, pos, val);
}

void json_comma(char* buf, uint16_t* pos) {
    buf[*pos] = ',';
    (*pos)++;
}