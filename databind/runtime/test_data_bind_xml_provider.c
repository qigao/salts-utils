#include "data_bind_xml_provider.h"
#include "data_bind_xml_writer.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

#include <string.h>

static int next_kind(cserde_reader *reader, cserde_token_kind kind,
                     cserde_token *token) {
  memset(token, 0, sizeof(*token));
  return cserde_reader_next(reader, token) == CSERDE_OK &&
         token->kind == kind;
}

static int slice_equal(const cserde_token *token, const char *text) {
  size_t len = strlen(text);
  return token->kind == CSERDE_STRING &&
         token->value.slice.size == len &&
         memcmp(token->value.slice.data, text, len) == 0;
}

typedef struct XmlOutput {
  char data[2048];
  size_t size;
  size_t calls;
  int fail;
} XmlOutput;

static int xml_output_write(const void *data, size_t len, void *user) {
  XmlOutput *out = (XmlOutput *)user;
  if (out == NULL || (data == NULL && len != 0u)) return -1;
  ++out->calls;
  if (out->fail || len > sizeof(out->data) - out->size - 1u)
    return -1;
  if (len != 0u) memcpy(out->data + out->size, data, len);
  out->size += len;
  out->data[out->size] = '\0';
  return 0;
}

static cserde_token xml_string(const char *text) {
  cserde_token token = {0};
  token.kind = CSERDE_STRING;
  token.value.slice.data = (const unsigned char *)text;
  token.value.slice.size = strlen(text);
  token.value.slice.lifetime = CSERDE_VIEW_STABLE;
  return token;
}


int main(void) {
  static const char xml[] =
      "<root id=\"7\" name=\"attribute\">"
      "<name>alice</name><tag>x</tag><tag>y</tag>"
      "</root>";
  DataBindFormatReader lease = DATA_BIND_FORMAT_READER_INIT;
  DataBindError error = DATA_BIND_ERROR_INIT;
  DataBindQueryLimits limits = DATA_BIND_QUERY_LIMITS_INIT;
  DataBindQueryDiagnostic diagnostic = DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  cserde_token token = {0};

  if (data_bind_format_reader_open(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 0u, &lease, &error) != DATA_BIND_OK)
    return 1;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_LIMIT_EXCEEDED)
    return 2;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 3;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return 4;

  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 5;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "name")) return 6;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "alice")) return 7;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return 8;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "x")) return 9;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return 10;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return 11;
  /* The child element named "name" wins over the same-named attribute. */
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "id")) return 12;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "7")) return 13;
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 14;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 15;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 16;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "/root/tag[2]",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 17;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return 18;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 19;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 20;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "/root/tag",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return 21;
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return 22;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "x")) return 23;
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return 24;
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return 25;
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 26;
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 27;

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "//*[",
          &limits, &diagnostic, &lease, &error) == DATA_BIND_OK)
    return 28;
  if (lease.reader != NULL) return 29;

  {
    static const char entity_xml[] =
        "<r attr='x&amp;y'><text>a&amp;b&#33;&#x1F600;</text>"
        "<literal><![CDATA[c&amp;d]]></literal></r>";

    lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_format_reader_open(
            data_bind_xml_format_provider(),
            entity_xml, sizeof(entity_xml) - 1u, 8u,
            &lease, &error) != DATA_BIND_OK)
      return 66;
    if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 67;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "text")) return 68;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_TRANSIENT ||
        token.value.slice.size != 8u ||
        memcmp(token.value.slice.data, "a&b!\xF0\x9F\x98\x80", 8u) != 0)
      return 69;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "literal")) return 70;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_STABLE ||
        !slice_equal(&token, "c&amp;d"))
      return 71;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "attr")) return 72;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_TRANSIENT ||
        !slice_equal(&token, "x&y"))
      return 73;
    if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 74;
    if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 75;
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {0};

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "NativeXmlFlat", xml_output_write, &output, 0u,
            &writer, &error) != DATA_BIND_ERR_LIMIT)
      return 30;

    writer = (DataBindXmlWriter)DATA_BIND_XML_WRITER_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "NativeXmlFlat", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_OK)
      return 31;
    native = data_bind_xml_writer_writer(&writer);
    if (native == NULL) return 32;

    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 33;
    token = xml_string("id");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 34;
    token = (cserde_token){.kind = CSERDE_UINT, .value.uint = UINT64_C(9)};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 35;
    token = xml_string("name");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 36;
    token = xml_string("a&b");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 37;
    token = xml_string("score");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 38;
    token = (cserde_token){.kind = CSERDE_FLOAT, .value.floating = 2.5};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 39;
    token = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 40;
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_OK)
      return 41;
    if (output.calls != 1u || output.size == 0u) return 42;
    if (strstr(output.data, "<NativeXmlFlat>") == NULL ||
        strstr(output.data, "<id>9</id>") == NULL ||
        strstr(output.data, "<name>a&amp;b</name>") == NULL ||
        strstr(output.data, "<score>2.5</score>") == NULL)
      return 43;

    lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
    if (data_bind_format_reader_open(
            data_bind_xml_format_provider(), output.data, output.size,
            4u, &lease, &error) != DATA_BIND_OK)
      return 44;
    if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return 45;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "id")) return 46;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "9")) return 47;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "name")) return 48;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "a&b")) return 49;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "score")) return 50;
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "2.5")) return 51;
    if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return 52;
    if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return 53;
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {0};

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "Flat", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_OK)
      return 54;
    native = data_bind_xml_writer_writer(&writer);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 55;
    token = xml_string("value");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 56;
    token = (cserde_token){.kind = CSERDE_NULL};
    if (cserde_writer_write(native, &token) != CSERDE_UNSUPPORTED)
      return 57;
    if (data_bind_xml_writer_close(&writer, &error) !=
        DATA_BIND_ERR_TYPE_MISMATCH)
      return 58;
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {0};

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "Flat", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_OK)
      return 59;
    native = data_bind_xml_writer_writer(&writer);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 60;
    token = xml_string("nested");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 61;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 62;
    token = xml_string("text");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 81;
    token = xml_string("a&b");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 82;
    token = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 83;
    token = xml_string("sibling");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 84;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 85;
    token = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 86;
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 87;
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_OK) return 63;
    if (strstr(output.data, "<nested><text>a&amp;b</text></nested>") == NULL ||
        strstr(output.data, "<sibling") == NULL || output.calls != 1u) return 88;
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "bad root", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_ERR_INVALID_ARG)
      return 64;
    if (data_bind_xml_writer_writer(&writer) != NULL) return 65;
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {0};

    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "Flat", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_OK)
      return 76;
    native = data_bind_xml_writer_writer(&writer);
    if (native == NULL) return 77;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 78;
    token = xml_string("bad key");
    if (cserde_writer_write(native, &token) != CSERDE_UNSUPPORTED)
      return 79;
    if (data_bind_xml_writer_close(&writer, &error) !=
        DATA_BIND_ERR_TYPE_MISMATCH)
      return 80;
  }

  /* The root consumes one frame. A limit failure never publishes bytes. */
  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    if (data_bind_xml_writer_open_root("Root", xml_output_write, &output,
                                      SIZE_MAX, &writer, &error) != DATA_BIND_ERR_LIMIT)
      return 89;
    if (data_bind_xml_writer_open_root("Root", xml_output_write, &output,
                                      1u, &writer, &error) != DATA_BIND_OK)
      return 90;
    native = data_bind_xml_writer_writer(&writer);
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 91;
    token = xml_string("child");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 92;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_LIMIT_EXCEEDED) return 93;
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_LIMIT) return 94;
    if (output.calls != 0u || output.size != 0u) return 95;
  }

  /* Closing an unfinished nested record must release its pending key. */
  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    cserde_writer *native;
    cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    if (data_bind_xml_writer_open_root("Root", xml_output_write, &output,
                                      2u, &writer, &error) != DATA_BIND_OK) return 96;
    native = data_bind_xml_writer_writer(&writer);
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 97;
    token = xml_string("child");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 98;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 99;
    token = xml_string("pending");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 100;
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_TYPE_MISMATCH) return 101;
    if (output.calls != 0u || output.size != 0u || writer.owner != NULL) return 102;
  }

  /* A complete nested document propagates a rejected sink without retrying. */
  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 1};
    cserde_writer *native;
    cserde_token token = {.kind = CSERDE_MAP_BEGIN};
    if (data_bind_xml_writer_open_root("Root", xml_output_write, &output,
                                      2u, &writer, &error) != DATA_BIND_OK) return 103;
    native = data_bind_xml_writer_writer(&writer);
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 104;
    token = xml_string("child");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 105;
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return 106;
    token = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(native, &token) != CSERDE_OK ||
        cserde_writer_write(native, &token) != CSERDE_OK) return 107;
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_IO) return 108;
    if (output.calls != 1u || output.size != 0u) return 109;
  }

  /* Schema projection synthesizes empty required sequences, while the lease
   * remains independent of the codec and includes ARRAY frames in its bound. */
  {
    static const char schema[] = "message Seq { [name(wire), alias(old)] list<int32> items; }";
    static const char input[] = "<Seq/>";
    DataBind *codec = NULL;
    DataBindFormatPlan *plan = NULL;
    size_t depth;
    if (data_bind_create_from_text(schema, sizeof(schema) - 1u, &codec, &error) != DATA_BIND_OK)
      return 110;
    if (data_bind_format_plan_compile(codec, "Seq", DATA_BIND_FORMAT_XML, &plan, &error) != DATA_BIND_OK)
      return 111;
    data_bind_free(codec);
    for (depth = 1u; depth <= 2u; ++depth) {
      lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
      if (data_bind_xml_format_reader_open_plan(plan, input, sizeof(input) - 1u,
                                              depth, &lease, &error) != DATA_BIND_OK) return 112;
      if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token) ||
          !next_kind(lease.reader, CSERDE_STRING, &token) || !slice_equal(&token, "items")) return 113;
      if (depth == 1u) {
        if (cserde_reader_next(lease.reader, &token) != CSERDE_LIMIT_EXCEEDED) return 114;
      } else {
        if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token) ||
            !next_kind(lease.reader, CSERDE_ARRAY_END, &token) ||
            !next_kind(lease.reader, CSERDE_MAP_END, &token) ||
            cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return 115;
      }
      if (data_bind_format_reader_close(&lease) != DATA_BIND_OK || lease.owner != NULL) return 116;
    }
    data_bind_format_plan_free(plan);
  }

  /* ARRAY names remain owned until the array closes, including error paths. */
  {
    size_t attempt;
    for (attempt = 0u; attempt < 3u; ++attempt) {
      DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
      XmlOutput output = {{0}, 0u, 0u, attempt == 2u};
      cserde_writer *native;
      cserde_token token = {.kind = CSERDE_MAP_BEGIN};
      if (data_bind_xml_writer_open_root("Seq", xml_output_write, &output,
                                        attempt == 0u ? 1u : 2u, &writer, &error) != DATA_BIND_OK) return 117;
      native = data_bind_xml_writer_writer(&writer);
      if (cserde_writer_write(native, &token) != CSERDE_OK) return 118;
      token = xml_string("items");
      if (cserde_writer_write(native, &token) != CSERDE_OK) return 119;
      token = (cserde_token){.kind = CSERDE_ARRAY_BEGIN};
      if (attempt == 0u) {
        if (cserde_writer_write(native, &token) != CSERDE_LIMIT_EXCEEDED ||
            data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_LIMIT) return 120;
      } else {
        if (cserde_writer_write(native, &token) != CSERDE_OK) return 121;
        token = xml_string("A&B");
        if (cserde_writer_write(native, &token) != CSERDE_OK) return 122;
        if (attempt == 1u) {
          if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_TYPE_MISMATCH) return 123;
        } else {
          token = (cserde_token){.kind = CSERDE_ARRAY_END};
          if (cserde_writer_write(native, &token) != CSERDE_OK) return 124;
          token = (cserde_token){.kind = CSERDE_MAP_END};
          if (cserde_writer_write(native, &token) != CSERDE_OK ||
              data_bind_xml_writer_close(&writer, &error) != DATA_BIND_ERR_IO) return 125;
        }
      }
      if (writer.owner != NULL || output.size != 0u || output.calls != (attempt == 2u ? 1u : 0u))
        return 126;
    }
  }
  return 0;
}
