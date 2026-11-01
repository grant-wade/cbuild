#include <stdlib.h>
#include <string.h>

#include "harness.h"

/* Parser position in the input text. */
typedef struct {
    const char *at;
} json_cursor_t;

static json_t *json_new(int type) {
    json_t *value = (json_t *)h_alloc(sizeof(json_t));
    memset(value, 0, sizeof(*value));
    value->type = type;
    return value;
}

static void json_skip_space(json_cursor_t *c) {
    while (*c->at == ' ' || *c->at == '\t' || *c->at == '\n' || *c->at == '\r') c->at++;
}

static int json_hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static size_t json_put_utf8(char *out, unsigned code) {
    if (code < 0x80) {
        out[0] = (char)code;
        return 1;
    }
    if (code < 0x800) {
        out[0] = (char)(0xC0 | (code >> 6));
        out[1] = (char)(0x80 | (code & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | (code >> 12));
    out[1] = (char)(0x80 | ((code >> 6) & 0x3F));
    out[2] = (char)(0x80 | (code & 0x3F));
    return 3;
}

static const char *json_parse_string(json_cursor_t *c) {
    if (*c->at != '"') return NULL;
    c->at++;
    char *out = (char *)h_alloc(strlen(c->at) + 1);
    size_t length = 0;
    while (*c->at != '"') {
        unsigned char ch = (unsigned char)*c->at;
        if (ch < 0x20) return NULL;
        if (ch != '\\') {
            out[length++] = (char)ch;
            c->at++;
            continue;
        }
        c->at++;
        switch (*c->at) {
            case '"': out[length++] = '"'; break;
            case '\\': out[length++] = '\\'; break;
            case '/': out[length++] = '/'; break;
            case 'b': out[length++] = '\b'; break;
            case 'f': out[length++] = '\f'; break;
            case 'n': out[length++] = '\n'; break;
            case 'r': out[length++] = '\r'; break;
            case 't': out[length++] = '\t'; break;
            case 'u': {
                unsigned code = 0;
                for (int i = 1; i <= 4; ++i) {
                    int digit = json_hex_digit(c->at[i]);
                    if (digit < 0) return NULL;
                    code = code * 16 + (unsigned)digit;
                }
                c->at += 4;
                length += json_put_utf8(out + length, code);
                break;
            }
            default: return NULL;
        }
        c->at++;
    }
    c->at++;
    out[length] = '\0';
    return out;
}

static json_t *json_parse_value(json_cursor_t *c);

static void json_append(json_t *value, const char *key, json_t *item) {
    json_t **items = (json_t **)h_alloc(sizeof(json_t *) * (size_t)(value->count + 1));
    const char **keys = (const char **)h_alloc(sizeof(char *) * (size_t)(value->count + 1));
    for (int i = 0; i < value->count; ++i) {
        items[i] = value->items[i];
        if (value->keys) keys[i] = value->keys[i];
    }
    items[value->count] = item;
    keys[value->count] = key;
    value->items = items;
    value->keys = keys;
    value->count++;
}

static json_t *json_parse_container(json_cursor_t *c, int type, char close) {
    json_t *value = json_new(type);
    c->at++;
    json_skip_space(c);
    if (*c->at == close) {
        c->at++;
        return value;
    }
    for (;;) {
        const char *key = NULL;
        json_skip_space(c);
        if (type == JSON_OBJECT) {
            key = json_parse_string(c);
            if (!key) return NULL;
            json_skip_space(c);
            if (*c->at != ':') return NULL;
            c->at++;
        }
        json_t *item = json_parse_value(c);
        if (!item) return NULL;
        json_append(value, key, item);
        json_skip_space(c);
        if (*c->at == ',') {
            c->at++;
            continue;
        }
        if (*c->at != close) return NULL;
        c->at++;
        return value;
    }
}

static json_t *json_parse_value(json_cursor_t *c) {
    json_skip_space(c);
    if (*c->at == '{') return json_parse_container(c, JSON_OBJECT, '}');
    if (*c->at == '[') return json_parse_container(c, JSON_ARRAY, ']');
    if (*c->at == '"') {
        json_t *value = json_new(JSON_STRING);
        value->text = json_parse_string(c);
        return value->text ? value : NULL;
    }
    if (strncmp(c->at, "null", 4) == 0) {
        c->at += 4;
        return json_new(JSON_NULL);
    }
    if (strncmp(c->at, "true", 4) == 0 || strncmp(c->at, "false", 5) == 0) {
        c->at += *c->at == 't' ? 4 : 5;
        return json_new(JSON_BOOL);
    }
    if (*c->at == '-' || (*c->at >= '0' && *c->at <= '9')) {
        char *end = NULL;
        strtod(c->at, &end);
        if (end == c->at) return NULL;
        c->at = end;
        return json_new(JSON_NUMBER);
    }
    return NULL;
}

json_t *json_parse(const char *text) {
    json_cursor_t cursor = { text };
    json_t *value = json_parse_value(&cursor);
    if (!value) return NULL;
    json_skip_space(&cursor);
    return *cursor.at == '\0' ? value : NULL;
}

json_t *json_get(const json_t *object, const char *key) {
    if (!object || object->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < object->count; ++i) {
        if (strcmp(object->keys[i], key) == 0) return object->items[i];
    }
    return NULL;
}
