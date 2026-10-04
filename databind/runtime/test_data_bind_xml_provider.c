#include "data_bind_xml_provider.h"
#include "data_bind_xml_writer.h"

#include <cserde/reader.h>
#include <cserde/writer.h>

#include <stdio.h>
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


static int xml_test_fail(int code, const cserde_token *token,
                         const DataBindError *error) {
  fprintf(stderr,
          "XML_PROVIDER_FAIL code=%d token_kind=%d slice_size=%zu "
          "lifetime=%d error=%d path=%s message=%s\n",
          code,
          token != NULL ? (int)token->kind : -1,
          token != NULL ? token->value.slice.size : 0u,
          token != NULL ? (int)token->value.slice.lifetime : -1,
          error != NULL ? (int)error->code : -1,
          error != NULL ? error->path : "",
          error != NULL ? error->message : "");
  return code;
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
    return xml_test_fail(1, &token, &error);
  if (cserde_reader_next(lease.reader, &token) != CSERDE_LIMIT_EXCEEDED)
    return xml_test_fail(2, &token, &error);
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(3, &token, &error);

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  if (data_bind_format_reader_open(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u, &lease, &error) != DATA_BIND_OK)
    return xml_test_fail(4, &token, &error);

  if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return xml_test_fail(5, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "name")) return xml_test_fail(6, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "alice")) return xml_test_fail(7, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return xml_test_fail(8, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "x")) return xml_test_fail(9, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "tag")) return xml_test_fail(10, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return xml_test_fail(11, &token, &error);
  /* The child element named "name" wins over the same-named attribute. */
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "id")) return xml_test_fail(12, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "7")) return xml_test_fail(13, &token, &error);
  if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return xml_test_fail(14, &token, &error);
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return xml_test_fail(15, &token, &error);
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(16, &token, &error);

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "/root/tag[2]",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return xml_test_fail(17, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return xml_test_fail(18, &token, &error);
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return xml_test_fail(19, &token, &error);
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(20, &token, &error);

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_ALL, "/root/tag",
          &limits, &diagnostic, &lease, &error) != DATA_BIND_OK)
    return xml_test_fail(21, &token, &error);
  if (!next_kind(lease.reader, CSERDE_ARRAY_BEGIN, &token)) return xml_test_fail(22, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "x")) return xml_test_fail(23, &token, &error);
  if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
      !slice_equal(&token, "y")) return xml_test_fail(24, &token, &error);
  if (!next_kind(lease.reader, CSERDE_ARRAY_END, &token)) return xml_test_fail(25, &token, &error);
  if (cserde_reader_next(lease.reader, &token) != CSERDE_DONE) return xml_test_fail(26, &token, &error);
  if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(27, &token, &error);

  lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
  error = (DataBindError)DATA_BIND_ERROR_INIT;
  diagnostic = (DataBindQueryDiagnostic)DATA_BIND_QUERY_DIAGNOSTIC_INIT;
  if (data_bind_format_reader_open_selected(
          data_bind_xml_format_provider(),
          xml, sizeof(xml) - 1u, 8u,
          DATA_BIND_STREAM_SELECT_PATH_FIRST, "//*[",
          &limits, &diagnostic, &lease, &error) == DATA_BIND_OK)
    return xml_test_fail(28, &token, &error);
  if (lease.reader != NULL) return xml_test_fail(29, &token, &error);

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
      return xml_test_fail(66, &token, &error);
    if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return xml_test_fail(67, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "text")) return xml_test_fail(68, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_TRANSIENT ||
        token.value.slice.size != 8u ||
        memcmp(token.value.slice.data, "a&b!\xF0\x9F\x98\x80", 8u) != 0)
      return xml_test_fail(69, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "literal")) return xml_test_fail(70, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_STABLE ||
        !slice_equal(&token, "c&amp;d"))
      return xml_test_fail(71, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "attr")) return xml_test_fail(72, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        token.value.slice.lifetime != CSERDE_VIEW_TRANSIENT ||
        !slice_equal(&token, "x&y"))
      return xml_test_fail(73, &token, &error);
    if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return xml_test_fail(74, &token, &error);
    if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(75, &token, &error);
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
      return xml_test_fail(30, &token, &error);

    writer = (DataBindXmlWriter)DATA_BIND_XML_WRITER_INIT;
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "NativeXmlFlat", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_OK)
      return xml_test_fail(31, &token, &error);
    native = data_bind_xml_writer_writer(&writer);
    if (native == NULL) return xml_test_fail(32, &token, &error);

    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(33, &token, &error);
    token = xml_string("id");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(34, &token, &error);
    token = (cserde_token){.kind = CSERDE_UINT, .value.uint = UINT64_C(9)};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(35, &token, &error);
    token = xml_string("name");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(36, &token, &error);
    token = xml_string("a&b");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(37, &token, &error);
    token = xml_string("score");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(38, &token, &error);
    token = (cserde_token){.kind = CSERDE_FLOAT, .value.floating = 2.5};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(39, &token, &error);
    token = (cserde_token){.kind = CSERDE_MAP_END};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(40, &token, &error);
    if (data_bind_xml_writer_close(&writer, &error) != DATA_BIND_OK)
      return xml_test_fail(41, &token, &error);
    if (output.calls != 1u || output.size == 0u) return xml_test_fail(42, &token, &error);
    if (strstr(output.data, "<NativeXmlFlat>") == NULL ||
        strstr(output.data, "<id>9</id>") == NULL ||
        strstr(output.data, "<name>a&amp;b</name>") == NULL ||
        strstr(output.data, "<score>2.5</score>") == NULL)
      return xml_test_fail(43, &token, &error);

    lease = (DataBindFormatReader)DATA_BIND_FORMAT_READER_INIT;
    if (data_bind_format_reader_open(
            data_bind_xml_format_provider(), output.data, output.size,
            4u, &lease, &error) != DATA_BIND_OK)
      return xml_test_fail(44, &token, &error);
    if (!next_kind(lease.reader, CSERDE_MAP_BEGIN, &token)) return xml_test_fail(45, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "id")) return xml_test_fail(46, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "9")) return xml_test_fail(47, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "name")) return xml_test_fail(48, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "a&b")) return xml_test_fail(49, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "score")) return xml_test_fail(50, &token, &error);
    if (!next_kind(lease.reader, CSERDE_STRING, &token) ||
        !slice_equal(&token, "2.5")) return xml_test_fail(51, &token, &error);
    if (!next_kind(lease.reader, CSERDE_MAP_END, &token)) return xml_test_fail(52, &token, &error);
    if (data_bind_format_reader_close(&lease) != DATA_BIND_OK) return xml_test_fail(53, &token, &error);
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
      return xml_test_fail(54, &token, &error);
    native = data_bind_xml_writer_writer(&writer);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(55, &token, &error);
    token = xml_string("value");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(56, &token, &error);
    token = (cserde_token){.kind = CSERDE_NULL};
    if (cserde_writer_write(native, &token) != CSERDE_UNSUPPORTED)
      return xml_test_fail(57, &token, &error);
    if (data_bind_xml_writer_close(&writer, &error) !=
        DATA_BIND_ERR_TYPE_MISMATCH)
      return xml_test_fail(58, &token, &error);
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
      return xml_test_fail(59, &token, &error);
    native = data_bind_xml_writer_writer(&writer);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(60, &token, &error);
    token = xml_string("nested");
    if (cserde_writer_write(native, &token) != CSERDE_OK) return xml_test_fail(61, &token, &error);
    token = (cserde_token){.kind = CSERDE_MAP_BEGIN};
    if (cserde_writer_write(native, &token) != CSERDE_UNSUPPORTED)
      return xml_test_fail(62, &token, &error);
    if (data_bind_xml_writer_close(&writer, &error) !=
        DATA_BIND_ERR_TYPE_MISMATCH)
      return xml_test_fail(63, &token, &error);
  }

  {
    DataBindXmlWriter writer = DATA_BIND_XML_WRITER_INIT;
    XmlOutput output = {{0}, 0u, 0u, 0};
    error = (DataBindError)DATA_BIND_ERROR_INIT;
    if (data_bind_xml_writer_open_root(
            "bad root", xml_output_write, &output, 2u,
            &writer, &error) != DATA_BIND_ERR_INVALID_ARG)
      return xml_test_fail(64, &token, &error);
    if (data_bind_xml_writer_writer(&writer) != NULL) return xml_test_fail(65, &token, &error);
  }

  return 0;
}
