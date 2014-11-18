CFLAGS = -g -Wall
SRC := \
spine.c \
spinebinary.c \
cjson/cJSON.c \
cjson/dtoa.c \
cjson/fpconv.c \
cjson/g_fmt.c \
cjson/lua_cjson.c \
cjson/strbuf.c \
lua/lapi.c \
lua/lauxlib.c \
lua/lbaselib.c \
lua/lcode.c \
lua/ldblib.c \
lua/ldebug.c \
lua/ldo.c \
lua/ldump.c \
lua/lfunc.c \
lua/lgc.c \
lua/linit.c \
lua/liolib.c \
lua/llex.c \
lua/lmathlib.c \
lua/lmem.c \
lua/loadlib.c \
lua/lobject.c \
lua/lopcodes.c \
lua/loslib.c \
lua/lparser.c \
lua/lstate.c \
lua/lstring.c \
lua/lstrlib.c \
lua/ltable.c \
lua/ltablib.c \
lua/ltm.c \
lua/lundump.c \
lua/lvm.c \
lua/lzio.c \
lua/print.c 

spine:
	gcc $(CFLAGS) -o spine $(SRC)

clean:
	find . -name "spine" -o -name "spine.exe" | xargs rm -v
	rm -rvf spine.dSYM