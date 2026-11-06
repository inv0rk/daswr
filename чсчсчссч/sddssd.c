
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEPTH 512

typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} json_type;

typedef struct json_value json_value;
typedef struct json_pair  json_pair;

struct json_value {
    json_type type;
    union {
        int    boolean;
        double number;
        char  *string;
        struct { json_value **items; size_t count; } array;
        struct { json_pair   *pairs; size_t count; } object;
    } as;
};

struct json_pair {
    char       *key;
    json_value *value;
};

/* ------------------------------------------------------------------ */
/*  Парсер                                                            */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;
    size_t      pos;
    int         depth;
    const char *error;
} parser;

static void skip_ws(parser *p) {
    while (isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static json_value *parse_value(parser *p);

static json_value *new_value(json_type t) {
    json_value *v = calloc(1, sizeof(*v));
    if (v) v->type = t;
    return v;
}

static int parse_string(parser *p, char **out) {
    if (p->src[p->pos] != '"') { p->error = "expected '\"'"; return -1; }
    p->pos++;
    size_t cap = 16, len = 0;
    char *buf = malloc(cap);
    if (!buf) { p->error = "oom"; return -1; }

    for (;;) {
        unsigned char c = (unsigned char)p->src[p->pos];
        if (c == '\0') { p->error = "unterminated string"; free(buf); return -1; }
        if (c == '"')  { p->pos++; break; }

        if (len + 8 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); p->error = "oom"; return -1; }
            buf = nb;
        }

        if (c == '\\') {
            p->pos++;
            char e = p->src[p->pos++];
            switch (e) {
                case '"':  buf[len++] = '"';  break;
                case '\\': buf[len++] = '\\'; break;
                case '/':  buf[len++] = '/';  break;
                case 'b':  buf[len++] = '\b'; break;
                case 'f':  buf[len++] = '\f'; break;
                case 'n':  buf[len++] = '\n'; break;
                case 'r':  buf[len++] = '\r'; break;
                case 't':  buf[len++] = '\t'; break;
                case 'u': {
                    unsigned cp = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = p->src[p->pos++];
                        cp <<= 4;
                        if      (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= h - 'a' + 10;
                        else if (h >= 'A' && h <= 'F') cp |= h - 'A' + 10;
                        else { p->error = "bad \\u"; free(buf); return -1; }
                    }
                    if (cp < 0x80) {
                        buf[len++] = (char)cp;
                    } else if (cp < 0x800) {
                        buf[len++] = (char)(0xC0 | (cp >> 6));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                    } else {
                        buf[len++] = (char)(0xE0 | (cp >> 12));
                        buf[len++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        buf[len++] = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default:
                    p->error = "bad escape"; free(buf); return -1;
            }
        } else if (c < 0x20) {
            p->error = "control char in string"; free(buf); return -1;
        } else {
            buf[len++] = (char)c;
            p->pos++;
        }
    }
    buf[len] = '\0';
    *out = buf;
    return 0;
}

static json_value *parse_number(parser *p) {
    size_t start = p->pos;
    if (p->src[p->pos] == '-') p->pos++;
    if (!isdigit((unsigned char)p->src[p->pos])) { p->error = "bad number"; return NULL; }
    while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    if (p->src[p->pos] == '.') {
        p->pos++;
        if (!isdigit((unsigned char)p->src[p->pos])) { p->error = "bad number"; return NULL; }
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    if (p->src[p->pos] == 'e' || p->src[p->pos] == 'E') {
        p->pos++;
        if (p->src[p->pos] == '+' || p->src[p->pos] == '-') p->pos++;
        if (!isdigit((unsigned char)p->src[p->pos])) { p->error = "bad number"; return NULL; }
        while (isdigit((unsigned char)p->src[p->pos])) p->pos++;
    }
    json_value *v = new_value(JSON_NUMBER);
    if (!v) { p->error = "oom"; return NULL; }
    v->as.number = strtod(p->src + start, NULL);
    return v;
}

static json_value *parse_array(parser *p) {
    p->pos++;
    json_value *v = new_value(JSON_ARRAY);
    if (!v) { p->error = "oom"; return NULL; }
    size_t cap = 0;
    skip_ws(p);
    if (p->src[p->pos] == ']') { p->pos++; return v; }
    for (;;) {
        json_value *item = parse_value(p);
        if (!item) { goto fail; }
        if (v->as.array.count == cap) {
            cap = cap ? cap * 2 : 4;
            json_value **items = realloc(v->as.array.items, cap * sizeof(*items));
            if (!items) { p->error = "oom"; goto fail; }
            v->as.array.items = items;
        }
        v->as.array.items[v->as.array.count++] = item;
        skip_ws(p);
        if (p->src[p->pos] == ',') { p->pos++; skip_ws(p); continue; }
        if (p->src[p->pos] == ']') { p->pos++; return v; }
        p->error = "expected ',' or ']'";
        goto fail;
    }
fail:
    /* освобождение — ниже определён json_free, поэтому forward decl */
    extern void json_free(json_value *);
    json_free(v);
    return NULL;
}

static json_value *parse_object(parser *p) {
    p->pos++;
    json_value *v = new_value(JSON_OBJECT);
    if (!v) { p->error = "oom"; return NULL; }
    size_t cap = 0;
    skip_ws(p);
    if (p->src[p->pos] == '}') { p->pos++; return v; }
    for (;;) {
        skip_ws(p);
        char *key = NULL;
        if (parse_string(p, &key) != 0) { goto fail; }
        skip_ws(p);
        if (p->src[p->pos] != ':') {
            p->error = "expected ':'"; free(key); goto fail;
        }
        p->pos++;
        json_value *val = parse_value(p);
        if (!val) { free(key); goto fail; }
        if (v->as.object.count == cap) {
            cap = cap ? cap * 2 : 4;
            json_pair *pairs = realloc(v->as.object.pairs, cap * sizeof(*pairs));
            if (!pairs) { p->error = "oom"; free(key); extern void json_free(json_value *); json_free(val); goto fail; }
            v->as.object.pairs = pairs;
        }
        v->as.object.pairs[v->as.object.count].key   = key;
        v->as.object.pairs[v->as.object.count].value = val;
        v->as.object.count++;
        skip_ws(p);
        if (p->src[p->pos] == ',') { p->pos++; continue; }
        if (p->src[p->pos] == '}') { p->pos++; return v; }
        p->error = "expected ',' or '}'";
        goto fail;
    }
fail:
    extern void json_free(json_value *);
    json_free(v);
    return NULL;
}

static json_value *parse_value(parser *p) {
    if (++p->depth > MAX_DEPTH) { p->error = "max depth exceeded"; return NULL; }
    json_value *result = NULL;
    skip_ws(p);
    char c = p->src[p->pos];

    if (c == '{') result = parse_object(p);
    else if (c == '[') result = parse_array(p);
    else if (c == '"') {
        char *s = NULL;
        if (parse_string(p, &s) == 0) {
            result = new_value(JSON_STRING);
            if (!result) { free(s); p->error = "oom"; }
            else result->as.string = s;
        }
    }
    else if (c == 't') {
        if (strncmp(p->src + p->pos, "true", 4) == 0) {
            p->pos += 4;
            result = new_value(JSON_BOOL);
            if (result) result->as.boolean = 1;
        } else p->error = "bad literal";
    }
    else if (c == 'f') {
        if (strncmp(p->src + p->pos, "false", 5) == 0) {
            p->pos += 5;
            result = new_value(JSON_BOOL);
            if (result) result->as.boolean = 0;
        } else p->error = "bad literal";
    }
    else if (c == 'n') {
        if (strncmp(p->src + p->pos, "null", 4) == 0) {
            p->pos += 4;
            result = new_value(JSON_NULL);
        } else p->error = "bad literal";
    }
    else if (c == '-' || isdigit((unsigned char)c)) {
        result = parse_number(p);
    }
    else if (c == '\0') p->error = "unexpected end of input";
    else                p->error = "unexpected character";

    p->depth--;
    return result;
}

json_value *json_parse(const char *text, const char **error) {
    if (!text) { if (error) *error = "null input"; return NULL; }
    parser p = { .src = text, .pos = 0, .depth = 0, .error = NULL };
    json_value *v = parse_value(&p);
    if (!v) { if (error) *error = p.error ? p.error : "parse error"; return NULL; }
    skip_ws(&p);
    if (p.src[p.pos] != '\0') {
        json_free(v);
        if (error) *error = "trailing garbage";
        return NULL;
    }
    if (error) *error = NULL;
    return v;
}

void json_free(json_value *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING: free(v->as.string); break;
        case JSON_ARRAY:
            for (size_t i = 0; i < v->as.array.count; i++) json_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < v->as.object.count; i++) {
                free(v->as.object.pairs[i].key);
                json_free(v->as.object.pairs[i].value);
            }
            free(v->as.object.pairs);
            break;
        default: break;
    }
    free(v);
}

/* ------------------------------------------------------------------ */
/*  Печать                                                            */
/* ------------------------------------------------------------------ */

static void indent(int n) { for (int i = 0; i < n; i++) putchar(' '); }

static void print_string(const char *s) {
    putchar('"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
            case '"':  fputs("\\\"", stdout); break;
            case '\\': fputs("\\\\", stdout); break;
            case '\b': fputs("\\b", stdout);  break;
            case '\f': fputs("\\f", stdout);  break;
            case '\n': fputs("\\n", stdout);  break;
            case '\r': fputs("\\r", stdout);  break;
            case '\t': fputs("\\t", stdout);  break;
            default:
                if (c < 0x20) printf("\\u%04x", c);
                else putchar(c);
        }
    }
    putchar('"');
}

static void print_rec(const json_value *v, int ind) {
    if (!v) { fputs("null", stdout); return; }
    switch (v->type) {
        case JSON_NULL:   fputs("null", stdout); break;
        case JSON_BOOL:   fputs(v->as.boolean ? "true" : "false", stdout); break;
        case JSON_NUMBER:
            if (v->as.number == (double)(long long)v->as.number)
                printf("%lld", (long long)v->as.number);
            else
                printf("%g", v->as.number);
            break;
        case JSON_STRING: print_string(v->as.string); break;
        case JSON_ARRAY:
            putchar('[');
            if (v->as.array.count == 0) { putchar(']'); break; }
            for (size_t i = 0; i < v->as.array.count; i++) {
                if (i) putchar(',');
                putchar('\n'); indent(ind + 2);
                print_rec(v->as.array.items[i], ind + 2);
            }
            putchar('\n'); indent(ind); putchar(']');
            break;
        case JSON_OBJECT:
            putchar('{');
            if (v->as.object.count == 0) { putchar('}'); break; }
            for (size_t i = 0; i < v->as.object.count; i++) {
                if (i) putchar(',');
                putchar('\n'); indent(ind + 2);
                print_string(v->as.object.pairs[i].key);
                fputs(": ", stdout);
                print_rec(v->as.object.pairs[i].value, ind + 2);
            }
            putchar('\n'); indent(ind); putchar('}');
            break;
    }
}

void json_print(const json_value *v, int indent_level) {
    print_rec(v, indent_level);
    putchar('\n');
}

/* ------------------------------------------------------------------ */
/*  main                                                              */
/* ------------------------------------------------------------------ */

static char *read_all(FILE *f) {
    size_t cap = 4096, len = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    size_t n;
    while ((n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if (len == cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
    }
    buf[len] = '\0';
    return buf;
}

int main(int argc, char **argv) {
    FILE *f = stdin;
    if (argc > 1) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            fprintf(stderr, "usage: %s [file.json]\n", argv[0]);
            return 0;
        }
        f = fopen(argv[1], "rb");
        if (!f) { perror(argv[1]); return 1; }
    }

    char *text = read_all(f);
    if (f != stdin) fclose(f);
    if (!text) { fputs("out of memory\n", stderr); return 1; }

    const char *err = NULL;
    json_value *v = json_parse(text, &err);
    free(text);

    if (!v) {
        fprintf(stderr, "parse error: %s\n", err ? err : "unknown");
        return 1;
    }

    json_print(v, 0);
    json_free(v);
    return 0;
}
