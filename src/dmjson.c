#define DMOD_ENABLE_REGISTRATION ON
#include "dmod.h"
#include "dmjson.h"
#include <limits.h>

#define JSON_MAGIC UINT32_C(0x4a534f4e)

typedef struct node node_t;
struct node {
    node_t *parent, *next;
    char *key;
    size_t key_len;
    union {
        struct { node_t *child, *tail; size_t count; };
        struct { char *text; size_t text_len; };
    };
    unsigned char type, state;
};
struct dmjson {
    uint32_t magic;
    node_t *root;
    size_t error, nodes;
};
typedef struct {
    const char *begin, *p, *end;
    node_t *root, *current;
    size_t depth, nodes, height;
    int error;
} parser_t;
typedef struct {
    char *buffer, *chunk;
    void *file;
    size_t capacity, used, pending;
    int error;
} writer_t;

static bool valid(dmjson_t ctx) { return ctx && ctx->magic == JSON_MAGIC; }
static size_t length(const char *s) { size_t n = 0; while (s[n]) ++n; return n; }
static bool container(node_t *n) { return n && (n->type == DMJSON_ARRAY || n->type == DMJSON_OBJECT); }
static bool digit(unsigned char c) { return c >= '0' && c <= '9'; }

static char *copy_bytes(const char *s, size_t n)
{
    char *out;
    if (n == SIZE_MAX) return NULL;
    out = Dmod_Malloc(n + 1);
    if (out) {
        for (size_t i = 0; i < n; ++i) out[i] = s[i];
        out[n] = 0;
    }
    return out;
}

/* Intrusive links are the tree itself: no auxiliary traversal stack/list. */
static void free_tree(node_t *n)
{
    node_t *next;
    if (n) n->parent = NULL;
    while (n) {
        if (container(n) && n->child) {
            next = n->child;
            n->child = NULL;
        } else {
            next = n->next ? n->next : n->parent;
            Dmod_Free(n->key);
            if (!container(n)) Dmod_Free(n->text);
            Dmod_Free(n);
        }
        n = next;
    }
}

static node_t *new_node(dmjson_type_t type)
{
    node_t *n = Dmod_Malloc(sizeof(*n));
    if (!n) return NULL;
    n->parent = n->next = NULL;
    n->key = NULL;
    n->key_len = 0;
    n->type = (unsigned char)type;
    n->state = 0;
    if (container(n)) { n->child = n->tail = NULL; n->count = 0; }
    else { n->text = NULL; n->text_len = 0; }
    return n;
}

static void append(node_t *parent, node_t *n)
{
    n->parent = parent;
    if (parent->tail) parent->tail->next = n;
    else parent->child = n;
    parent->tail = n;
    ++parent->count;
}

static void whitespace(parser_t *p)
{
    while (p->p < p->end && (*p->p == ' ' || *p->p == '\t' ||
           *p->p == '\r' || *p->p == '\n')) ++p->p;
}

static int hex(unsigned char c)
{
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool hex4(parser_t *p, uint32_t *value)
{
    *value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (p->p == p->end || hex((unsigned char)*p->p) < 0) return false;
        *value = (*value << 4) | (unsigned)hex((unsigned char)*p->p++);
    }
    return true;
}

/* Consume a Unicode scalar, rejecting overlong UTF-8 and surrogate encodings. */
static bool read_utf8(const char **cursor, const char *end, uint32_t *value)
{
    unsigned char first = (unsigned char)*(*cursor)++;
    unsigned remaining;
    uint32_t minimum;
    if (first < 0x80) { *value = first; return first >= 0x20; }
    if (first >= 0xc2 && first <= 0xdf) { remaining = 1; minimum = 0x80; *value = first & 31; }
    else if (first >= 0xe0 && first <= 0xef) { remaining = 2; minimum = 0x800; *value = first & 15; }
    else if (first >= 0xf0 && first <= 0xf4) { remaining = 3; minimum = 0x10000; *value = first & 7; }
    else return false;
    while (remaining--) {
        if (*cursor == end || ((unsigned char)**cursor & 0xc0) != 0x80) return false;
        *value = (*value << 6) | ((unsigned char)*(*cursor)++ & 63);
    }
    return *value >= minimum && *value <= 0x10ffff && !(*value >= 0xd800 && *value <= 0xdfff);
}

static bool escaped_scalar(parser_t *p, uint32_t *v)
{
    uint32_t low;
    unsigned char c;
    if (p->p == p->end) return false;
    c = (unsigned char)*p->p++;
    switch (c) {
    case '"': case '\\': case '/': *v = c; return true;
    case 'b': *v = 8; return true;
    case 'f': *v = 12; return true;
    case 'n': *v = 10; return true;
    case 'r': *v = 13; return true;
    case 't': *v = 9; return true;
    case 'u': break;
    default: return false;
    }
    if (!hex4(p, v)) return false;
    if (*v >= 0xdc00 && *v <= 0xdfff) return false;
    if (*v < 0xd800 || *v > 0xdbff) return true;
    if (p->end - p->p < 2 || p->p[0] != '\\' || p->p[1] != 'u') return false;
    p->p += 2;
    if (!hex4(p, &low) || low < 0xdc00 || low > 0xdfff) return false;
    *v = 0x10000 + ((*v - 0xd800) << 10) + low - 0xdc00;
    return true;
}

static void encode_scalar(char *out, size_t *n, uint32_t v)
{
    unsigned bytes = v < 0x80 ? 1 : v < 0x800 ? 2 : v < 0x10000 ? 3 : 4;
    if (out) {
        if (bytes == 1) out[*n] = (char)v;
        else {
            out[*n] = (char)((bytes == 2 ? 0xc0 : bytes == 3 ? 0xe0 : 0xf0) | (v >> (6 * (bytes - 1))));
            for (unsigned i = 1; i < bytes; ++i)
                out[*n + i] = (char)(0x80 | ((v >> (6 * (bytes - i - 1))) & 63));
        }
    }
    *n += bytes;
}

static bool string_pass(parser_t *p, char *out, size_t *n)
{
    uint32_t v;
    *n = 0;
    while (p->p < p->end && *p->p != '"') {
        if (*p->p == '\\') {
            ++p->p;
            if (!escaped_scalar(p, &v)) return false;
        } else if (!read_utf8(&p->p, p->end, &v)) return false;
        encode_scalar(out, n, v);
    }
    if (p->p == p->end) return false;
    ++p->p;
    if (out) out[*n] = 0;
    return true;
}

static char *parse_string(parser_t *p, size_t *n)
{
    const char *start;
    char *out;
    if (p->p == p->end || *p->p != '"') { p->error = DMJSON_ERR_SYNTAX; return NULL; }
    start = ++p->p;
    if (!string_pass(p, NULL, n)) { p->error = DMJSON_ERR_SYNTAX; return NULL; }
    out = Dmod_Malloc(*n + 1);
    if (!out) { p->error = DMJSON_ERR_MEMORY; return NULL; }
    p->p = start;
    (void)string_pass(p, out, n);
    return out;
}

static bool take(parser_t *p, char c)
{
    if (p->p < p->end && *p->p == c) { ++p->p; return true; }
    return false;
}

static bool number(parser_t *p)
{
    (void)take(p, '-');
    if (!take(p, '0')) {
        if (p->p == p->end || *p->p < '1' || *p->p > '9') return false;
        do { ++p->p; } while (p->p < p->end && digit((unsigned char)*p->p));
    }
    if (take(p, '.')) {
        if (p->p == p->end || !digit((unsigned char)*p->p)) return false;
        do { ++p->p; } while (p->p < p->end && digit((unsigned char)*p->p));
    }
    if (take(p, 'e') || take(p, 'E')) {
        if (!take(p, '+')) (void)take(p, '-');
        if (p->p == p->end || !digit((unsigned char)*p->p)) return false;
        do { ++p->p; } while (p->p < p->end && digit((unsigned char)*p->p));
    }
    return true;
}

static bool literal(parser_t *p, const char *s)
{
    while (*s) if (p->p == p->end || *p->p++ != *s++) return false;
    return true;
}

static bool scalar(parser_t *p, node_t *n)
{
    const char *start = p->p;
    if (*p->p == '"') {
        n->type = DMJSON_STRING;
        n->text = parse_string(p, &n->text_len);
        return n->text != NULL;
    }
    if (*p->p == 't' || *p->p == 'f') {
        n->type = DMJSON_BOOL;
        n->state = *p->p == 't';
        return literal(p, n->state ? "true" : "false");
    }
    if (*p->p == 'n') { n->type = DMJSON_NULL; return literal(p, "null"); }
    if (!number(p)) return false;
    n->type = DMJSON_NUMBER;
    n->text_len = (size_t)(p->p - start);
    n->text = copy_bytes(start, n->text_len);
    if (!n->text) p->error = DMJSON_ERR_MEMORY;
    return n->text != NULL;
}

static bool parse_value(parser_t *p)
{
    node_t *n;
    if (p->nodes >= DMJSON_MAX_NODES) { p->error = DMJSON_ERR_LIMIT; return false; }
    n = new_node(DMJSON_NULL);
    if (!n) { p->error = DMJSON_ERR_MEMORY; return false; }
    ++p->nodes;
    if (p->current) append(p->current, n);
    else p->root = n;
    if (p->current && p->current->type == DMJSON_OBJECT) {
        n->key = parse_string(p, &n->key_len);
        if (!n->key) return false;
        whitespace(p);
        if (!take(p, ':')) return false;
        whitespace(p);
    }
    if (p->p == p->end) return false;
    if (*p->p == '[' || *p->p == '{') {
        if (p->depth >= DMJSON_MAX_DEPTH) { p->error = DMJSON_ERR_LIMIT; return false; }
        ++p->depth;
        if (p->depth > p->height) p->height = p->depth;
        n->type = *p->p++ == '[' ? DMJSON_ARRAY : DMJSON_OBJECT;
        n->child = n->tail = NULL;
        n->count = 0;
        p->current = n;
        return true;
    }
    return scalar(p, n);
}

/* Container state 0 = first value/close, 1 = separator/close, 2 = value required. */
static bool parse_step(parser_t *p)
{
    node_t *n = p->current;
    char close = n->type == DMJSON_ARRAY ? ']' : '}';
    whitespace(p);
    if (n->state != 2 && take(p, close)) {
        --p->depth;
        p->current = n->parent;
        return true;
    }
    if (n->state == 1) {
        if (!take(p, ',')) return false;
        n->state = 2;
        whitespace(p);
    }
    n->state = 1;
    return parse_value(p);
}

static parser_t *parse(const char *text, size_t size)
{
    parser_t *p = Dmod_Malloc(sizeof(*p));
    if (!p) return NULL;
    p->begin = p->p = text;
    p->end = text + size;
    p->root = p->current = NULL;
    p->nodes = p->depth = p->height = 0;
    p->error = DMJSON_OK;
    whitespace(p);
    if (!parse_value(p)) goto fail;
    while (p->current) if (!parse_step(p)) goto fail;
    whitespace(p);
    if (p->p != p->end) goto fail;
    return p;
fail:
    if (!p->error) p->error = DMJSON_ERR_SYNTAX;
    free_tree(p->root);
    p->root = NULL;
    return p;
}

/* Decode a JSON Pointer segment one byte at a time; no temporary key buffer. */
static int pointer_byte(const char **p, const char *end)
{
    int c;
    if (*p == end) return -1;
    c = (unsigned char)*(*p)++;
    if (c != '~') return c;
    if (*p == end || (**p != '0' && **p != '1')) return -2;
    return *(*p)++ == '0' ? '~' : '/';
}

static bool key_matches(node_t *n, const char *p, const char *end)
{
    size_t i = 0;
    int c;
    while ((c = pointer_byte(&p, end)) >= 0) {
        if (i == n->key_len || (unsigned char)n->key[i++] != c) return false;
    }
    return c == -1 && i == n->key_len;
}

static node_t *indexed(node_t *n, size_t index)
{
    n = n->child;
    while (n && index--) n = n->next;
    return n;
}

static node_t *segment(node_t *n, const char *p, const char *end)
{
    node_t *found = NULL;
    size_t index = 0;
    if (!n) return NULL;
    if (n->type == DMJSON_OBJECT) {
        for (n = n->child; n; n = n->next) if (key_matches(n, p, end)) found = n;
        return found;
    }
    if (n->type != DMJSON_ARRAY || p == end || (*p == '0' && end - p != 1)) return NULL;
    for (; p < end; ++p) {
        if (!digit((unsigned char)*p) || index > (SIZE_MAX - (unsigned)(*p - '0')) / 10) return NULL;
        index = index * 10 + (unsigned)(*p - '0');
    }
    return index < n->count ? indexed(n, index) : NULL;
}

static node_t *find_range(dmjson_t ctx, const char *path, const char *end)
{
    node_t *n = valid(ctx) ? ctx->root : NULL;
    const char *start;
    if (!path) return NULL;
    while (path < end) {
        if (*path++ != '/') return NULL;
        start = path;
        while (path < end && *path != '/') ++path;
        n = segment(n, start, path);
        if (!n) return NULL;
    }
    return n;
}

static node_t *find(dmjson_t ctx, const char *path)
{
    return path ? find_range(ctx, path, path + length(path)) : NULL;
}

#define FILE_CHUNK_SIZE 128u

static void flush(writer_t *w)
{
    if (!w->error && w->pending && Dmod_FileWrite(w->chunk, 1, w->pending, w->file) != w->pending)
        w->error = DMJSON_ERR_FILE;
    w->pending = 0;
}

static void emit(writer_t *w, const char *data, size_t n)
{
    if (w->error) return;
    if (n > (size_t)INT_MAX - 1 - w->used) { w->error = DMJSON_ERR_LIMIT; return; }
    if (w->file) {
        for (size_t i = 0; i < n && !w->error; ++i) {
            w->chunk[w->pending++] = data[i];
            if (w->pending == FILE_CHUNK_SIZE) flush(w);
        }
    } else if (w->buffer) {
        if (n >= w->capacity - w->used) { w->error = DMJSON_ERR_BUFFER; return; }
        for (size_t i = 0; i < n; ++i) w->buffer[w->used + i] = data[i];
    }
    w->used += n;
}

static void quote(writer_t *w, const char *text, size_t n)
{
    size_t start = 0;
    char escape[6] = {'\\', 'u', '0', '0', '0', '0'};
    const char *hexes = "0123456789abcdef";
    emit(w, "\"", 1);
    for (size_t i = 0; i < n && !w->error; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 32 && c != '"' && c != '\\') continue;
        emit(w, text + start, i - start);
        if (c == '"' || c == '\\') {
            escape[1] = (char)c;
            emit(w, escape, 2);
            escape[1] = 'u';
        } else {
            escape[4] = hexes[c >> 4]; escape[5] = hexes[c & 15];
            emit(w, escape, 6);
        }
        start = i + 1;
    }
    emit(w, text + start, n - start);
    emit(w, "\"", 1);
}

static void write_value(writer_t *w, node_t *n)
{
    switch (n->type) {
    case DMJSON_NULL: emit(w, "null", 4); break;
    case DMJSON_BOOL: emit(w, n->state ? "true" : "false", n->state ? 4 : 5); break;
    case DMJSON_NUMBER: emit(w, n->text, n->text_len); break;
    case DMJSON_STRING: quote(w, n->text, n->text_len); break;
    case DMJSON_ARRAY: emit(w, "[", 1); break;
    case DMJSON_OBJECT: emit(w, "{", 1); break;
    default: w->error = DMJSON_ERR_INVALID; break;
    }
}

static int write_tree(writer_t *w, node_t *root)
{
    node_t *n = root;
    if (!n) return DMJSON_ERR_NOT_FOUND;
    while (n && !w->error) {
        if (n->parent && n->parent->type == DMJSON_OBJECT) {
            quote(w, n->key, n->key_len);
            emit(w, ":", 1);
        }
        write_value(w, n);
        if (container(n) && n->child) { n = n->child; continue; }
        if (container(n)) emit(w, n->type == DMJSON_ARRAY ? "]" : "}", 1);
        while (n != root && !n->next) {
            n = n->parent;
            emit(w, n->type == DMJSON_ARRAY ? "]" : "}", 1);
        }
        if (n == root) break;
        emit(w, ",", 1);
        n = n->next;
    }
    if (w->error) return w->error;
    if (w->buffer) w->buffer[w->used] = 0;
    return (int)w->used + 1;
}

static writer_t *writer(char *buffer, size_t capacity, void *file)
{
    writer_t *w = Dmod_Malloc(sizeof(*w));
    if (w) {
        w->buffer = buffer; w->capacity = capacity; w->file = file;
        w->chunk = NULL; w->pending = w->used = 0; w->error = 0;
    }
    return w;
}

dmod_dmjson_api_declaration(1.0, dmjson_t, _create, (void))
{
    dmjson_t ctx = Dmod_Malloc(sizeof(*ctx));
    if (ctx) { ctx->magic = JSON_MAGIC; ctx->root = NULL; ctx->error = SIZE_MAX; ctx->nodes = 0; }
    return ctx;
}

dmod_dmjson_api_declaration(1.0, void, _destroy, (dmjson_t ctx))
{
    if (!valid(ctx)) return;
    ctx->magic = 0;
    free_tree(ctx->root);
    Dmod_Free(ctx);
}

dmod_dmjson_api_declaration(1.0, bool, _is_valid, (dmjson_t ctx)) { return valid(ctx); }

dmod_dmjson_api_declaration(1.0, int, _parse_buffer, (dmjson_t ctx, const char *json, size_t size))
{
    parser_t *p;
    int result;
    if (!valid(ctx) || !json || size > PTRDIFF_MAX) return DMJSON_ERR_INVALID;
    p = parse(json, size);
    if (!p) return DMJSON_ERR_MEMORY;
    result = p->error;
    ctx->error = result ? (size_t)(p->p - p->begin) : SIZE_MAX;
    if (!result) {
        free_tree(ctx->root);
        ctx->root = p->root;
        ctx->nodes = p->nodes;
    }
    Dmod_Free(p);
    return result;
}

dmod_dmjson_api_declaration(1.0, int, _parse_string, (dmjson_t ctx, const char *json))
{
    return json ? dmjson_parse_buffer(ctx, json, length(json)) : DMJSON_ERR_INVALID;
}

dmod_dmjson_api_declaration(1.0, size_t, _error_offset, (dmjson_t ctx))
{
    return valid(ctx) ? ctx->error : SIZE_MAX;
}

dmod_dmjson_api_declaration(1.0, int, _generate_string, (dmjson_t ctx, char *buffer, size_t capacity))
{
    writer_t *w;
    int result;
    if (!valid(ctx)) return DMJSON_ERR_INVALID;
    w = writer(NULL, 0, NULL);
    if (!w) return DMJSON_ERR_MEMORY;
    result = write_tree(w, ctx->root);
    if (result > 0 && buffer) {
        if ((size_t)result > capacity) result = DMJSON_ERR_BUFFER;
        else { w->buffer = buffer; w->capacity = capacity; w->used = 0; result = write_tree(w, ctx->root); }
    }
    Dmod_Free(w);
    return result;
}

/* File APIs use a heap buffer sized to the file, no fixed I/O stack buffers. */
dmod_dmjson_api_declaration(1.0, int, _parse_file, (dmjson_t ctx, const char *filename))
{
    void *file;
    char *buffer;
    Dmod_FileSize_t size;
    size_t used = 0, got;
    int result = DMJSON_ERR_FILE;
    if (!valid(ctx) || !filename) return DMJSON_ERR_INVALID;
    file = Dmod_FileOpen(filename, "rb");
    if (!file) return DMJSON_ERR_FILE;
    size = Dmod_FileSize(file);
    if (size > PTRDIFF_MAX) { Dmod_FileClose(file); return DMJSON_ERR_LIMIT; }
    buffer = Dmod_Malloc((size_t)size + 1);
    if (!buffer) { Dmod_FileClose(file); return DMJSON_ERR_MEMORY; }
    while (used < (size_t)size) {
        got = Dmod_FileRead(buffer + used, 1, (size_t)size - used, file);
        if (!got) break;
        used += got;
    }
    if (used == (size_t)size) result = dmjson_parse_buffer(ctx, buffer, used);
    Dmod_Free(buffer);
    Dmod_FileClose(file);
    return result;
}

dmod_dmjson_api_declaration(1.0, int, _generate_file, (dmjson_t ctx, const char *filename))
{
    void *file;
    writer_t *w;
    int result;
    if (!valid(ctx) || !filename) return DMJSON_ERR_INVALID;
    if (!ctx->root) return DMJSON_ERR_NOT_FOUND;
    w = writer(NULL, 0, NULL);
    if (!w) return DMJSON_ERR_MEMORY;
    w->chunk = Dmod_Malloc(FILE_CHUNK_SIZE);
    if (!w->chunk) { Dmod_Free(w); return DMJSON_ERR_MEMORY; }
    file = Dmod_FileOpen(filename, "wb");
    if (!file) { Dmod_Free(w->chunk); Dmod_Free(w); return DMJSON_ERR_FILE; }
    w->file = file;
    result = write_tree(w, ctx->root);
    flush(w);
    if (w->error) result = w->error;
    Dmod_FileClose(file);
    Dmod_Free(w->chunk);
    Dmod_Free(w);
    return result < 0 ? result : DMJSON_OK;
}

dmod_dmjson_api_declaration(1.0, dmjson_type_t, _type, (dmjson_t ctx, const char *path))
{
    node_t *n = find(ctx, path);
    return n ? n->type : DMJSON_MISSING;
}

dmod_dmjson_api_declaration(1.0, size_t, _count, (dmjson_t ctx, const char *path))
{
    node_t *n = find(ctx, path);
    return container(n) ? n->count : 0;
}

static node_t *key_node(dmjson_t ctx, const char *path, size_t index)
{
    node_t *n = find(ctx, path);
    return n && n->type == DMJSON_OBJECT && index < n->count ? indexed(n, index) : NULL;
}

dmod_dmjson_api_declaration(1.0, const char*, _key_name, (dmjson_t ctx, const char *path, size_t index))
{
    node_t *n = key_node(ctx, path, index);
    return n ? n->key : NULL;
}

dmod_dmjson_api_declaration(1.0, size_t, _key_length, (dmjson_t ctx, const char *path, size_t index))
{
    node_t *n = key_node(ctx, path, index);
    return n ? n->key_len : 0;
}

dmod_dmjson_api_declaration(1.0, const char*, _get_string, (dmjson_t ctx, const char *path, const char *fallback))
{
    node_t *n = find(ctx, path);
    return n && n->type == DMJSON_STRING ? n->text : fallback;
}

dmod_dmjson_api_declaration(1.0, size_t, _string_length, (dmjson_t ctx, const char *path))
{
    node_t *n = find(ctx, path);
    return n && n->type == DMJSON_STRING ? n->text_len : 0;
}

dmod_dmjson_api_declaration(1.0, const char*, _get_number, (dmjson_t ctx, const char *path, const char *fallback))
{
    node_t *n = find(ctx, path);
    return n && n->type == DMJSON_NUMBER ? n->text : fallback;
}

dmod_dmjson_api_declaration(1.0, bool, _get_bool, (dmjson_t ctx, const char *path, bool fallback))
{
    node_t *n = find(ctx, path);
    return n && n->type == DMJSON_BOOL ? n->state != 0 : fallback;
}

dmod_dmjson_api_declaration(1.0, int32_t, _get_int, (dmjson_t ctx, const char *path, int32_t fallback))
{
    node_t *n = find(ctx, path);
    const char *s;
    uint32_t value = 0, limit;
    bool negative;
    if (!n || n->type != DMJSON_NUMBER) return fallback;
    s = n->text;
    negative = *s == '-';
    if (negative) ++s;
    limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    for (; *s; ++s) {
        if (!digit((unsigned char)*s) || value > (limit - (unsigned)(*s - '0')) / 10) return fallback;
        value = value * 10 + (unsigned)(*s - '0');
    }
    if (negative && value == UINT32_C(2147483648)) return INT32_MIN;
    return negative ? -(int32_t)value : (int32_t)value;
}

static node_t *next_node(node_t *n, node_t *root)
{
    if (container(n) && n->child) return n->child;
    while (n != root && !n->next) n = n->parent;
    return n == root ? NULL : n->next;
}

static size_t tree_size(node_t *root)
{
    size_t count = 0;
    for (node_t *n = root; n; n = next_node(n, root)) ++count;
    return count;
}

static bool valid_segment(const char *p, const char *end)
{
    int c;
    do { c = pointer_byte(&p, end); } while (c >= 0);
    return c == -1;
}

static char *copy_key(const char *p, const char *end, size_t *size)
{
    const char *start = p;
    char *key;
    int c;
    *size = 0;
    while (pointer_byte(&p, end) >= 0) ++*size;
    key = Dmod_Malloc(*size + 1);
    if (!key) return NULL;
    p = start;
    *size = 0;
    while ((c = pointer_byte(&p, end)) >= 0) key[(*size)++] = (char)c;
    key[*size] = 0;
    return key;
}

/* All validation/allocation precedes relinking; failures leave ctx intact. */
static int replace_member(node_t *parent, node_t *old, node_t *value)
{
    node_t *previous = NULL;
    if (!old) { append(parent, value); return DMJSON_OK; }
    for (node_t *n = parent->child; n != old; n = n->next) previous = n;
    value->parent = parent;
    value->next = old->next;
    value->key = old->key;
    value->key_len = old->key_len;
    old->key = NULL;
    if (previous) previous->next = value;
    else parent->child = value;
    if (parent->tail == old) parent->tail = value;
    old->next = NULL;
    free_tree(old);
    return DMJSON_OK;
}

static int validate_text(const char *text, size_t size);

static int commit_member(dmjson_t ctx, const char *path, node_t *value, size_t nodes, size_t height)
{
    const char *end = path + length(path), *key = end;
    node_t *parent, *old;
    size_t depth = 0, removed;
    while (key > path && key[-1] != '/') --key;
    if (key == path || !valid_segment(key, end)) return DMJSON_ERR_INVALID;
    parent = find_range(ctx, path, key - 1);
    if (!parent) return DMJSON_ERR_NOT_FOUND;
    if (!container(parent)) return DMJSON_ERR_TYPE;
    old = segment(parent, key, end);
    if (!old && parent->type == DMJSON_ARRAY && !(end - key == 1 && *key == '-')) return DMJSON_ERR_NOT_FOUND;
    for (node_t *n = parent; n; n = n->parent) ++depth;
    if (height > DMJSON_MAX_DEPTH - depth) return DMJSON_ERR_LIMIT;
    removed = tree_size(old);
    if (nodes > DMJSON_MAX_NODES - (ctx->nodes - removed)) return DMJSON_ERR_LIMIT;
    if (!old && parent->type == DMJSON_OBJECT) {
        value->key = copy_key(key, end, &value->key_len);
        if (!value->key) return DMJSON_ERR_MEMORY;
        if (validate_text(value->key, value->key_len)) return DMJSON_ERR_INVALID;
    }
    replace_member(parent, old, value);
    ctx->nodes = ctx->nodes - removed + nodes;
    return DMJSON_OK;
}

/* Takes ownership of value on both success and failure. */
static int commit(dmjson_t ctx, const char *path, node_t *value, size_t nodes, size_t height)
{
    int result;
    if (!valid(ctx) || !path) result = DMJSON_ERR_INVALID;
    else if (!*path) {
        free_tree(ctx->root);
        ctx->root = value;
        ctx->nodes = nodes;
        return DMJSON_OK;
    } else result = commit_member(ctx, path, value, nodes, height);
    if (result) free_tree(value);
    return result;
}

static int set_parsed(dmjson_t ctx, const char *path, const char *json, bool number_only)
{
    parser_t *p;
    int result;
    if (!valid(ctx) || !path || !json) return DMJSON_ERR_INVALID;
    p = parse(json, length(json));
    if (!p) return DMJSON_ERR_MEMORY;
    result = p->error;
    ctx->error = result ? (size_t)(p->p - p->begin) : SIZE_MAX;
    if (!result && number_only && p->root->type != DMJSON_NUMBER) {
        free_tree(p->root);
        result = DMJSON_ERR_TYPE;
    } else if (!result) result = commit(ctx, path, p->root, p->nodes, p->height);
    Dmod_Free(p);
    return result;
}

dmod_dmjson_api_declaration(1.0, int, _set_json, (dmjson_t ctx, const char *path, const char *json))
{
    return set_parsed(ctx, path, json, false);
}

dmod_dmjson_api_declaration(1.0, int, _set_number, (dmjson_t ctx, const char *path, const char *value))
{
    return set_parsed(ctx, path, value, true);
}

static int validate_text(const char *text, size_t size)
{
    const char *end = text + size;
    uint32_t scalar_value;
    while (text < end) {
        if ((unsigned char)*text < 0x80) ++text;
        else if (!read_utf8(&text, end, &scalar_value)) return DMJSON_ERR_INVALID;
    }
    return DMJSON_OK;
}

dmod_dmjson_api_declaration(1.0, int, _set_string, (dmjson_t ctx, const char *path, const char *value))
{
    node_t *n;
    int result;
    size_t size;
    if (!valid(ctx) || !path || !value) return DMJSON_ERR_INVALID;
    size = length(value);
    result = validate_text(value, size);
    if (result) return result;
    n = new_node(DMJSON_STRING);
    if (!n) return DMJSON_ERR_MEMORY;
    n->text_len = size;
    n->text = copy_bytes(value, size);
    if (!n->text) { free_tree(n); return DMJSON_ERR_MEMORY; }
    return commit(ctx, path, n, 1, 0);
}

dmod_dmjson_api_declaration(1.0, int, _set_int, (dmjson_t ctx, const char *path, int32_t value))
{
    char buffer[12];
    char *p = buffer + sizeof(buffer) - 1;
    uint32_t magnitude = value < 0 ? (uint32_t)(-(value + 1)) + 1 : (uint32_t)value;
    *p = 0;
    do { *--p = (char)('0' + magnitude % 10); magnitude /= 10; } while (magnitude);
    if (value < 0) *--p = '-';
    return dmjson_set_number(ctx, path, p);
}

static int set_simple(dmjson_t ctx, const char *path, dmjson_type_t type, bool value)
{
    node_t *n;
    if (!valid(ctx) || !path) return DMJSON_ERR_INVALID;
    n = new_node(type);
    if (!n) return DMJSON_ERR_MEMORY;
    n->state = value;
    return commit(ctx, path, n, 1, 0);
}

dmod_dmjson_api_declaration(1.0, int, _set_bool, (dmjson_t ctx, const char *path, bool value))
{
    return set_simple(ctx, path, DMJSON_BOOL, value);
}

dmod_dmjson_api_declaration(1.0, int, _set_null, (dmjson_t ctx, const char *path))
{
    return set_simple(ctx, path, DMJSON_NULL, false);
}

dmod_dmjson_api_declaration(1.0, int, _remove, (dmjson_t ctx, const char *path))
{
    node_t *n, *parent, *previous = NULL;
    if (!valid(ctx) || !path) return DMJSON_ERR_INVALID;
    n = find(ctx, path);
    if (!n) return DMJSON_ERR_NOT_FOUND;
    ctx->nodes -= tree_size(n);
    parent = n->parent;
    if (!parent) ctx->root = NULL;
    else {
        for (node_t *item = parent->child; item != n; item = item->next) previous = item;
        if (previous) previous->next = n->next;
        else parent->child = n->next;
        if (parent->tail == n) parent->tail = previous;
        --parent->count;
    }
    n->next = NULL;
    free_tree(n);
    return DMJSON_OK;
}

int dmod_init(const Dmod_Config_t *config) { (void)config; return 0; }
int dmod_deinit(void) { return 0; }
