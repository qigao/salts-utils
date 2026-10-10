#ifndef DATA_BIND_XML_PROVIDER_H
#define DATA_BIND_XML_PROVIDER_H

#include "data_bind_format_provider.h"
#include "data_bind_projection_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Return the built-in XML provider.
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
DATA_BIND_API const DataBindFormatProvider *data_bind_xml_format_provider(void);

/** Open an XML record with a compiled XML FormatPlan. Required list/set fields
 * collect repeated child elements (including aliases) in document order;
 * no matching elements means an empty sequence. Optional/nullable sequences,
 * nested sequence elements, maps and variants are rejected by plan admission.
 * Non-sequence contracts retain the ordinary provider's token behavior.
 * The immutable plan is borrowed until data_bind_format_reader_close(). The
 * lease owns the DOM, decoded text scratch and stack bounded by max_depth
 * (including MAP and ARRAY frames). Input bytes follow the ordinary provider
 * lifetime contract. Unknown/duplicate scalar fields fail during consumption.
 * Returns INVALID_ARG for invalid arguments, SCHEMA for a non-XML record plan,
 * or the normal format-reader parse/allocation/limit status. */
DATA_BIND_API DataBindStatus data_bind_xml_format_reader_open_plan(
    const DataBindFormatPlan *plan, const char *data, size_t len,
    size_t max_depth, DataBindFormatReader *out_reader, DataBindError *error);

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_XML_PROVIDER_H */
