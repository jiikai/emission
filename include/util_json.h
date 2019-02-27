/*  @file       util_json.h
    @brief      A set of convenience macros for formatting JSON.
    @author     Joa Käis (github.com/jiikai).
    @copyright  Public domain.
*/

#ifndef _util_json_h
#define _util_json_h

#define JSON_FRMT_KEY_VALUE_PAIR(out, out_n, k_name, v_name, prep_delim, ...)\
    snprintf(out, out_n, (prep_delim ? ",{\"" k_name "\":\"%s\",\"" v_name "\":%s}"\
            : "{\"" k_name "\":\"%s\",\"" v_name "\":%s}"), __VA_ARGS__)

#define JSON_FRMT_KEY_ARRAY_VALUE_PAIR(out, out_n, k_name, v_name, prep_delim, ...)\
    snprintf(out, out_n, (prep_delim ? ",{\"" k_name "\":\"%s\",\"" v_name "\":[%s]}"\
            : "{\"" k_name "\":\"%s\",\"" v_name "\":[%s]}"), __VA_ARGS__)

#define JSON_ENTRY(out, out_n, buf, buf_n, append, key_name, val_name, ...)\
    snprintf(buf, buf_n, (append ? ",{\"%s\":%s}" : "{\"%s\":%s}"), key_name, val_name) < 0 ? -1 :\
    snprintf(out, out_n, buf, __VA_ARGS__)

#endif  /* _util_json_h_ */
