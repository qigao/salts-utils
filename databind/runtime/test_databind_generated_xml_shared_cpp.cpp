#include "cmeta_graph_generated.h"
#include "tinytest.hpp"

#include <cstring>

spec("generated XML sequence DLL consumer") {
  it("owns nested records across the C DLL boundary and preserves them after failed input") {
    static const char xml[] =
        "<XmlSequenceEnvelope><data><marker>9</marker><old_values>1</old_values>"
        "<items><id>7</id><old_text>A&amp;B</old_text></items>"
        "<wire_values>-2</wire_values><switches>true</switches></data>"
        "<title>shared</title></XmlSequenceEnvelope>";
    static const char invalid[] =
        "<XmlSequenceEnvelope><data><marker>0</marker>"
        "<items><id>1</id><text>temporary</text></items>"
        "<items><id>bad</id></items></data><title>bad</title></XmlSequenceEnvelope>";
    DataBind *codec = nullptr;
    DataBindError error = DATA_BIND_ERROR_INIT;
    check_equal(Graph_codec_create(&codec, &error), DATA_BIND_OK);
    {
      Graph_typed::XmlSequenceEnvelopeOwner copy, decoded;
      {
        Graph_typed::XmlSequenceEnvelopeOwner source;
        check_equal(source.from_xml(codec, xml, sizeof(xml) - 1u, &error), DATA_BIND_OK);
        check_equal(cmeta_data_value_copy(&XmlSequenceEnvelope_CMETA_DATA, copy.get(), source.get()), CMETA_OK);
        check(copy->title != source->title);
        check(vec_at_const(&copy->data.items.raw, 0u) != vec_at_const(&source->data.items.raw, 0u));
      }
      const auto *item = static_cast<const XmlSequenceItem_t *>(vec_at_const(&copy->data.items.raw, 0u));
      check_not_null(item);
      check_equal(item->text, "A&B");
      check_equal(vec_size(&copy->data.values.raw), size_t(2u));
      check_equal(copy->title, "shared");
      check_not_equal(copy.from_xml(codec, invalid, sizeof(invalid) - 1u, &error), DATA_BIND_OK);
      check_equal(copy->data.marker, 9);
      check_equal(copy->title, "shared");
      char *output = nullptr;
      size_t length = 0u;
      check_equal(XmlSequenceEnvelope_to_xml(codec, copy.get(), &output, &length, &error), DATA_BIND_OK);
      check_contains(output, "<wire_values>1</wire_values><wire_values>-2</wire_values>");
      check_contains(output, "<wire_text>A&amp;B</wire_text>");
      check_equal(decoded.from_xml(codec, output, length, &error), DATA_BIND_OK);
      data_bind_serialized_free(output);
      output = nullptr;
      check_equal(XmlSequenceEnvelope_to_yaml(codec, copy.get(), &output, &length, &error), DATA_BIND_OK);
      check_equal(decoded.from_yaml(codec, output, length, &error), DATA_BIND_OK);
      check_equal(*static_cast<const uint8_t *>(vec_at_const(&decoded->data.switches.raw, 0u)), uint8_t(1u));
      data_bind_serialized_free(output);
    }
    data_bind_free(codec);
  }
}
