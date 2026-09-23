# Third-Party Notices

SaltsUtils first-party code is licensed under the Apache License 2.0.
The components and data sets below retain their upstream license terms.

| Component | Repository path | Upstream license | Notes |
| --- | --- | --- | --- |
| CRoaring | `vendor/croar/` | Apache-2.0 OR MIT | License notices are embedded in the amalgamated source. |
| libecc | `crypto/vendor/libecc/` | BSD-2-Clause OR GPL-2.0-or-later | SaltsUtils selects the BSD license for redistribution. See `crypto/vendor/libecc/LICENSE`. |
| cyaml | `parser/cyaml/` | MIT | See `parser/cyaml/LICENSE`. |
| cxml | `parser/xml_parser/vendor/cxml/` | MIT | See `parser/xml_parser/vendor/cxml/LICENSE.txt`. |
| Monocypher | `databind/vendor/monocypher/` | BSD-2-Clause OR CC0-1.0 | License notices are embedded in the upstream source. |
| Unicode Character Database | `unicode/data/` and generated Unicode tables | Unicode-3.0 | See `unicode/data/LICENSE.txt`; generated tables retain the data-license attribution. |
| SQLite Lemon parser generator | `tools/lemon/` | Public-domain dedication | The source headers explicitly disclaim copyright. |

Dependencies downloaded by vcpkg or another package manager are not relicensed
by SaltsUtils and remain governed by their respective upstream licenses.
