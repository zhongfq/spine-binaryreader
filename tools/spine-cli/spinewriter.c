//
// $id: spinewriter.c zhongfengqu $
//

#include "spinewriter.h"

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
    FILE* file;
} spinewriter;

#define SPINEWRITER "spinewriter"
#define GETWRITER() (*(spinewriter **)lua_touserdata(L, 1))
#define PUT(ch) fputc((ch), self->file)

static void write_varint(spinewriter *self, int value, bool optimizePositive);

static int _new(lua_State *L)
{
    spinewriter *self = (spinewriter *)malloc(sizeof(spinewriter));
    self->file = fopen(luaL_checkstring(L, 1), "wb");
    if (!self->file) {
        lua_pushfstring(L, "can't open file: %s", luaL_checkstring(L, 1));
        lua_error(L);
    }
    
    *(spinewriter **)lua_newuserdata(L, sizeof(spinewriter *)) = self;
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_setmetatable(L, -2);
    
    return 1;
}

static int _gc(lua_State *L)
{
    spinewriter *self = GETWRITER();
    fclose(self->file);
    free(self);
    return 1;
}

static int _write_bool(lua_State *L)
{
    spinewriter *self = GETWRITER();
    int value = lua_toboolean(L, 2);
    PUT(value != 0);
    return 0;
}

static int _write_byte(lua_State *L)
{
    spinewriter *self = GETWRITER();
    int value = (int)luaL_checkinteger(L, 2);
    PUT(value & 0xFF);
    return 0;
}

static void write_short(spinewriter *self, int value)
{
    PUT(value >> 8 & 0xFF);
    PUT(value & 0xFF);
}

static int _write_short(lua_State *L)
{
    spinewriter *self = GETWRITER();
    int value = (int)luaL_checkinteger(L, 2);
    write_short(self, value);
    return 0;
}

static void write_varint(spinewriter *self, int value, bool optimizePositive)
{
    if (!optimizePositive) {
        // zigZag encode
        value = (value << 1) ^ (value >> 31);
    }
    
    unsigned int uvalue = (unsigned int)value;
    
    while (true) {
        if ((uvalue & ~0x7F) == 0) {
            PUT(uvalue);
            return;
        } else {
            PUT((uvalue & 0x7F) | 0x80);
            uvalue >>= 7;
        }
    }
}

static int _write_varint(lua_State *L)
{
    lua_settop(L, 3);
    spinewriter *self = GETWRITER();
    int value = (int)luaL_checkinteger(L, 2);
    write_varint(self, value, lua_toboolean(L, 3));
    
    return 0;
}

static int _write_int(lua_State *L)
{
    spinewriter *self = GETWRITER();
    int value = (int)luaL_checkinteger(L, 2);
    
    PUT(value >> 24 & 0xFF);
    PUT(value >> 16 & 0xFF);
    PUT(value >> 8  & 0xFF);
    PUT(value & 0xFF);
    
    return 0;
}

static int _write_float(lua_State *L)
{
    union {
        float f;
        int i;
    } u;
    
    u.f = (float)luaL_checknumber(L, 2);
    
    lua_settop(L, 1);
    lua_pushinteger(L, u.i);

    return _write_int(L);
}

static int _write_string(lua_State *L)
{
    spinewriter *self = GETWRITER();
    const char *str = lua_tostring(L, -1);
    
    if (str == NULL) {
        write_varint(self, 0, true);
    } else if (strlen(str) == 0) {
        write_varint(self, 1, true);
    } else {
        int len = (int)strlen(str);
        write_varint(self, len + 1, true);
        fwrite(str, sizeof(char), len, self->file);
    }
    
    return 0;
}

static const luaL_Reg spinelib[] = {
    {"new", _new},
    {"__gc", _gc},
    {"bool", _write_bool},
    {"byte", _write_byte},
    {"short", _write_short},
    {"int", _write_int},
    {"varint", _write_varint},
    {"float", _write_float},
    {"string", _write_string},
    {NULL, NULL},
};

LUALIB_API int luaopen_spinewriter(lua_State *L)
{
    luaL_newlibtable(L, spinelib);
    
    lua_pushvalue(L, -1);
    luaL_setfuncs(L, spinelib, 1);
    
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    
    lua_pushvalue(L, -1);
    lua_setglobal(L, SPINEWRITER);
    
    return 1;
}