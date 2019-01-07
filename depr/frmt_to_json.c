
static int
format_to_json(void *dest, const char *key_name, const char *val_name,
     char **codes, char **names, size_t nitems, size_t total_byte_length)
{
    char *json_str = calloc(total_byte_length + 1, sizeof(char));
    check(json_str, ERR_MEM, EMISS_ERR);
    char buf[0xFF] = {0};
    FRMT_JSON_ENTRY(json_str, buf, 0, key_name, val_name, codes[0], names[0]);
    size_t j = strlen(json_str);
    for (size_t i = 0; i < nitems; i++) {
        memset(buf, 0, strlen(buf));
        FRMT_JSON_ENTRY(&json_str[j], buf, 1, key_name, val_name, codes[i], names[i]);
        j += strlen(&json_str[j]);
    }
    dest = json_str;
    return 1;
error:
    if (json_str) {
        free(json_str);
    }
    return 0;
}
