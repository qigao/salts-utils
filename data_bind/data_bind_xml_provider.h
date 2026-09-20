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

#ifdef __cplusplus
}
#endif

#endif /* DATA_BIND_XML_PROVIDER_H */
