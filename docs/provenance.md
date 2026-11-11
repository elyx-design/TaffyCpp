# Provenance

TaffyCpp is an independent C++23 translation of
[DioxusLabs/taffy v0.15.1](https://github.com/DioxusLabs/taffy/tree/0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a),
pinned at commit:

```text
0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a
```

It is not a clean-room rewrite or the older inobelar C++ port. The standalone
repository was extracted from the C++ port's source commit:

```text
3c7c02ae8a9712505c9d719b978e37a86e3f7163
```

That revision was verified directly with `git rev-parse HEAD` during extraction.
The extraction retains the production headers/sources, CMake package support,
native C++ tests, XML fixtures and standalone C++ examples. It does not require
or ship the upstream Rust implementation, font/artwork assets, benchmark engines
or the porting toolchain.

## Attribution

The complete upstream MIT grant is retained in the root [LICENSE](../LICENSE),
including both owners:

- Copyright (c) 2018 Visly Inc.
- Copyright (c) 2026 Taffy Authors

No additional individual copyright owner is inferred. The upstream project
historically retained authors' copyrights while licensing their contributions
under MIT. Borrowed fixtures and helpers retain their own applicable terms;
see [third-party notices](third-party-notices.md). The root license does not
relicense all imported test material.

## Fixture identity

The XML corpus retains **6,604 files / 7,603,025 bytes**, with unchanged relative
paths and bytes from upstream `tests/xml/`. Its manifest SHA-256 is:

```text
5d545980d3a2fa2f23100e73a7a562cdde3459e149343b212067f005f7c84180
```

The [fixture README](../tests/fixtures/README.md) defines the manifest encoding.
Some fixture ancestry predates Taffy and includes WPT and Yoga material; exact
importer checkout revisions and complete per-file derivation remain qualified.

## Compatibility scope

The C++ source includes subsequent targeted correctness, ownership and scaling
corrections, not only a literal translation of the upstream pin. This extraction
does not add algorithm fixes or resolve deferred CSS behavior. See
[known issues](known-issues.md). Historical test results are source-bound, not
fresh verification of every future change or every platform.

### Post-port fix: capped intrinsic flex wrapping

This is an additive correctness fix, not part of the original upstream-to-C++
translation:

- Convert outer min/max sizes to content-space wrapping limits by subtracting
  padding, borders and scrollbar insets, while preserving NaN comparison semantics.
- Resolve capped max-content sizing before wrapping so the container retains its
  intrinsic outer size clamped by the maximum.
- Preserve whether the main size was originally known, so early intrinsic sizing
  still finalizes percentage gaps and does not reconstruct an outer size from a
  zero-clamped inner size.

Implementation: `src/flex/lines.cpp` and `src/compute_flex.cpp`. Regression tests
are in `tests/production_layout_regressions.cpp`, tagged `[wrap-insets]` and
`[intrinsic-finalization]`. These targeted tests do not establish general flexbox
or CSS conformance.
