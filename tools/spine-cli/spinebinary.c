//
// $id: spinebinary.c 2014-09-05 zhongfengqu $
//

#include "spinebinary.h"

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    FILE* file;
    int skip;
} spinebinary;

#define SPINEBINARY "spinebinary"
#define PUT(ch) fputc((ch), self->file)

static void write_int(spinebinary* self, int value);

static int _new(lua_State* L)
{
    spinebinary* self = (spinebinary*)malloc(sizeof(spinebinary));
    self->file = fopen(luaL_checkstring(L, 1), "wb");
    self->skip = 0;
    if (!self->file)
    {
        lua_pushfstring(L, "can't open file: %s", luaL_checkstring(L, 1));
        lua_error(L);
    }
    *(spinebinary**)lua_newuserdata(L, sizeof(spinebinary*)) = self;
    lua_getglobal(L, SPINEBINARY);
    lua_setmetatable(L, -2);
    write_int(self, 0);
    return 1;
}

static int _gc(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    fclose(self->file);
    free(self);
    return 1;
}

static int _write_bool(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    int value = lua_toboolean(L, 2);
    PUT(value != 0);
    return 0;
}

static int _write_char(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    int value = luaL_checkint(L, 2);
    PUT(value & 0xFF);
    return 0;
}

static void write_short(spinebinary* self, int value)
{
    PUT(value >> 8 & 0xFF);
    PUT(value & 0xFF);
}

static int _write_short(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    int value = luaL_checkint(L, 2);
    write_short(self, value);
    return 0;
}

static void write_int(spinebinary* self, int value)
{
    PUT(value >> 24 & 0xFF);
    PUT(value >> 16 & 0xFF);
    PUT(value >> 8  & 0xFF);
    PUT(value & 0xFF);
}

static int _write_int(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    int value = luaL_checkint(L, 2);
    write_int(self, value);
    return 0;
}

static int _write_float(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    
    union {
        float f;
        int i;
    } u;
    
    u.f = (float)luaL_checknumber(L, 2);
    write_int(self, u.i);

    return 0;
}

static int _write_string(lua_State* L)
{
    spinebinary* self = *(spinebinary**)lua_touserdata(L, 1);
    
    if (self->skip == 0)
    {
        self->skip = 1;
        fseek(self->file, 0, SEEK_END);
        size_t len = ftell(self->file);
        fseek(self->file, 0, SEEK_SET);
        write_int(self, (int)len - 4);
        fseek(self->file, 0, SEEK_END);
    }
    
    size_t len;
    const char* str = luaL_checklstring(L, 2, &len);
    write_short(self, (int)len);
    fwrite(str, sizeof(char), len + 1, self->file);
    return 0;
}

static const luaL_Reg spinelib[] = {
    {"new", _new},
    {"__gc", _gc},
    {"bool", _write_bool},
    {"char", _write_char},
    {"short", _write_short},
    {"int", _write_int},
    {"float", _write_float},
    {"string", _write_string},
    {NULL, NULL},
};

LUALIB_API int luaopen_spinebinary(lua_State* L)
{
    luaL_register(L, SPINEBINARY, spinelib);
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");
    
    return 1;
}