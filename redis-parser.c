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


static int redis_parse_reply(lua_State *L);
static const char * parse_single_line_reply(const char *src, const char *last,
        size_t *dst_len);
static const char * parse_bulk_reply(const char *src, const char *last,
        size_t *dst_len);
static int parse_multi_bulk_reply(lua_State *L, const char *src,
        const char *last);


static const struct luaL_Reg redis_parser[] = {
    {"parse_reply", redis_parse_reply},
    {NULL, NULL}
};


int
luaopen_redis_parser(lua_State *L)
{
    luaL_register(L, "redis.parser", redis_parser);

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

