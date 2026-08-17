#include "c11_lua_bind.h"
#include "tinytest.h"

#include <stdbool.h>
#include <string.h>

static int l_add(lua_State* L) {
    double a = luaL_checknumber(L, 1);
    double b = luaL_checknumber(L, 2);
    lua_pushnumber(L, a + b);
    return 1;
}

static int l_concat(lua_State* L) {
    const char* s1 = luaL_checkstring(L, 1);
    const char* s2 = luaL_checkstring(L, 2);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s", s1, s2);
    lua_pushstring(L, buf);
    return 1;
}

/* Test struct with const char* (borrowed, unsafe after stack changes) */
#define PLAYER_VIEW_FIELDS(X) \
    X(int, id) \
    X(double, hp) \
    X(const char*, name) \
    X(bool, is_active)

C11_LUA_DEFINE_STRUCT(PlayerView, PLAYER_VIEW_FIELDS)

/* Test struct with tstr_t (owned, safe after stack changes) */
#define PLAYER_OWNED_FIELDS(X) \
    X(int, id) \
    X(double, hp) \
    X(tstr_t, name) \
    X(tstr_t, title) \
    X(bool, is_active)

C11_LUA_DEFINE_STRUCT(PlayerOwned, PLAYER_OWNED_FIELDS)
C11_LUA_DEFINE_STRUCT_CLEANUP(PlayerOwned, name, title)

/* Test struct with tstr_v (borrowed view) */
#define PLAYER_VIEW_V_FIELDS(X) \
    X(int, id) \
    X(tstr_v, name) \
    X(bool, is_active)

C11_LUA_DEFINE_STRUCT(PlayerViewV, PLAYER_VIEW_V_FIELDS)

suite("c11 lua bind") {
    static lua_State* L;

    before_each() {
        L = luaL_newstate();
        luaL_openlibs(L);
    }

    after_each() {
        if (L) lua_close(L);
        L = NULL;
    }

    it("pushes and gets typed values") {
        c11_lua_push(L, 42);
        c11_lua_push(L, 3.14159);
        c11_lua_push(L, "Hello C11");
        c11_lua_push(L, (bool)true);

        bool b_val = false;
        const char* s_val = NULL;
        double d_val = 0.0;
        int i_val = 0;

        c11_lua_get(L, 4, &b_val);
        c11_lua_get(L, 3, &s_val);
        c11_lua_get(L, 2, &d_val);
        c11_lua_get(L, 1, &i_val);

        check_int_eq(i_val, 42);
        check_double_eq(d_val, 3.14159, 0.00001);
        check_str_eq(s_val, "Hello C11");
        check_true(b_val);
        lua_pop(L, 4);
    }

    it("strictly extracts values and preserves outputs on failure") {
        const lua_Integer beyond_int = (lua_Integer)INT_MAX + 1;
        int integer = 0;
        unsigned char byte = 0;
        double number = 0.0;
        bool boolean = false;

        lua_pushinteger(L, 42);
        check_int_eq(c11_lua_get_checked(L, -1, &integer), TURBO_OK);
        check_int_eq(integer, 42);
        lua_pop(L, 1);

        lua_pushnumber(L, 3.5);
        integer = 77;
        check_int_eq(c11_lua_get_checked(L, -1, &integer), TURBO_EPROTO);
        check_int_eq(integer, 77);
        check_int_eq(c11_lua_get_checked(L, -1, &number), TURBO_OK);
        check_double_eq(number, 3.5, 0.00001);
        lua_pop(L, 1);

        lua_pushinteger(L, beyond_int);
        integer = 88;
        check_int_eq(c11_lua_get_checked(L, -1, &integer), TURBO_ERANGE);
        check_int_eq(integer, 88);
        lua_pop(L, 1);

        lua_pushinteger(L, -1);
        byte = 9;
        check_int_eq(c11_lua_get_checked(L, -1, &byte), TURBO_ERANGE);
        check_int_eq(byte, 9);
        lua_pop(L, 1);

        lua_pushboolean(L, 1);
        check_int_eq(c11_lua_get_checked(L, -1, &boolean), TURBO_OK);
        check_true(boolean);
        lua_pop(L, 1);

        lua_pushstring(L, "12");
        integer = 99;
        check_int_eq(c11_lua_get_checked(L, -1, &integer), TURBO_EPROTO);
        check_int_eq(integer, 99);
        lua_pop(L, 1);
    }

    it("copies binary Lua strings into owned tstr_t values") {
        static const char payload[] = {'A', '\0', 'B'};
        tstr_t owned = NULL;

        lua_pushlstring(L, payload, sizeof(payload));
        check_int_eq(c11_lua_get_checked(L, -1, &owned), TURBO_OK);
        lua_pop(L, 1);

        check_not_null(owned);
        check_size_eq(tstr_len(owned), sizeof(payload));
        check_mem_eq(owned, payload, sizeof(payload));
        tstr_free(owned);
    }

    it("registers C functions into a Lua table") {
        C11_LUA_BIND_FUNCS(L, l_add, l_concat);
        lua_setglobal(L, "MyLib");

        const char* script =
            "res_add = MyLib.l_add(15, 27)\n"
            "res_str = MyLib.l_concat('C11 ', 'Lua')\n";
        check_int_eq(luaL_dostring(L, script), LUA_OK);

        lua_getglobal(L, "res_add");
        check_int_eq((int)lua_tointeger(L, -1), 42);
        lua_pop(L, 1);

        lua_getglobal(L, "res_str");
        check_str_eq(lua_tostring(L, -1), "C11 Lua");
        lua_pop(L, 1);
    }

    it("binds named and batched global C functions") {
        check_int_eq(C11_LUA_BIND_FUNCTION(L, l_add), TURBO_OK);
        check_int_eq(C11_LUA_BIND_FUNCTION_AS(L, "sum", l_add), TURBO_OK);
        C11_LUA_BIND_GLOBAL_FUNCS(L, l_concat);

        check_int_eq(luaL_dostring(
                         L,
                         "assert(l_add(2, 3) == 5);"
                         "assert(sum(4, 5) == 9);"
                         "assert(l_concat('C', '11') == 'C11');"),
                     LUA_OK);
    }

    it("runs scripts in inherited and isolated environments") {
        int base = lua_gettop(L);

        check_int_eq(c11_lua_create_environment(L, "missing_child", "missing_parent", true),
                     TURBO_ENOENT);
        check_int_eq(lua_gettop(L), base);

        check_int_eq(c11_lua_create_environment(L, "parent_env", NULL, true), TURBO_OK);
        check_int_eq(c11_lua_create_environment(L, "child_env", "parent_env", true), TURBO_OK);
        check_int_eq(c11_lua_run_script_in_environment(L, "parent_env", "shared = 8"),
                     LUA_OK);
        check_int_eq(c11_lua_run_script_in_environment(L, "child_env", "x = shared + 1"),
                     LUA_OK);

        lua_getglobal(L, "child_env");
        lua_getfield(L, -1, "x");
        check_int_eq(lua_tointeger(L, -1), 9);
        lua_pop(L, 2);

        lua_getglobal(L, "shared");
        check_true(lua_isnil(L, -1));
        lua_pop(L, 1);

        check_int_eq(c11_lua_create_isolated_environment(L, "isolated", NULL), TURBO_OK);
        check_int_ne(c11_lua_run_script_in_environment(L, "isolated", "print('blocked')"),
                     LUA_OK);
        check_true(lua_isstring(L, -1));
        lua_pop(L, 1);
        check_int_eq(lua_gettop(L), base);
    }

    it("retains and releases registry references") {
        c11_lua_ref_t ref = C11_LUA_REF_INIT;

        lua_newtable(L);
        lua_pushinteger(L, 17);
        lua_setfield(L, -2, "value");
        check_int_eq(c11_lua_ref_create(L, -1, &ref), TURBO_OK);
        lua_pop(L, 1);

        check_true(c11_lua_ref_is_valid(&ref));
        check_int_eq(c11_lua_ref_push(L, &ref), TURBO_OK);
        lua_getfield(L, -1, "value");
        check_int_eq(lua_tointeger(L, -1), 17);
        lua_pop(L, 2);

        check_int_eq(c11_lua_ref_release(&ref), TURBO_OK);
        check_false(c11_lua_ref_is_valid(&ref));
        check_int_eq(c11_lua_ref_push(L, &ref), TURBO_ENOENT);
    }

    it("calls global functions through a protected boundary") {
        check_int_eq(C11_LUA_BIND_FUNCTION_AS(L, "sum", l_add), TURBO_OK);

        lua_pushinteger(L, 20);
        lua_pushinteger(L, 22);
        check_int_eq(c11_lua_pcall_global(L, "sum", 2, 1), LUA_OK);
        check_int_eq(lua_tointeger(L, -1), 42);
        lua_pop(L, 1);

        lua_pushinteger(L, 1);
        check_int_eq(c11_lua_pcall_global(L, "missing", 1, 1), LUA_ERRRUN);
        check_true(lua_isstring(L, -1));
        check_str_contains(lua_tostring(L, -1), "not callable");
        lua_pop(L, 1);
    }

    it("serializes a struct to and from a Lua table") {
        PlayerView p1 = { .id = 1001, .hp = 98.5, .name = "Warrior", .is_active = true };
        PlayerView_to_lua(L, &p1);
        lua_setglobal(L, "player1");

        lua_getglobal(L, "player1");
        PlayerView p2 = {0};
        PlayerView_from_lua(L, -1, &p2);
        // ⚠️ p2.name is borrowed - must use BEFORE lua_pop

        check_int_eq(p2.id, 1001);
        check_double_eq(p2.hp, 98.5, 0.001);
        check_str_eq(p2.name, "Warrior");
        check_true(p2.is_active);

        lua_pop(L, 1);
        // ⚠️ p2.name is now DANGLING - do not access!
    }

    it("handles tstr_t owned strings safely") {
        // Create Lua table
        lua_newtable(L);
        lua_pushinteger(L, 2002);
        lua_setfield(L, -2, "id");
        lua_pushnumber(L, 100.0);
        lua_setfield(L, -2, "hp");
        lua_pushstring(L, "Mage");
        lua_setfield(L, -2, "name");
        lua_pushstring(L, "Archmage");
        lua_setfield(L, -2, "title");
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "is_active");

        // Extract to PlayerOwned (tstr_t fields)
        PlayerOwned p = {0};
        PlayerOwned_from_lua(L, -1, &p);
        lua_pop(L, 1);  // Pop table from stack

        // ✅ SAFE: p.name owns its memory, independent of Lua stack
        check_int_eq(p.id, 2002);
        check_double_eq(p.hp, 100.0, 0.001);
        check_str_eq(p.name, "Mage");
        check_str_eq(p.title, "Archmage");
        check_size_eq(tstr_len(p.name), sizeof("Mage") - 1);
        check_size_eq(tstr_len(p.title), sizeof("Archmage") - 1);
        check_true(p.is_active);

        // Must cleanup tstr_t fields
        PlayerOwned_cleanup(&p);
    }

    it("handles tstr_v borrowed views") {
        lua_newtable(L);
        lua_pushinteger(L, 3003);
        lua_setfield(L, -2, "id");
        lua_pushstring(L, "Ranger");
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, 0);
        lua_setfield(L, -2, "is_active");

        PlayerViewV p = {0};
        PlayerViewV_from_lua(L, -1, &p);

        // ✅ Safe to use while table is on stack
        check_int_eq(p.id, 3003);
        check_int_eq(p.name.len, 6);
        check_true(memcmp(p.name.data, "Ranger", 6) == 0);
        check_false(p.is_active);

        lua_pop(L, 1);
        // ⚠️ p.name.data is now DANGLING
    }

    it("supports arena allocation for automatic cleanup") {
        MemoryPool *arena = pool_create(4096);
        check_true(arena != NULL);
        if (!arena) return;

        lua_newtable(L);
        lua_pushinteger(L, 4004);
        lua_setfield(L, -2, "id");
        lua_pushstring(L, "Paladin");
        lua_setfield(L, -2, "name");
        lua_pushboolean(L, 1);
        lua_setfield(L, -2, "is_active");

        PlayerViewV p = {0};
        PlayerViewV_from_lua_arena(L, -1, &p, arena);
        lua_pop(L, 1);

        /* The copied view remains valid until the arena is destroyed. */
        check_int_eq(p.id, 4004);
        check_size_eq(p.name.len, sizeof("Paladin") - 1);
        check_mem_eq(p.name.data, "Paladin", p.name.len);
        check_size_ge(pool_get_used(arena), p.name.len + 1);
        check_true(p.is_active);

        pool_destroy(arena);
    }

    it("pushes tstr_t and tstr_v to Lua") {
        tstr_t owned = tstr_dup("Owned String");
        tstr_v view = tstr_v_from_cstr("View String");

        c11_lua_push(L, owned);
        c11_lua_push(L, view);
        c11_lua_push(L, 42);

        check_str_eq(lua_tostring(L, 1), "Owned String");
        check_str_eq(lua_tostring(L, 2), "View String");
        check_int_eq(lua_tointeger(L, 3), 42);

        lua_pop(L, 3);
        tstr_free(owned);
    }

    it("gets tstr_t and tstr_v from Lua") {
        lua_pushstring(L, "Test String");

        tstr_t owned = NULL;
        c11_lua_get(L, -1, &owned);
        check_str_eq(owned, "Test String");

        tstr_v view;
        c11_lua_get(L, -1, &view);
        check_int_eq(view.len, 11);
        check_true(memcmp(view.data, "Test String", 11) == 0);

        lua_pop(L, 1);

        // ✅ owned still valid after pop
        check_str_eq(owned, "Test String");
        tstr_free(owned);

        // ⚠️ view.data is now DANGLING - do not access
    }
}
