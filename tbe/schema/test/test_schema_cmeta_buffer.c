#include "tinytest.h"
#include "schema_cmeta.h"

#include <cbind/cbind.h>
#include <salts_cmeta_data.h>

#include <stddef.h>
#include <string.h>

/* Test-first declaration; the implementation belongs to the private schema layer. */
int schema_cmeta_buffer_data(cmeta_data_desc *out_data,
                             const char *stable_id,
                             const char *display_name,
                             cmeta_data_kind kind,
                             const cmeta_type_desc *storage_type,
                             const cmeta_data_buffer_shape *shape,
                             const cmeta_data_buffer_ops *ops);

#define COUNT_OF(items_) (sizeof(items_) / sizeof((items_)[0]))
#define SLICE_TOKEN(kind_, data_, size_, flags_) \
  { .kind = (kind_), .value.slice = { \
      (const unsigned char *)(data_), (size_), (flags_) } }
#define KEY_TOKEN(text_) \
  SLICE_TOKEN(CSERDE_STRING, text_, sizeof(text_) - 1u, CSERDE_VIEW_STABLE)

static const cmeta_data_buffer_shape owned_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_shape borrowed_shape = {CMETA_DATA_BUFFER_BORROWED};

typedef struct BufferCase {
  const char *stable_id;
  cmeta_data_kind kind;
  cserde_token_kind token_kind;
} BufferCase;

static const BufferCase buffer_cases[] = {
  {"test.schema.string", CMETA_DATA_STRING, CSERDE_STRING},
  {"test.schema.bytes", CMETA_DATA_BYTES, CSERDE_BYTES}
};

typedef struct TokenSource {
  const cserde_token *tokens;
  size_t count;
  size_t index;
} TokenSource;

static cserde_status next_token(void *context, cserde_token *out) {
  TokenSource *source = (TokenSource *)context;
  if (source->index == source->count) return CSERDE_DONE;
  *out = source->tokens[source->index++];
  return CSERDE_OK;
}

static const cserde_reader_ops reader_ops = {
  sizeof(cserde_reader_ops), CSERDE_READER_OPS_ABI_VERSION, next_token
};

static cbind_status decode_tokens(const cmeta_data_desc *data,
                                  const cserde_token *tokens, size_t count,
                                  void *out, size_t max_bytes,
                                  cbind_error *error, size_t *consumed) {
  unsigned char scratch[1] = {0};
  TokenSource source = {tokens, count, 0u};
  cserde_reader reader = {0};
  cbind_context context = CBIND_CONTEXT_WITH_BUFFERS_INIT(
      scratch, sizeof(scratch), 1u, 0u, max_bytes);
  cbind_status status;

  check_equal(cserde_reader_init(&reader, &reader_ops, &source), CSERDE_OK);
  status = cbind_decode(&context, data, &reader, out, error);
  if (consumed != NULL) *consumed = source.index;
  return status;
}

static void build_buffer(cmeta_data_desc *data, const BufferCase *item,
                         const cmeta_data_buffer_shape *shape,
                         const cmeta_data_buffer_ops *ops) {
  check_true(schema_cmeta_buffer_data(data, item->stable_id, "buffer",
                                      item->kind, ops->storage_type, shape, ops));
}

suite("schema_cmeta_buffer") {
  describe("explicit storage descriptor lowering") {
    it("keeps string and bytes semantics separate from the storage choice") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        cmeta_data_desc owned = {0};
        cmeta_data_desc borrowed = {0};
        build_buffer(&owned, &buffer_cases[i], &owned_shape,
                     &salts_tstr_cmeta_buffer_ops);
        build_buffer(&borrowed, &buffer_cases[i], &borrowed_shape,
                     &salts_vstr_cmeta_buffer_ops);
        check_true(cmeta_data_desc_valid(&owned));
        check_true(cmeta_data_desc_valid(&borrowed));
        check_equal(owned.kind, buffer_cases[i].kind);
        check_equal(borrowed.kind, buffer_cases[i].kind);
        check_true(owned.storage_type == &salts_tstr_cmeta_type);
        check_true(borrowed.storage_type == &salts_vstr_cmeta_type);
        check_true(owned.shape == &owned_shape);
        check_true(borrowed.shape == &borrowed_shape);
        check_true(cmeta_data_buffer_ops_of(&owned) == &salts_tstr_cmeta_buffer_ops);
        check_true(cmeta_data_buffer_ops_of(&borrowed) == &salts_vstr_cmeta_buffer_ops);
      }
      check_null(schema_cmeta_builtin_data("string"));
      check_null(schema_cmeta_builtin_data("bytes"));
    }

    it("accepts storage metadata copies by CMeta semantic identity") {
      cmeta_type_identity identity = salts_tstr_cmeta_identity;
      cmeta_type_desc storage = salts_tstr_cmeta_type;
      cmeta_data_buffer_ops ops = salts_tstr_cmeta_buffer_ops;
      cmeta_data_desc data = {0};
      const unsigned char input[] = {'a', 0, 'b'};
      tstr out = NULL;
      storage.identity = &identity;
      ops.storage_type = &storage;

      check_true(schema_cmeta_buffer_data(&data, "test.schema.copy", "copy",
          CMETA_DATA_BYTES, &salts_tstr_cmeta_type, &owned_shape, &ops));
      check_true(cmeta_type_identity_equal(data.storage_type->identity, &identity));
      check_equal(cmeta_data_buffer_assign(&data, &out, input, sizeof(input),
                                           sizeof(input)), CMETA_OK);
      check_equal(tstr_len(out), sizeof(input));
      check_equal(memcmp(out, input, sizeof(input)), 0);
      check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
      check_null(out);
    }

    it("rejects invalid or unsupported mappings without publishing a descriptor") {
      const cmeta_data_desc sentinel = {
        .struct_size = sizeof(cmeta_data_desc),
        .abi_version = CMETA_DATA_DESC_ABI_VERSION,
        .stable_id = "sentinel", .display_name = "sentinel",
        .kind = CMETA_DATA_BOOL, .storage_type = &cmeta_type_bool
      };
      cmeta_data_desc out = sentinel;
      unsigned char original[sizeof(out)];
      cmeta_data_buffer_ops incomplete = salts_tstr_cmeta_buffer_ops;
      cmeta_data_buffer_ops custom = salts_tstr_cmeta_buffer_ops;
      const cmeta_data_buffer_shape custom_shape = {CMETA_DATA_BUFFER_CUSTOM};
      struct InvalidCase {
        const char *id;
        const char *name;
        cmeta_data_kind kind;
        const cmeta_type_desc *storage;
        const cmeta_data_buffer_shape *shape;
        const cmeta_data_buffer_ops *ops;
      };
      const struct InvalidCase cases[] = {
        {NULL, "buffer", CMETA_DATA_STRING, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_SINT, &salts_tstr_cmeta_type,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &cmeta_type_int,
         &owned_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &borrowed_shape, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_STRING, &salts_tstr_cmeta_type,
         NULL, &salts_tstr_cmeta_buffer_ops},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, NULL},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &owned_shape, &incomplete},
        {"id", "buffer", CMETA_DATA_BYTES, &salts_tstr_cmeta_type,
         &custom_shape, &custom}
      };
      size_t i;
      memcpy(original, &out, sizeof(out));
      incomplete.assign = NULL;
      custom.ownership = CMETA_DATA_BUFFER_CUSTOM;
      for (i = 0u; i < COUNT_OF(cases); ++i) {
        check_false(schema_cmeta_buffer_data(&out, cases[i].id, cases[i].name,
            cases[i].kind, cases[i].storage, cases[i].shape, cases[i].ops));
        check_true(memcmp(&out, original, sizeof(out)) == 0);
      }
      check_false(schema_cmeta_buffer_data(NULL, "id", "buffer", CMETA_DATA_BYTES,
          &salts_tstr_cmeta_type, &owned_shape, &salts_tstr_cmeta_buffer_ops));
    }
  }

  describe("CBind consumption of lowered storage") {
    it("copies exact owned bytes including embedded NUL from transient input") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        unsigned char input[] = {'a', 0, 'b'};
        const cserde_token token = SLICE_TOKEN(buffer_cases[i].token_kind,
            input, sizeof(input), CSERDE_VIEW_TRANSIENT);
        cmeta_data_desc data = {0};
        tstr out = NULL;
        bool zero = false;
        build_buffer(&data, &buffer_cases[i], &owned_shape, &salts_tstr_cmeta_buffer_ops);
        check_equal(decode_tokens(&data, &token, 1u, &out, sizeof(input), NULL, NULL), CBIND_OK);
        check_true(out != NULL);
        check_true((const void *)out != (const void *)input);
        check_equal(tstr_len(out), sizeof(input));
        check_equal(memcmp(out, input, sizeof(input)), 0);
        input[0] = 'z';
        check_equal(out[0], 'a');
        check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
        check_equal(cmeta_data_buffer_is_zero(&data, &out, &zero), CMETA_OK);
        check_true(zero);
      }
    }

    it("retains stable borrowed views and never frees their source") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        unsigned char input[] = {'v', 0, 'w'};
        const cserde_token token = SLICE_TOKEN(buffer_cases[i].token_kind,
            input, sizeof(input), CSERDE_VIEW_STABLE);
        cmeta_data_desc data = {0};
        vstr out = {NULL, 0u};
        build_buffer(&data, &buffer_cases[i], &borrowed_shape, &salts_vstr_cmeta_buffer_ops);
        check_equal(decode_tokens(&data, &token, 1u, &out, sizeof(input), NULL, NULL), CBIND_OK);
        check_true(out.data == (const char *)input);
        check_equal(out.len, sizeof(input));
        check_equal(memcmp(out.data, input, sizeof(input)), 0);
        check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
        check_null(out.data);
        check_equal(out.len, (size_t)0u);
        check_equal(input[0], (unsigned char)'v');
      }
    }

    it("rejects transient borrowed input without an owning fallback") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        const cserde_token token = SLICE_TOKEN(buffer_cases[i].token_kind,
            "temp", 4u, CSERDE_VIEW_TRANSIENT);
        cmeta_data_desc data = {0};
        cbind_error error = CBIND_ERROR_INIT;
        vstr out = {NULL, 0u};
        build_buffer(&data, &buffer_cases[i], &borrowed_shape, &salts_vstr_cmeta_buffer_ops);
        check_equal(decode_tokens(&data, &token, 1u, &out, 4u, &error, NULL), CBIND_UNSUPPORTED);
        check_null(out.data);
        check_equal(out.len, (size_t)0u);
        check_true(error.shape == &data);
      }
    }

    it("preserves semantic zero for empty values limits and token mismatches") {
      size_t i;
      for (i = 0u; i < COUNT_OF(buffer_cases); ++i) {
        const cserde_token empty = SLICE_TOKEN(buffer_cases[i].token_kind,
            NULL, 0u, CSERDE_VIEW_STABLE);
        const cserde_token exact = SLICE_TOKEN(buffer_cases[i].token_kind,
            "123", 3u, CSERDE_VIEW_STABLE);
        const cserde_token wrong = SLICE_TOKEN(
            buffer_cases[i].token_kind == CSERDE_STRING ? CSERDE_BYTES : CSERDE_STRING,
            "123", 3u, CSERDE_VIEW_STABLE);
        cmeta_data_desc owned = {0};
        cmeta_data_desc borrowed = {0};
        cbind_error error = CBIND_ERROR_INIT;
        tstr text = NULL;
        vstr view = {NULL, 0u};
        build_buffer(&owned, &buffer_cases[i], &owned_shape, &salts_tstr_cmeta_buffer_ops);
        build_buffer(&borrowed, &buffer_cases[i], &borrowed_shape, &salts_vstr_cmeta_buffer_ops);
        check_equal(decode_tokens(&owned, &empty, 1u, &text, 0u, NULL, NULL), CBIND_OK);
        check_equal(decode_tokens(&borrowed, &empty, 1u, &view, 0u, NULL, NULL), CBIND_OK);
        check_null(text);
        check_null(view.data);
        check_equal(view.len, (size_t)0u);
        check_equal(decode_tokens(&owned, &exact, 1u, &text, 2u, &error, NULL), CBIND_LIMIT_EXCEEDED);
        check_equal(error.target_status, CMETA_CAPACITY_EXCEEDED);
        check_equal(decode_tokens(&borrowed, &exact, 1u, &view, 2u, NULL, NULL), CBIND_LIMIT_EXCEEDED);
        check_equal(decode_tokens(&owned, &wrong, 1u, &text, 3u, NULL, NULL), CBIND_TOKEN_MISMATCH);
        check_equal(decode_tokens(&borrowed, &wrong, 1u, &view, 3u, NULL, NULL), CBIND_TOKEN_MISMATCH);
        check_null(text);
        check_null(view.data);
        check_equal(view.len, (size_t)0u);
      }
    }

    it("rejects a nonzero destination before consuming input") {
      const cserde_token token = SLICE_TOKEN(CSERDE_BYTES, "new", 3u, CSERDE_VIEW_STABLE);
      cmeta_data_desc data = {0};
      tstr out = NULL;
      size_t consumed = SIZE_MAX;
      build_buffer(&data, &buffer_cases[1], &owned_shape, &salts_tstr_cmeta_buffer_ops);
      check_equal(cmeta_data_buffer_assign(&data, &out,
          (const unsigned char *)"old", 3u, 3u), CMETA_OK);
      check_equal(decode_tokens(&data, &token, 1u, &out, 3u, NULL, &consumed), CBIND_DESTINATION_NOT_EMPTY);
      check_equal(consumed, (size_t)0u);
      check_equal(tstr_len(out), (size_t)3u);
      check_equal(memcmp(out, "old", 3u), 0);
      check_equal(cmeta_data_buffer_restore_zero(&data, &out), CMETA_OK);
    }

    it("rolls back an earlier owned field when a borrowed field fails") {
      typedef struct BufferRecord { tstr payload; vstr alias; } BufferRecord;
      static const cmeta_type_identity identity = CMETA_TYPE_ID_ATOM_INIT("test.schema.record");
      static const cmeta_type_desc storage = {
        "BufferRecord", sizeof(BufferRecord), CMETA_ALIGNOF(BufferRecord),
        CMETA_T_OBJECT, NULL, NULL, &identity
      };
      static const cmeta_field_desc layout_fields[] = {
        {"payload", "tstr", offsetof(BufferRecord, payload), sizeof(tstr),
         CMETA_ALIGNOF(tstr), &salts_tstr_cmeta_type, NULL},
        {"alias", "vstr", offsetof(BufferRecord, alias), sizeof(vstr),
         CMETA_ALIGNOF(vstr), &salts_vstr_cmeta_type, NULL}
      };
      static const cmeta_struct_desc layout = {
        "BufferRecord", sizeof(BufferRecord), CMETA_ALIGNOF(BufferRecord),
        layout_fields, COUNT_OF(layout_fields)
      };
      cmeta_data_desc payload = {0}, alias = {0}, record = {0};
      cmeta_data_struct_shape shape = {0};
      const cmeta_data_field_desc fields[] = {
        {"test.schema.record.payload", "payload", offsetof(BufferRecord, payload), &payload},
        {"test.schema.record.alias", "alias", offsetof(BufferRecord, alias), &alias}
      };
      cserde_token tokens[] = {
        {.kind = CSERDE_MAP_BEGIN}, KEY_TOKEN("payload"),
        SLICE_TOKEN(CSERDE_BYTES, "ok", 2u, CSERDE_VIEW_TRANSIENT),
        KEY_TOKEN("alias"),
        SLICE_TOKEN(CSERDE_STRING, "view", 4u, CSERDE_VIEW_TRANSIENT),
        {.kind = CSERDE_MAP_END}
      };
      BufferRecord out = {0};
      cbind_error error = CBIND_ERROR_INIT;
      build_buffer(&payload, &buffer_cases[1], &owned_shape, &salts_tstr_cmeta_buffer_ops);
      build_buffer(&alias, &buffer_cases[0], &borrowed_shape, &salts_vstr_cmeta_buffer_ops);
      check_true(schema_cmeta_struct_data(&record, &shape, "test.schema.record.data",
          "BufferRecord", &storage, &layout, fields, COUNT_OF(fields)));
      check_equal(decode_tokens(&record, tokens, COUNT_OF(tokens), &out, 4u, &error, NULL), CBIND_UNSUPPORTED);
      check_null(out.payload);
      check_null(out.alias.data);
      check_equal(out.alias.len, (size_t)0u);
      check_true(error.shape == &alias);
      check_true(error.field == &fields[1]);
      check_equal(error.depth, (size_t)1u);

      tokens[4].value.slice.lifetime = CSERDE_VIEW_STABLE;
      check_equal(decode_tokens(&record, tokens, COUNT_OF(tokens), &out, 4u, NULL, NULL), CBIND_OK);
      check_equal(tstr_len(out.payload), (size_t)2u);
      check_equal(memcmp(out.payload, "ok", 2u), 0);
      check_true(out.alias.data == (const char *)tokens[4].value.slice.data);
      check_equal(cmeta_data_buffer_restore_zero(&payload, &out.payload), CMETA_OK);
      check_equal(cmeta_data_buffer_restore_zero(&alias, &out.alias), CMETA_OK);
    }
  }
}

#undef KEY_TOKEN
#undef SLICE_TOKEN
#undef COUNT_OF
