#define DDEBUG 0
#include "ddebug.h"

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>


enum {
    BAD_REPLY           = 0,
    STATUS_REPLY        = 1,
    ERROR_REPLY         = 2,
    INTEGER_REPLY       = 3,
    BULK_REPLY          = 4,
    MULTI_BULK_REPLY    = 5
};

enum {
    PARSE_OK    = 0,
    PARSE_ERROR = 1
};


#define UINT64_LEN   (sizeof("18446744073709551615") - 1)

static char *redis_null = "null";

static int redis_parse_reply(lua_State *L);
static int redis_build_query(lua_State *L);
static const char * parse_single_line_reply(const char *src, const char *last,
        size_t *dst_len);
static const char * parse_bulk_reply(const char *src, const char *last,
        size_t *dst_len);
static int parse_multi_bulk_reply(lua_State *L, const char *src,
        const char *last);
static size_t get_num_size(size_t i);
static char *sprintf_num(char *dst, int64_t ui64);


static const struct luaL_Reg redis_parser[] = {
    {"parse_reply", redis_parse_reply},
    {"build_query", redis_build_query},
    {NULL, NULL}
};


int
luaopen_redis_parser(lua_State *L)
{
    luaL_register(L, "redis.parser", redis_parser);

    lua_pushlightuserdata(L, redis_null);
    lua_setfield(L, -2, "null");

    lua_pushnumber(L, BAD_REPLY);
    lua_setfield(L, -2, "BAD_REPLY");

    lua_pushnumber(L, STATUS_REPLY);
    lua_setfield(L, -2, "STATUS_REPLY");

    lua_pushnumber(L, ERROR_REPLY);
    lua_setfield(L, -2, "ERROR_REPLY");

    lua_pushnumber(L, INTEGER_REPLY);
    lua_setfield(L, -2, "INTEGER_REPLY");

    lua_pushnumber(L, BULK_REPLY);
    lua_setfield(L, -2, "BULK_REPLY");

    lua_pushnumber(L, MULTI_BULK_REPLY);
    lua_setfield(L, -2, "MULTI_BULK_REPLY");

    return 1;
}


static int
redis_parse_reply(lua_State *L)
{
    const char      *p, *last;
    size_t           len;
    const char      *dst;
    size_t           dst_len;
    lua_Number       num;
    int              rc;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expected one argument but got %d",
                lua_gettop(L));
    }

    p = luaL_checklstring(L, 1, &len);

    if (len == 0) {
        lua_pushliteral(L, "empty reply");
        lua_pushnumber(L, BAD_REPLY);
        return 2;
    }

    last = p + len;

    switch (*p) {
    case '+':
        p++;
        dst = parse_single_line_reply(p, last, &dst_len);

        if (dst_len == -2) {
            lua_pushliteral(L, "bad status reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
        lua_pushnumber(L, STATUS_REPLY);
        break;

    case '-':
        p++;
        dst = parse_single_line_reply(p, last, &dst_len);

        if (dst_len == -2) {
            lua_pushliteral(L, "bad error reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
        lua_pushnumber(L, ERROR_REPLY);
        break;

    case ':':
        p++;
        dst = parse_single_line_reply(p, last, &dst_len);

        if (dst_len == -2) {
            lua_pushliteral(L, "bad integer reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
        num = lua_tonumber(L, -1);
        lua_pushnumber(L, num);
        lua_pushnumber(L, INTEGER_REPLY);
        break;

    case '$':
        p++;
        dst = parse_bulk_reply(p, last, &dst_len);

        if (dst_len == -2) {
            lua_pushliteral(L, "bad bulk reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        if (dst_len == -1) {
            lua_pushnil(L);
            lua_pushnumber(L, BULK_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
        lua_pushnumber(L, BULK_REPLY);
        break;

    case '*':
        p++;
        rc = parse_multi_bulk_reply(L, p, last);

        if (rc != PARSE_OK) {
            lua_pushliteral(L, "bad multi bulk reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        /* rc == PARSE_OK */

        lua_pushnumber(L, MULTI_BULK_REPLY);
        break;

    default:
        lua_pushliteral(L, "empty reply");
        lua_pushnumber(L, BAD_REPLY);
        break;
    }

    return 2;
}


static const char *
parse_single_line_reply(const char *src, const char *last, size_t *dst_len)
{
    const char  *p = src;
    int          seen_cr = 0;

    while (p != last) {

        if (*p == '\r') {
            seen_cr = 1;

        } else if (seen_cr) {
            if (*p == '\n') {
                *dst_len = p - src - 1;
                return src;
            }

            seen_cr = 0;
        }

        p++;
    }

    /* CRLF not found at all */
    *dst_len = -2;
    return NULL;
}


#define CHECK_EOF if (p >= last) goto invalid;


static const char *
parse_bulk_reply(const char *src, const char *last, size_t *dst_len)
{
    const char *p = src;
    ssize_t     size = 0;
    const char *dst;

    CHECK_EOF

    /* read the bulk size */

    if (*p == '-') {
        p++;
        CHECK_EOF

        while (*p != '\r') {
            if (*p < '0' || *p > '9') {
                goto invalid;
            }

            p++;
            CHECK_EOF
        }

        /* *p == '\r' */

        if (last - p < size + sizeof("\r\n") - 1) {
            goto invalid;
        }

        p++;

        if (*p++ != '\n') {
            goto invalid;
        }

        *dst_len = -1;
        return p - (sizeof("\r\n") - 1);
    }

    while (*p != '\r') {
        if (*p < '0' || *p > '9') {
            goto invalid;
        }

        size *= 10;
        size += *p - '0';

        p++;
        CHECK_EOF
    }

    /* *p == '\r' */

    p++;
    CHECK_EOF

    if (*p++ != '\n') {
        goto invalid;
    }

    /* read the bulk data */

    if (last - p < size + sizeof("\r\n") - 1) {
        goto invalid;
    }

    dst = p;

    p += size;

    if (*p++ != '\r') {
        goto invalid;
    }

    if (*p++ != '\n') {
        goto invalid;
    }

    *dst_len = size;
    return dst;

invalid:
    *dst_len = -2;
    return NULL;
}


static int
parse_multi_bulk_reply(lua_State *L, const char *src, const char *last)
{
    const char      *p = src;
    int              count = 0;
    int              i;
    size_t           dst_len;
    const char      *dst;

    dd("enter multi bulk parser");

    CHECK_EOF

    while (*p != '\r') {
        if (*p < '0' || *p > '9') {
            dd("expecting digit, but found %c", *p);
            goto invalid;
        }

        count *= 10;
        count += *p - '0';

        p++;
        CHECK_EOF
    }

    dd("count = %d", count);

    /* *p == '\r' */

    p++;
    CHECK_EOF

    if (*p++ != '\n') {
        goto invalid;
    }

    dd("reading the individual bulks");

    lua_createtable(L, count, 0);

    for (i = 1; i <= count; i++) {
        CHECK_EOF

        if (*p++ != '$') {
            goto invalid;
        }

        dst = parse_bulk_reply(p, last, &dst_len);

        if (dst_len == -2) {
            dd("bulk %d reply parse fail for multi bulks", i);
            return PARSE_ERROR;
        }

        if (dst_len == -1) {
            lua_pushnil(L);
            p = dst + sizeof("\r\n") - 1;

        } else {
            lua_pushlstring(L, dst, dst_len);
            p = dst + dst_len + sizeof("\r\n") - 1;
        }

        lua_rawseti(L, -2, i);
    }

    return PARSE_OK;

invalid:
    return PARSE_ERROR;
}


static int
redis_build_query(lua_State *L)
{
    int          i, n;
    size_t       len, total;
    const char  *p;
    char        *last;
    char        *buf;
    int          flag;

    if (lua_gettop(L) != 1) {
        return luaL_error(L, "expected one argument but got %d",
                lua_gettop(L));
    }

    luaL_checktype(L, 1, LUA_TTABLE);

    n = luaL_getn(L, 1);

    if (n == 0) {
        return luaL_error(L, "empty input param table");
    }

    total = sizeof("*") - 1
          + get_num_size(n)
          + sizeof("\r\n") - 1
          ;

    for (i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);

        dd("param type: %d (%d)", lua_type(L, -1), LUA_TUSERDATA);

        switch (lua_type(L, -1)) {
            case LUA_TSTRING:
            case LUA_TNUMBER:
                lua_tolstring(L, -1, &len);

                total += sizeof("$") - 1
                       + get_num_size(len)
                       + sizeof("\r\n") - 1
                       + len
                       + sizeof("\r\n") - 1
                       ;

                break;

            case LUA_TBOOLEAN:
                total += sizeof("$1\r\n1\r\n") - 1;
                break;

            case LUA_TLIGHTUSERDATA:
                p = lua_touserdata(L, -1);
                dd("user data: %p", p);
                if (p == redis_null) {
                    total += sizeof("$-1\r\n") - 1;
                    break;
                }

            default:
                return luaL_error(L, "parameter %d is not a string, number, "
                        "redis.parser.null, or boolean value", i);
        }
    }

    buf = malloc(total);
    if (buf == NULL) {
        return luaL_error(L, "out of memory");
    }

    last = buf;

    lua_pushlstring(L, buf, total);

    *last++ = '*';
    last = sprintf_num(last, n);
    *last++ = '\r'; *last++ = '\n';

    for (i = 1; i <= n; i++) {
        lua_rawgeti(L, 1, i);

        switch (lua_type(L, -1)) {
            case LUA_TSTRING:
            case LUA_TNUMBER:
                p = luaL_checklstring(L, -1, &len);

                *last++ = '$';

                last = sprintf_num(last, len);

                *last++ = '\r'; *last++ = '\n';

                memcpy(last, p, len);
                last += len;

                *last++ = '\r'; *last++ = '\n';

                break;

            case LUA_TBOOLEAN:
                memcpy(last, "$1\r\n", sizeof("$1\r\n") - 1);
                last += sizeof("$1\r\n") - 1;

                flag = lua_toboolean(L, -1);
                *last++ = flag ? '1' : '0';

                *last++ = '\r'; *last++ = '\n';

                break;

            case LUA_TLIGHTUSERDATA:
                /* must be null */
                memcpy(last, "$-1\r\n", sizeof("$-1\r\n") - 1);
                last += sizeof("$-1\r\n") - 1;
                break;

            default:
                /* cannot reach here */
                break;
        }
    }

    if (last - buf != (ssize_t) total) {
        return luaL_error(L, "buffer error");
    }

    lua_pushlstring(L, buf, total);

    free(buf);

    return 1;
}


static size_t
get_num_size(size_t i)
{
    size_t          n = 0;

    do {
        i = i / 10;
        n++;
    } while (i > 0);

    return n;
}


static char *
sprintf_num(char *dst, int64_t ui64)
{
    char             *p;
    char              temp[UINT64_LEN + 1];
    size_t            len;

    p = temp + UINT64_LEN;

    do {
        *--p = (char) (ui64 % 10 + '0');
    } while (ui64 /= 10);

    len = (temp + UINT64_LEN) - p;

    memcpy(dst, p, len);

    return dst + len;
}

