#ifndef DATA_BIND_XML_PROVIDER_H
#define DATA_BIND_XML_PROVIDER_H

#include "data_bind_format_provider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the statically linked XML provider.
 *
 * The provider projects the document root into DataBind's schema-facing token
 * model: element children and attributes become map entries, leaf elements
 * become strings, repeated child names remain repeated map keys, and a child
 * element takes precedence over an attribute with the same display name.
 *
 * Comments, processing instructions and formatting-only text are not emitted,
 * matching the existing DataBind XML field-binding semantics. No XPath,
 * registry lookup, fallback or alternate parser is selected by this provider.
 */
const DataBindFormatProvider *data_bind_xml_format_provider(void);

/**
 * Explicit-root XML writer lease.
 *
 * XML document output requires a root element identity, which is not part of
 * the generic DataBindFormatWriter ABI. This dedicated lease therefore takes
 * root_name explicitly instead of inventing a default root or hiding state in
 * the generic provider.
 *
 * The first v1 writer profile accepts exactly one flat CSerde MAP whose keys
 * are XML element names and whose values are STRING/BOOL/SINT/UINT/finite
 * FLOAT. NULL, BYTES, ARRAY and nested MAP values fail closed.
 */
typedef struct DataBindXmlWriter {
  size_t size;
  cserde_writer *writer;
  void *owner;
} DataBindXmlWriter;

#define DATA_BIND_XML_WRITER_INIT \
  { sizeof(DataBindXmlWriter), NULL, NULL }

DATA_BIND_API DataBindStatus data_bind_xml_writer_open_root(
    const char *root_name,
    DataBindWriteFn write,
    void *write_user,
    size_t max_depth,
    DataBindXmlWriter *out_writer,
    DataBindError *error);

DATA_BIND_API cserde_writer *
data_bind_xml_writer_writer(DataBindXmlWriter *writer);

DATA_BIND_API DataBindStatus data_bind_xml_writer_close(
    DataBindXmlWriter *writer,
    DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_XML_PROVIDER_H */
