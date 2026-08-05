#ifndef JSON_H
#define JSON_H

#include <stdint.h>

/* Hand-crafted JSON serializer (no parser).
 * Builds a JSON string by appending to a fixed buffer.
 */

/* Begin an object: writes '{' */
void json_begin(char* buf, uint16_t* pos);

/* End an object: writes '}' */
void json_end(char* buf, uint16_t* pos);

/* Write a quoted string key: "key" */
void json_key(char* buf, uint16_t* pos, const char* key);

/* Write a string value: "value" */
void json_string_val(char* buf, uint16_t* pos, const char* val);

/* Write an integer value: 1234 */
void json_number_val(char* buf, uint16_t* pos, uint32_t val);

/* Convenience: key + ":" + value */
void json_string(char* buf, uint16_t* pos, const char* key, const char* val);
void json_number(char* buf, uint16_t* pos, const char* key, uint32_t val);

/* Write a comma separator */
void json_comma(char* buf, uint16_t* pos);

#endif /* JSON_H */