#include <salts/plugin.h>
#include <tinytest.h>

#include "plugin_test_interface.h"

#include <string.h>

typedef struct plugin_test_codec_state {
    int bias;
} plugin_test_codec_state;

static int plugin_test_transform(void *self, int value) {
    plugin_test_codec_state *state = (plugin_test_codec_state *)self;
    return value + state->bias;
}

CMETA_IMPLEMENTS(plugin_test_codec, plugin_test_codec_impl, 1u,
    .transform = plugin_test_transform);

FunctionDecl(value, int, plugin_test_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

int plugin_test_add(int left, int right) {
    return left + right;
}

FunctionDecl(value, int, plugin_test_add_other,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

int plugin_test_add_other(int left, int right) {
    return left + right + 1;
}

static salts_plugin_function_export make_function_export(void) {
    return (salts_plugin_function_export){
        .base = {
            .struct_size = SALTS_PLUGIN_FUNCTION_EXPORT_SIZE,
            .abi_version = SALTS_PLUGIN_ABI_VERSION,
            .kind = SALTS_PLUGIN_EXPORT_FUNCTION,
            .contract_version = 1u,
            .capabilities = 4u,
            .export_id = "add",
            .contract_id = "test.math.Add",
        },
        .function = FunctionMeta(plugin_test_add),
        .function_abi = FunctionAbi(plugin_test_add),
        .entry = (salts_plugin_function_entry)plugin_test_add,
    };
}

static salts_plugin_interface_export make_interface_export(
    plugin_test_codec *codec) {
    return (salts_plugin_interface_export){
        .base = {
            .struct_size = SALTS_PLUGIN_INTERFACE_EXPORT_SIZE,
            .abi_version = SALTS_PLUGIN_ABI_VERSION,
            .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
            .contract_version = 1u,
            .capabilities = 1u,
            .export_id = "codec",
            .contract_id = "test.codec",
        },
        .interface_desc = plugin_test_interface_a(),
        .interface_value = codec,
    };
}

static salts_plugin_manifest make_manifest(
    const salts_plugin_export *const *exports,
    size_t export_count) {
    return (salts_plugin_manifest){
        .struct_size = SALTS_PLUGIN_MANIFEST_SIZE,
        .abi_version = SALTS_PLUGIN_ABI_VERSION,
        .plugin_id = "test.plugin",
        .version = {2u, 0u, 0u},
        .capabilities = 5u,
        .exports = exports,
        .export_count = export_count,
    };
}

spec("Salts Plugin ABI 2 contract") {
describe("direct ABI 2 admission") {
    it("accepts one flat pointer table of Function and Interface exports") {
        plugin_test_codec_state state = {7};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_function_export fn = make_function_export();
        salts_plugin_interface_export iface = make_interface_export(&codec);
        const salts_plugin_export *exports[] = { &iface.base, &fn.base };
        salts_plugin_manifest manifest = make_manifest(exports, 2u);
        const salts_plugin_export *base = NULL;
        const salts_plugin_function_export *found_fn = NULL;
        const salts_plugin_interface_export *found_iface = NULL;
        typedef int (*add_fn)(int, int);
        add_fn exact;

        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_OK);

        check_equal(salts_plugin_manifest_find_function_export(
                        &manifest, "add", &found_fn),
                    SALTS_PLUGIN_OK);
        check_true(found_fn == &fn);
        exact = (add_fn)found_fn->entry;
        check_equal(exact(20, 22), 42);

        check_equal(salts_plugin_manifest_find_interface_export(
                        &manifest, "codec", &found_iface),
                    SALTS_PLUGIN_OK);
        check_true(found_iface == &iface);
        check_true(plugin_test_codec_valid(
            (const plugin_test_codec *)found_iface->interface_value));
        check_equal(plugin_test_codec_transform(
                        (plugin_test_codec *)found_iface->interface_value, 5),
                    12);

        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "add", &base),
                    SALTS_PLUGIN_OK);
        check_true(base == &fn.base);
        check_true(salts_plugin_export_has_capabilities(base, 4u));
        check_false(salts_plugin_export_has_capabilities(base, 8u));
    }

    it("requires exact manifest, export ABI and concrete row size") {
        salts_plugin_function_export fn = make_function_export();
        const salts_plugin_export *exports[] = { &fn.base };
        salts_plugin_manifest manifest = make_manifest(exports, 1u);

        manifest.abi_version = SALTS_PLUGIN_ABI_VERSION - 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);

        manifest = make_manifest(exports, 1u);
        manifest.struct_size = SALTS_PLUGIN_MANIFEST_SIZE - 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, 1u);
        fn.base.abi_version = SALTS_PLUGIN_ABI_VERSION - 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);

        fn = make_function_export();
        fn.base.struct_size = SALTS_PLUGIN_EXPORT_SIZE;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);
    }

    it("rejects removed CALLABLE/unknown export kinds instead of falling back") {
        salts_plugin_export unknown = {
            .struct_size = SALTS_PLUGIN_EXPORT_SIZE,
            .abi_version = SALTS_PLUGIN_ABI_VERSION,
            .kind = 99u,
            .contract_version = 1u,
            .export_id = "unknown",
            .contract_id = "test.unknown",
        };
        const salts_plugin_export *exports[] = { &unknown };
        salts_plugin_manifest manifest = make_manifest(exports, 1u);

        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);
    }

    it("rejects duplicate IDs across Function and Interface rows") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_function_export fn = make_function_export();
        salts_plugin_interface_export iface = make_interface_export(&codec);
        const salts_plugin_export *exports[] = { &iface.base, &fn.base };
        salts_plugin_manifest manifest = make_manifest(exports, 2u);

        fn.base.export_id = iface.base.export_id;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_DUPLICATE_EXPORT);
    }
}

describe("FunctionDesc export") {
    it("uses CMeta FunctionMeta/FunctionAbi and rejects malformed rows") {
        salts_plugin_function_export fn = make_function_export();
        const salts_plugin_export *exports[] = { &fn.base };
        salts_plugin_manifest manifest = make_manifest(exports, 1u);
        cmeta_function_abi_desc bad_abi = *FunctionAbi(plugin_test_add);
        const salts_plugin_function_export *required = NULL;

        check_equal(salts_plugin_export_require_function(
                        &fn.base, "test.math.Add", 1u, 4u, &required),
                    SALTS_PLUGIN_OK);
        check_true(required == &fn);
        check_true(required->function == FunctionMeta(plugin_test_add));
        check_true(required->function_abi == FunctionAbi(plugin_test_add));

        required = (const salts_plugin_function_export *)(uintptr_t)1u;
        check_equal(salts_plugin_export_require_function(
                        &fn.base, "test.math.Add", 2u, 4u, &required),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_null(required);

        fn.entry = NULL;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        fn = make_function_export();
        bad_abi.function = FunctionMeta(plugin_test_add_other);
        fn.function_abi = &bad_abi;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);
    }
}

describe("Interface export") {
    it("keeps provider/vtable semantics distinct from Function exports") {
        plugin_test_codec_state left_state = {1};
        plugin_test_codec_state right_state = {2};
        plugin_test_codec left_codec =
            plugin_test_codec_impl_as_plugin_test_codec(&left_state);
        plugin_test_codec right_codec =
            plugin_test_codec_impl_as_plugin_test_codec(&right_state);
        salts_plugin_interface_export iface = make_interface_export(&left_codec);
        const salts_plugin_interface_export *required = NULL;

        check_true(plugin_test_interface_a() != plugin_test_interface_b());
        check_true(salts_plugin_interface_desc_equal(
            plugin_test_interface_a(), plugin_test_interface_b()));

        iface.interface_desc = plugin_test_interface_b();
        iface.interface_value = &right_codec;
        check_equal(salts_plugin_export_require_interface(
                        &iface.base, "test.codec", 1u, 1u,
                        plugin_test_interface_a(), &required),
                    SALTS_PLUGIN_OK);
        check_true(required == &iface);
        check_equal(plugin_test_codec_transform(
                        (plugin_test_codec *)required->interface_value, 5),
                    7);
    }
}

describe("ABI layout") {
    it("publishes only exact ABI 2 layouts") {
        check_equal(SALTS_PLUGIN_ABI_VERSION, 2u);
        check_equal(SALTS_PLUGIN_EXPORT_SIZE,
                    (uint32_t)sizeof(salts_plugin_export));
        check_equal(SALTS_PLUGIN_FUNCTION_EXPORT_SIZE,
                    (uint32_t)sizeof(salts_plugin_function_export));
        check_equal(SALTS_PLUGIN_INTERFACE_EXPORT_SIZE,
                    (uint32_t)sizeof(salts_plugin_interface_export));
        check_equal(SALTS_PLUGIN_MANIFEST_SIZE,
                    (uint32_t)sizeof(salts_plugin_manifest));
    }
}

describe("status contract") {
    it("keeps required failure classes distinct") {
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INVALID_MANIFEST), "invalid_manifest"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_UNSUPPORTED_ABI), "unsupported_abi"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_DUPLICATE_EXPORT), "duplicate_export"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT),
                    "incompatible_contract"), 0);
    }
}
}
