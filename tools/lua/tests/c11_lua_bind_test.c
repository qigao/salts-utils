/* C11 public binding API tests. */
#include "salts_lua.h"
#include "tinytest.h"

#include <stdbool.h>
#include <string.h>

static int typed_answer(void) { return 42; }
static int typed_add(int left, int right) { return left + right; }
static double typed_blend(double first, double second, double weight) {
    return first + (second - first) * weight;
}
static int typed_notification_count;
static int typed_captured_sum;
static void typed_notify(const char* message) {
    if (message != NULL && strcmp(message, "ready") == 0)
        ++typed_notification_count;
}
static int typed_sum9(int a1, int a2, int a3, int a4, int a5,
                      int a6, int a7, int a8, int a9) {
    return a1 + a2 + a3 + a4 + a5 + a6 + a7 + a8 + a9;
}
static void typed_capture9(int a1, int a2, int a3, int a4, int a5,
                           int a6, int a7, int a8, int a9) {
    typed_captured_sum = typed_sum9(a1, a2, a3, a4, a5, a6, a7, a8, a9);
}

static int typed_fetch_continue(lua_State* L, int status,
                                lua_KContext context) {
    (void)status;
    lua_pushinteger(L, (lua_Integer)context + 100);
    return 1;
}

static int typed_fetch(lua_State* L) {
    int request_id;
    int rc = c11_lua_get_checked(L, 1, &request_id);
    if (rc != SALTS_OK)
        return c11_lua_typed_argument_error(L, 1, rc);
    lua_pushliteral(L, "pending");
    return lua_yieldk(L, 1, (lua_KContext)request_id,
                      typed_fetch_continue);
}

C11_LUA_FUNCTION(typed_answer, int)
C11_LUA_FUNCTION(typed_add, int, int, left, int, right)
C11_LUA_FUNCTION(typed_blend, double,
                 double, first, double, second, double, weight)
C11_LUA_VOID_FUNCTION(typed_notify, const char*, message)
C11_LUA_FUNCTION(typed_sum9, int,
                 int, a1, int, a2, int, a3, int, a4, int, a5,
                 int, a6, int, a7, int, a8, int, a9)
C11_LUA_VOID_FUNCTION(typed_capture9,
                      int, a1, int, a2, int, a3, int, a4, int, a5,
                      int, a6, int, a7, int, a8, int, a9)
C11_LUA_COROUTINE(typed_fetch)

typedef struct cleanup_record {
    tstr first;
    tstr second;
    tstr third;
    tstr fourth;
    tstr fifth;
    tstr sixth;
} cleanup_record;

C11_LUA_DEFINE_STRUCT_CLEANUP(cleanup_record,
                              first, second, third, fourth, fifth, sixth)

suite("c11 lua bind") {
    static lua_State* L;

    before_each() {
        L = luaL_newstate();
        luaL_openlibs(L);
        typed_notification_count = 0;
        typed_captured_sum = 0;
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

        check_equal(i_val, 42);
        check_within(d_val, 3.14159, 0.00001);
        check_equal(s_val, "Hello C11");
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
        check_equal(c11_lua_get_checked(L, -1, &integer), SALTS_OK);
        check_equal(integer, 42);
        lua_pop(L, 1);

        lua_pushnumber(L, 3.5);
        integer = 77;
        check_equal(c11_lua_get_checked(L, -1, &integer), SALTS_EPROTO);
        check_equal(integer, 77);
        check_equal(c11_lua_get_checked(L, -1, &number), SALTS_OK);
        check_within(number, 3.5, 0.00001);
        lua_pop(L, 1);

        lua_pushinteger(L, beyond_int);
        integer = 88;
        check_equal(c11_lua_get_checked(L, -1, &integer), SALTS_ERANGE);
        check_equal(integer, 88);
        lua_pop(L, 1);

        lua_pushinteger(L, -1);
        byte = 9;
        check_equal(c11_lua_get_checked(L, -1, &byte), SALTS_ERANGE);
        check_equal(byte, 9);
        lua_pop(L, 1);

        lua_pushboolean(L, 1);
        check_equal(c11_lua_get_checked(L, -1, &boolean), SALTS_OK);
        check_true(boolean);
        lua_pop(L, 1);

        lua_pushstring(L, "12");
        integer = 99;
        check_equal(c11_lua_get_checked(L, -1, &integer), SALTS_EPROTO);
        check_equal(integer, 99);
        lua_pop(L, 1);
    }

    it("copies binary Lua strings into owned tstr values") {
        static const char payload[] = {'A', '\0', 'B'};
        tstr owned = NULL;

        lua_pushlstring(L, payload, sizeof(payload));
        check_equal(c11_lua_get_checked(L, -1, &owned), SALTS_OK);
        lua_pop(L, 1);

        check_not_null(owned);
        check_equal(tstr_len(owned), sizeof(payload));
        check_equal(owned, payload, sizeof(payload));
        tstr_free(owned);
    }

    it("cleans every owned string field in a wide record") {
        cleanup_record value = {
            tstr_dup("first"),
            tstr_dup("second"),
            tstr_dup("third"),
            tstr_dup("fourth"),
            tstr_dup("fifth"),
            tstr_dup("sixth")
        };

        cleanup_record_cleanup(&value);

        check_null(value.first);
        check_null(value.second);
        check_null(value.third);
        check_null(value.fourth);
        check_null(value.fifth);
        check_null(value.sixth);
    }

    it("adapts ordinary typed C functions to Lua") {
        check_equal(C11_LUA_BIND_AS(L, "answer", typed_answer), SALTS_OK);
        check_equal(C11_LUA_BIND_AS(L, "add", typed_add), SALTS_OK);
        check_equal(C11_LUA_BIND_AS(L, "blend", typed_blend), SALTS_OK);
        check_equal(C11_LUA_BIND_AS(L, "notify", typed_notify), SALTS_OK);

        check_equal(luaL_dostring(
                         L,
                         "assert(answer() == 42);"
                         "assert(add(20, 22) == 42);"
                         "assert(blend(10.0, 20.0, 0.25) == 12.5);"
                         "notify('ready');"),
                     LUA_OK);
        check_equal(typed_notification_count, 1);
    }

    it("adapts functions with nine parameters") {
        check_equal(C11_LUA_BIND(L, typed_sum9), SALTS_OK);
        check_equal(C11_LUA_BIND(L, typed_capture9), SALTS_OK);

        check_equal(luaL_dostring(
                         L,
                         "assert(typed_sum9(1,2,3,4,5,6,7,8,9) == 45);"
                         "typed_capture9(1,2,3,4,5,6,7,8,9)"),
                     LUA_OK);
        check_equal(typed_captured_sum, 45);
    }

    it("rejects typed function arity and argument mismatches") {
        check_equal(C11_LUA_BIND_AS(L, "add", typed_add), SALTS_OK);

        check_not_equal(luaL_dostring(L, "return add(1)"), LUA_OK);
        check_contains(lua_tostring(L, -1), "expects 2 argument");
        lua_pop(L, 1);

        check_not_equal(luaL_dostring(L, "return add('1', 2)"), LUA_OK);
        check_contains(lua_tostring(L, -1), "argument type mismatch");
        lua_pop(L, 1);
    }

    it("binds a yieldable C function through the common binder") {
        check_equal(C11_LUA_BIND_AS(L, "fetch", typed_fetch), SALTS_OK);

        check_equal(luaL_dostring(
                         L,
                         "local co = coroutine.create(function() return fetch(42) end);"
                         "local ok, pending = coroutine.resume(co);"
                         "assert(ok and pending == 'pending');"
                         "assert(coroutine.status(co) == 'suspended');"
                         "local resumed, result = coroutine.resume(co);"
                         "assert(resumed and result == 142);"
                         "assert(coroutine.status(co) == 'dead');"),
                     LUA_OK);
    }

    it("rejects coroutine suspension from the main Lua thread") {
        check_equal(C11_LUA_BIND(L, typed_fetch), SALTS_OK);
        check_not_equal(luaL_dostring(L, "return typed_fetch(1)"), LUA_OK);
        check_true(lua_isstring(L, -1));
        lua_pop(L, 1);
    }

    it("runs scripts in inherited and isolated environments") {
        int base = lua_gettop(L);

        check_equal(c11_lua_create_environment(L, "missing_child", "missing_parent", true),
                     SALTS_ENOENT);
        check_equal(lua_gettop(L), base);

        check_equal(c11_lua_create_environment(L, "parent_env", NULL, true), SALTS_OK);
        check_equal(c11_lua_create_environment(L, "child_env", "parent_env", true), SALTS_OK);
        check_equal(c11_lua_run_script_in_environment(L, "parent_env", "shared = 8"),
                     LUA_OK);
        check_equal(c11_lua_run_script_in_environment(L, "child_env", "x = shared + 1"),
                     LUA_OK);

        lua_getglobal(L, "child_env");
        lua_getfield(L, -1, "x");
        check_equal(lua_tointeger(L, -1), 9);
        lua_pop(L, 2);

        lua_getglobal(L, "shared");
        check_true(lua_isnil(L, -1));
        lua_pop(L, 1);

        check_equal(c11_lua_create_isolated_environment(L, "isolated", NULL), SALTS_OK);
        check_not_equal(c11_lua_run_script_in_environment(L, "isolated", "print('blocked')"),
                     LUA_OK);
        check_true(lua_isstring(L, -1));
        lua_pop(L, 1);
        check_equal(lua_gettop(L), base);
    }

    it("retains and releases registry references") {
        c11_lua_ref_t ref = C11_LUA_REF_INIT;

        lua_newtable(L);
        lua_pushinteger(L, 17);
        lua_setfield(L, -2, "value");
        check_equal(c11_lua_ref_create(L, -1, &ref), SALTS_OK);
        lua_pop(L, 1);

        check_true(c11_lua_ref_is_valid(&ref));
        check_equal(c11_lua_ref_push(L, &ref), SALTS_OK);
        lua_getfield(L, -1, "value");
        check_equal(lua_tointeger(L, -1), 17);
        lua_pop(L, 2);

        check_equal(c11_lua_ref_release(&ref), SALTS_OK);
        check_false(c11_lua_ref_is_valid(&ref));
        check_equal(c11_lua_ref_push(L, &ref), SALTS_ENOENT);
    }

    it("calls global functions through a protected boundary") {
        check_equal(C11_LUA_BIND_AS(L, "sum", typed_add), SALTS_OK);

        lua_pushinteger(L, 20);
        lua_pushinteger(L, 22);
        check_equal(c11_lua_pcall_global(L, "sum", 2, 1), LUA_OK);
        check_equal(lua_tointeger(L, -1), 42);
        lua_pop(L, 1);

        lua_pushinteger(L, 1);
        check_equal(c11_lua_pcall_global(L, "missing", 1, 1), LUA_ERRRUN);
        check_true(lua_isstring(L, -1));
        check_contains(lua_tostring(L, -1), "not callable");
        lua_pop(L, 1);
    }

    it("pushes tstr and vstr to Lua") {
        tstr owned = tstr_dup("Owned String");
        vstr view = vstr_from_cstr("View String");

        c11_lua_push(L, owned);
        c11_lua_push(L, view);
        c11_lua_push(L, 42);

        check_equal(lua_tostring(L, 1), "Owned String");
        check_equal(lua_tostring(L, 2), "View String");
        check_equal(lua_tointeger(L, 3), 42);

        lua_pop(L, 3);
        tstr_free(owned);
    }

    it("gets tstr and vstr from Lua") {
        lua_pushstring(L, "Test String");

        tstr owned = NULL;
        c11_lua_get(L, -1, &owned);
        check_equal(owned, "Test String");

        vstr view;
        c11_lua_get(L, -1, &view);
        check_equal(view.len, 11);
        check_true(memcmp(view.data, "Test String", 11) == 0);

        lua_pop(L, 1);

        // ✅ owned still valid after pop
        check_equal(owned, "Test String");
        tstr_free(owned);

        // ⚠️ view.data is now DANGLING - do not access
    }
}
