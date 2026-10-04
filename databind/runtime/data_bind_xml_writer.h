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
 * hidden in the generic FormatProvider ABI. The first writer profile accepts
 * exactly one flat CSerde MAP:
 *
 *   STRING key -> XML element name
 *   STRING / BOOL / SINT / UINT / finite FLOAT -> escaped leaf text
 *
 * NULL, BYTES, ARRAY and nested MAP values fail closed.
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
