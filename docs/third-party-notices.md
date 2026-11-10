# Third-party notices

## Library

TaffyCpp is a C++ translation of
[DioxusLabs/taffy v0.15.1](https://github.com/DioxusLabs/taffy/tree/0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a),
commit `0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a`.
The complete upstream MIT license is retained in [LICENSE](../LICENSE):
Copyright (c) 2018 Visly Inc.; Copyright (c) 2026 Taffy Authors.
The production library has no third-party dependency beyond the C++ standard
library. See [provenance](provenance.md) for the extraction revision and scope.

## Retained tests and fixtures

The root MIT license does not automatically relicense imported material. Preserve
existing source attributions and the [fixture license](../tests/fixtures/LICENSE)
alongside these notices when redistributing the retained tests/fixtures.

### Web Platform Tests (WPT)

Upstream fixture ancestry includes WPT material, historically offered under the
**W3C Test Suite License OR W3C 3-clause BSD License**, and later BSD-3-Clause.
The BSD alternative permits modifications; the restrictive Test Suite branch is
not the only available choice. Complete, verbatim historical notices are retained:

| Local notice | Upstream source |
| --- | --- |
| [W3C dual license](../notices/WPT-W3C-DUAL-LICENSE.txt) | [WPT at 1f0d8835](https://github.com/web-platform-tests/wpt/blob/1f0d8835edeba24238ee4ab1ca1079edf2449f03/LICENSE.md) |
| [BSD, 2019 copyright](../notices/WPT-BSD-2019.txt) | [WPT at b52d2076](https://github.com/web-platform-tests/wpt/blob/b52d2076a8681e4a77adef1c1fa9f061281ae083/LICENSE.md) |
| [BSD, undated copyright](../notices/WPT-BSD.txt) | [WPT at 9091c3cd](https://github.com/web-platform-tests/wpt/blob/9091c3cd988109592c4dc72038120f02810197cd/LICENSE.md) |

These are historical source notices, not a claim that every XML expectation has
an individually verified HTML derivation or that the exact importer checkout is
known. Prior investigation identified Microsoft, Gérard Talbot, Morten Stenshorne
and Opera source attributions; authorship metadata is not a new assertion of
copyright ownership. Preserve any file-specific credits present in retained files.

### Yoga / Stretch fixture ancestry

Historical Yoga-derived fixtures include Facebook **BSD plus PATENTS** material,
including 2017 cases by Lukas Wöhrl. Later Yoga comparison snapshots offer MIT.
Retain the historical notices rather than treating today's license as proof of
all earlier import terms:

- [Yoga BSD license](../notices/YOGA-BSD-LICENSE.txt) and
  [additional patent grant](../notices/YOGA-PATENTS.txt), from
  [Yoga commit e9927377](https://github.com/facebook/yoga/tree/e9927377b58a2bcac3615d79062ad6d81854bcdc).
- [Yoga MIT license](../notices/YOGA-MIT-LICENSE.txt), from
  [comparison commit 53a8842a](https://github.com/facebook/yoga/blob/53a8842abb3df5877b1b2c6ccf935561282ced68/LICENSE).
  This is a historical comparison, **not a proven importer revision**.

Actual importer checkouts and complete per-file mapping of import waves remain
unrecorded. Original Taffy contributions do not erase borrowed fixture obligations.

### Ported measurement helpers

Upstream `taffy_test_helpers` declares **MIT OR Apache-2.0**. For the retained
ported helper logic, the MIT alternative is supported by that declaration and
the contemporaneous project grant. The complete
[historical MIT notice](../notices/TAFFY-HELPERS-HISTORICAL-MIT.txt) is retained from
[the helper-introduction revision](https://github.com/DioxusLabs/taffy/blob/2a3e8476bac4bdc6b887080fcee6a89c7b627688/LICENSE.md).
No helper-specific copyright owner or Apache notice set is invented here.
The arithmetic measurement helper is not the Ahem font; no font is included.

## Test dependencies

These packages are used only for native tests, not by the installed library:

| Package | License | Upstream notice |
| --- | --- | --- |
| Catch2 (fallback 3.8.1) | BSL-1.0 | [LICENSE.txt](https://github.com/catchorg/Catch2/blob/v3.8.1/LICENSE.txt) |
| pugixml (fallback 1.15) | MIT | [LICENSE.md](https://github.com/zeux/pugixml/blob/v1.15/LICENSE.md) |
| nlohmann/json (fallback 3.11.3) | MIT, with embedded component notices | [LICENSE.MIT](https://github.com/nlohmann/json/blob/v3.11.3/LICENSE.MIT) |

They are supplied by parent targets, installed packages or optional pinned
fetches, and retain their own licenses. If redistributing their sources or
artifacts, preserve the notices applicable to the actual selected versions,
including embedded component notices (for example nlohmann/json's Abseil
Apache-2.0 attribution). The table is not a substitute for those package notices.
Catch2's BSL-1.0 includes an object-code exception; it does not require its notice
in every object-only distribution. See [building](building.md) for dependency control.

## Scope and qualifications

This small notice set covers retained fixture/helper history, not unrelated
fonts, artwork, benchmark code or development dependencies that are not shipped
here. No complete legal audit, rights-holder attestation, publishing approval or
blanket legal clearance is asserted. The remaining import-history qualifications
apply to the repository's test material; they are not additional production link
dependencies.
