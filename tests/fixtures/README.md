# XML layout fixtures

`xml/` contains **6,604 XML files / 7,603,025 bytes** retained from
[DioxusLabs/taffy v0.15.1](https://github.com/DioxusLabs/taffy/tree/0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a/tests/xml),
commit `0064bd54cbe38bf1d8bcd6ec6ab0704d45fb048a`, originally under `tests/xml/`.
Relative paths and file bytes are unchanged. No Rust sources or fixture-generation
tools are needed to run them: the native C++ runner discovers these files directly.

The SHA-256 of the sorted manifest is:

```text
5d545980d3a2fa2f23100e73a7a562cdde3459e149343b212067f005f7c84180
```

For each XML file, the manifest contains its path relative to `xml/`, a NUL byte,
the lowercase SHA-256 of its contents, and LF, sorted by relative path and encoded
as UTF-8. README and license files are not corpus members.

The complete upstream MIT notice is retained unchanged in [LICENSE](LICENSE),
including Visly Inc. and Taffy Authors. Preserve it when redistributing fixtures.
It does not supersede imported fixture terms: WPT BSD/W3C and historical Yoga
BSD/PATENTS notices and import-history qualifications are described in
[third-party notices](../../docs/third-party-notices.md).

See [tests](../README.md) for running the suite and
[provenance](../../docs/provenance.md) for the extraction boundary.
