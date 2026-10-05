/* nv_json.h - the json module. Part of the Novus runtime; included by novus_rt.h.
 *
 * The module is split by job; each part names what it depends on:
 *   nv_json_text.h   limits, the text of strings and numbers
 *   nv_json_write.h  stringify / pretty
 *   nv_json_cursor.h the cursor and lexical helpers of reading
 *   nv_json_scan.h   isValid (strict RFC 8259)
 *   nv_json_string.h reading a string: escapes, surrogate pairs
 *   nv_json_parse.h  parse / tryParse / load / save
 */
#ifndef NV_JSON_H
#define NV_JSON_H

#include "nv_json_text.h"
#include "nv_json_write.h"
#include "nv_json_cursor.h"
#include "nv_json_scan.h"
#include "nv_json_string.h"
#include "nv_json_parse.h"

#endif /* NV_JSON_H */
