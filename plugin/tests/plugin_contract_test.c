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

typed_any(value, int, plugin_test_increment, (int value)) {
    return value + 1;
}

typed_any(value, int, plugin_test_decrement, (int value)) {
    return value - 1;
}

typed_any(value, long, plugin_test_widen, (int value)) {
    return (long)value;
}

static salts_plugin_manifest make_manifest(
    salts_plugin_export exports[2],
    plugin_test_codec *codec) {
    exports[0] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
        .contract_version = 1u,
        .capabilities = 1u,
        .export_id = "codec",
        .contract_id = "test.codec",
        .interface_desc = plugin_test_interface_a(),
        .interface_value = codec,
        .callable = NULL,
    };
    exports[1] = (salts_plugin_export){
        .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
        .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
        .kind = SALTS_PLUGIN_EXPORT_CALLABLE,
        .contract_version = 1u,
        .capabilities = 2u,
        .export_id = "increment",
        .contract_id = "test.transform",
        .interface_desc = NULL,
        .interface_value = NULL,
        .callable = &plugin_test_increment,
    };

    return (salts_plugin_manifest){
        .struct_size = SALTS_PLUGIN_MANIFEST_V1_SIZE,
        .abi_version = SALTS_PLUGIN_ABI_VERSION,
        .plugin_id = "test.plugin",
        .version = {1u, 2u, 3u},
        .capabilities = 3u,
        .exports = exports,
        .export_count = 2u,
    };
}

spec("Salts Plugin ABI contract") {
describe("manifest admission") {
    it("accepts finite Interface and Callable exports") {
        plugin_test_codec_state state = {7};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);
        const salts_plugin_export *found = NULL;

        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "codec", &found),
                    SALTS_PLUGIN_OK);
        check_true(found == &exports[0]);
        check_true(plugin_test_codec_valid(
            (const plugin_test_codec *)found->interface_value));
        check_equal(plugin_test_codec_transform(
                        (plugin_test_codec *)found->interface_value, 5),
                    12);
        check_true(salts_plugin_export_has_capabilities(found, 1u));
        check_false(salts_plugin_export_has_capabilities(found, 2u));
        check_equal(salts_plugin_export_require_interface(
                        found, "test.codec", 1u, 1u,
                        plugin_test_interface_b()),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_export_require_interface(
                        found, "test.codec", 2u, 1u,
                        plugin_test_interface_b()),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);
        check_equal(salts_plugin_export_require_interface(
                        found, "test.codec", 1u, 2u,
                        plugin_test_interface_b()),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

        check_equal(salts_plugin_manifest_find_export(
                        &manifest, "missing", &found),
                    SALTS_PLUGIN_UNKNOWN_EXPORT);
        check_null(found);
    }

    it("rejects ABI and structural mismatches before lifecycle use") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);

        manifest.abi_version = SALTS_PLUGIN_ABI_VERSION + 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);

        manifest = make_manifest(exports, &codec);
        manifest.struct_size = SALTS_PLUGIN_MANIFEST_V1_SIZE - 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        exports[0].struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE - 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_INVALID_MANIFEST);

        manifest = make_manifest(exports, &codec);
        exports[1].abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION + 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_UNSUPPORTED_ABI);
    }

    it("rejects duplicate exports and bounded-capacity overflow") {
        plugin_test_codec_state state = {0};
        plugin_test_codec codec =
            plugin_test_codec_impl_as_plugin_test_codec(&state);
        salts_plugin_export exports[2];
        salts_plugin_manifest manifest = make_manifest(exports, &codec);

        exports[1].export_id = exports[0].export_id;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_DUPLICATE_EXPORT);

        manifest = make_manifest(exports, &codec);
        manifest.export_count = SALTS_PLUGIN_MAX_EXPORTS + 1u;
        check_equal(salts_plugin_manifest_validate(
                        &manifest, SALTS_PLUGIN_ABI_VERSION),
                    SALTS_PLUGIN_CAPACITY_EXCEEDED);
    }
}

describe("semantic identity") {
    it("compares independently compiled Interface representations by content") {
        const cmeta_interface_desc *left = plugin_test_interface_a();
        const cmeta_interface_desc *right = plugin_test_interface_b();

        check_not_null(left);
        check_not_null(right);
        check_true(left != right);
        check_true(salts_plugin_interface_desc_valid(left));
        check_true(salts_plugin_interface_desc_valid(right));
        check_true(salts_plugin_interface_desc_equal(left, right));
    }

    it("does not use descriptor, implementation, or callable addresses as contract identity") {
        plugin_test_codec_state left_state = {1};
        plugin_test_codec_state right_state = {2};
        plugin_test_codec left_codec =
            plugin_test_codec_impl_as_plugin_test_codec(&left_state);
        plugin_test_codec right_codec =
            plugin_test_codec_impl_as_plugin_test_codec(&right_state);
        salts_plugin_export left_interface = {
            .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
            .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
            .kind = SALTS_PLUGIN_EXPORT_INTERFACE,
            .contract_version = 1u,
            .export_id = "left",
            .contract_id = "test.codec",
            .interface_desc = plugin_test_interface_a(),
            .interface_value = &left_codec,
        };
        salts_plugin_export right_interface = left_interface;
        salts_plugin_export left_callable = {
            .struct_size = SALTS_PLUGIN_EXPORT_V1_SIZE,
            .abi_version = SALTS_PLUGIN_EXPORT_ABI_VERSION,
            .kind = SALTS_PLUGIN_EXPORT_CALLABLE,
            .contract_version = 1u,
            .export_id = "inc",
            .contract_id = "test.transform",
            .callable = &plugin_test_increment,
        };
        salts_plugin_export right_callable = left_callable;

        right_interface.export_id = "right";
        right_interface.interface_desc = plugin_test_interface_b();
        right_interface.interface_value = &right_codec;
        check_true(left_interface.interface_desc !=
                   right_interface.interface_desc);
        check_true(left_interface.interface_value !=
                   right_interface.interface_value);
        check_true(salts_plugin_export_contract_equal(
            &left_interface, &right_interface));

        right_callable.export_id = "dec";
        right_callable.callable = &plugin_test_decrement;
        check_true(left_callable.callable != right_callable.callable);
        check_false(cmeta_callable_same(
            *left_callable.callable, *right_callable.callable));
        check_true(salts_plugin_export_contract_equal(
            &left_callable, &right_callable));

        check_equal(salts_plugin_export_require_callable(
                        &left_callable, "test.transform", 1u, 0u,
                        &plugin_test_decrement),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_export_require_callable(
                        &left_callable, "test.transform", 1u, 0u,
                        &plugin_test_widen),
                    SALTS_PLUGIN_INCOMPATIBLE_CONTRACT);

        right_callable.contract_version = 2u;
        check_false(salts_plugin_export_contract_equal(
            &left_callable, &right_callable));
    }
}

describe("status contract") {
    it("keeps required failure classes distinct") {
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INVALID_MANIFEST), "invalid_manifest"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_UNSUPPORTED_ABI), "unsupported_abi"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_DUPLICATE_PLUGIN_ID), "duplicate_plugin_id"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_DUPLICATE_EXPORT), "duplicate_export"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_UNKNOWN_EXPORT), "unknown_export"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_INCOMPATIBLE_CONTRACT),
                    "incompatible_contract"), 0);
        check_equal(strcmp(salts_plugin_status_string(
                        SALTS_PLUGIN_CAPACITY_EXCEEDED),
                    "capacity_exceeded"), 0);
    }
}
}
