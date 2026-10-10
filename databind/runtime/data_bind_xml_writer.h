#ifndef DATA_BIND_XML_WRITER_H
#define DATA_BIND_XML_WRITER_H

#include "data_bind.h"

#include <cserde/writer.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Explicit-root XML writer lease.
 *
 * XML output requires a document-root identity, which is deliberately not
 * hidden in the generic FormatProvider ABI. The writer accepts one CSerde MAP
 * with recursively nested MAP values and record-field ARRAY values:
 *
 *   STRING key -> XML element name
 *   STRING / BOOL / SINT / UINT / finite FLOAT -> escaped leaf text
 *
 * MAP values become nested elements. ARRAY items repeat the owning field name;
 * an empty ARRAY emits no element. Items may be scalar or MAP, but not ARRAY.
 * NULL and BYTES fail closed. max_depth bounds simultaneously open MAP/ARRAY
 * frames, including the root. The lease
 * owns its document, stack and copied keys; tokens are borrowed only during
 * write(). Only a complete document is delivered to the sink on finish().
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

#endif /* DATA_BIND_XML_WRITER_H */
