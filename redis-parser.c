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


static int redis_parse_reply(lua_State *L);
static const char * parse_single_line_reply(const char *src, const char *last,
        size_t *dst_len);
static const char * parse_bulk_reply(const char *src, const char *last,
        size_t *dst_len);
static const char * parse_multi_bulk_reply(const char *src, const char *last,
        size_t *dst_len);


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

        if (dst == NULL) {
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

        if (dst == NULL) {
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

        if (dst == NULL) {
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

        if (dst == NULL) {
            lua_pushliteral(L, "bad bulk reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
        lua_pushnumber(L, BULK_REPLY);
        break;

    case '*':
        p++;
        dst = parse_multi_bulk_reply(p, last, &dst_len);

        if (dst == NULL) {
            lua_pushliteral(L, "bad multi bulk reply");
            lua_pushnumber(L, BAD_REPLY);
            return 2;
        }

        lua_pushlstring(L, dst, dst_len);
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
    *dst_len = 0;
    return NULL;
}


static const char *
parse_bulk_reply(const char *src, const char *last, size_t *dst_len)
{
    /* TODO */
    return NULL;
}


static const char *
parse_multi_bulk_reply(const char *src, const char *last,
        size_t *dst_len)
{
    /* TODO */
    return NULL;
}

