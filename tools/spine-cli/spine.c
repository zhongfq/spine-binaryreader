//
// $id: spine.c 2014-09-05 zhongfengqu $
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "cjson/lua_cjson.h"

#include "spinebinary.h"

static int _traceback(lua_State* L)
{
    const char* errmsg = lua_tostring(L, -1);
    lua_getglobal(L, "debug");
    lua_getfield(L, -1, "traceback");
    lua_call(L, 0, 1);
    printf("%s \n%s\n", errmsg, lua_tostring(L, -1));
    return 1;
}

int main(int argc, const char * argv[])
{
    if (argc < 2)
    {
        printf("no input path\n");
        exit(1);
    }

    const char* apppath = argv[0];
    char* path = strrchr(argv[0], '/');
    char buff[1024] = {0};
    strncat(buff, apppath, path ? (path - apppath + 1) : 0);
    strcat(buff, "converter.lua");
    
    lua_State* L = lua_open();
    luaL_openlibs(L);
    luaopen_cjson(L);
    luaopen_spinebinary(L);
    luaL_dofile(L, buff);
    
    FILE* file = fopen(argv[1], "r");
    
    if (!file)
    {
        printf("can not open file: %s\n", argv[1]);
        exit(1);
    }
    
    fseek(file, 0, SEEK_END);
    size_t len = ftell(file);
    char* data = (char*)malloc(len);
    fseek(file, 0, SEEK_SET);
    fread(data, 1, len, file);
    fclose(file);
    
    lua_pushcfunction(L, _traceback);
    lua_getglobal(L, "convert");
    luaL_checktype(L, -1, LUA_TFUNCTION);
    lua_pushlstring(L, data, len);
    luaL_gsub(L, argv[1], ".json", ".skel");
    lua_pcall(L, 2, 0, -4);
    
    lua_close(L);

    free(data);
    
    return 0;
}

