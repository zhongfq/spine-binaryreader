//
// $id: spinec.c zhongfengqu $
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "cjson/lua_cjson.h"

#include "spinewriter.h"

static int _traceback(lua_State *L)
{
    const char *errmsg = lua_tostring(L, -1);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_call(L, 0, 1);
    printf("%s \n%s\n", errmsg, lua_tostring(L, -1));
    return 1;
}

static bool isop(const char *op, const char *arg)
{
    return strncmp(op, arg, 2) == 0;
}

static const char *get_arg(int argc, const char *argv[], int *index)
{
    int i = *index + 1;
    if (i < argc && argv[i][0] != '-') {
        *index = i;
        return argv[i];
    } else {
        return NULL;
    }
}

int main(int argc, const char *argv[])
{
    const char *jsonfile = NULL;
    const char *skelfile = NULL;
    
    int errfunc;
    const char* cmdpath;
    FILE* inputfile;
    
    bool option_makeup = false;
    bool option_trim = false;
    bool option_nonessential = false;
    
    for (int i = 1; i < argc; i++) {
        const char *op = argv[i];
        if (isop("-o", op)) {
            skelfile = get_arg(argc, argv, &i);
            jsonfile = get_arg(argc, argv, &i);
        }
        if (isop("-m", op)) {
            option_makeup = true;
        }
        if (isop("-x", op)) {
            option_trim = true;
        }
        if (isop("-e", op)) {
            option_nonessential = true;
        }
    }
    
    if (skelfile == NULL) {
        printf("no output path\n");
        exit(1);
    }
    
    if (jsonfile == NULL) {
        printf("no input path\n");
        exit(1);
    }
    
    inputfile = fopen(jsonfile, "r");
    if (inputfile == NULL) {
        printf("can not open file: %s\n", jsonfile);
        exit(1);
    }
    fclose(inputfile);
    
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);
    luaopen_cjson(L);
    luaopen_spinewriter(L);
    
    lua_pushcfunction(L, _traceback);
    errfunc = lua_gettop(L);
    
    cmdpath = (const char *)strrchr(argv[0], '/');
    if (cmdpath == NULL) {
        lua_pushstring(L, "./converter.lua");
    } else {
        luaL_gsub(L, argv[0], cmdpath + 1, "converter.lua");
    }
    
    cmdpath = lua_tostring(L, -1);
    
    if (luaL_loadfile(L, cmdpath) != LUA_OK) {
        printf("%s\n", lua_tostring(L, -1));
        exit(1);
    }
    
    if (lua_pcall(L, 0, 0, errfunc) != LUA_OK) {
        exit(1);
    }
    
    lua_getglobal(L, "main");
    lua_pushstring(L, jsonfile);
    lua_pushstring(L, skelfile);
    
    lua_createtable(L, 0, 2);
    
    lua_pushboolean(L, option_makeup);
    lua_setfield(L, -2, "makeup");
    
    lua_pushboolean(L, option_trim);
    lua_setfield(L, -2, "trim");
    
    lua_pushboolean(L, option_nonessential);
    lua_setfield(L, -2, "nonessential");
    
    lua_pcall(L, 3, 0, errfunc);
    
    lua_close(L);
    
    return 0;
}
