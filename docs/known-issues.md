# Known issues

TaffyCpp is a layout library, not a browser or a CSS-conformance certification.
Passing the test corpus does not establish correctness for every CSS layout.

## Deferred layout behavior

The latest source assessment leaves these inherited issues explicitly deferred:

- **Floats:** shared-context cache hits can omit float effects, changing root
  height and clearance after sibling mutation. Further float placement and
  padding/border containment defects remain. These cases require actual floats.
- **Flexbox:** when min/max constraints conflict with padding/border floors,
  clamping can allocate too much space to siblings or misalign reversed boxes.
- **Grid:** collapsed `auto-fit` tracks with distributed alignment can produce
  inconsistent static/containing areas for absolutely positioned children.

This extraction does not fix those issues. Matching the pinned upstream
implementation does not make an inherited bug correct.

Earlier investigation also found shared cache/layout invariant failures after
child reorder/detachment and after changing a parent from flex to block: forcing
a dirty recomputation changed geometry in both implementations. Later production
cache and layout corrections do not establish that every such mutation sequence
is fixed. This extraction makes no new closure claim for those findings.

The port includes targeted corrections to inherited behavior (including some
float clearance, flex sizing and root layout cases); it is not intended to
reproduce every incorrect result of the upstream pin. Those corrections must not
be read as resolving all root/float, flex or grid issues.

## API boundaries

- The CSS parser handles supported values and declaration lists, not a complete
  stylesheet, selector engine or cascade. `calc()` needs an application resolver.
- There is no production JSON serializer/deserializer. JSON used by tests is not
  a public serialization API.
- Text shaping, rendering, interactive sticky scrolling and size containment are
  outside the library's API. Applications supply measurement callbacks.
- `Layout` has no persisted baseline getter; full callbacks use `LayoutOutput`.
- Captured measurement/context changes require explicit invalidation. Callback
  identity is not a substitute for dirtying affected nodes.
- Keep application trees acyclic with unique parents. Context destructors must
  not throw or mutate the owning tree. See the [API guide](api.md) for ownership,
  callback and error-handling rules.

## Verification and distribution

The source assessment records a passing default native 16-group suite, including
all 6,604 XML fixtures. XML geometry is mandatory; optional grid metadata and
scroll sizes follow upstream's conditional comparisons when both sides provide
values. A missing optional result is not fabricated by the runner.

Native Windows was verified locally with MSVC 19.44 and Clang 21 (see
[building](building.md)); it is not yet covered by CI, and MinGW is unverified.
Layout recursion is proportional to tree depth, so deep trees in unoptimized builds
can exhaust Windows' 1 MiB default thread stack. No stable binary ABI,
universal platform support, published release or blanket legal clearance is
claimed. Fixture import history retains qualifications described in
[third-party notices](third-party-notices.md).

See [building](building.md), [tests](../tests/README.md) and
[provenance](provenance.md) for the supported workflow and source boundary.
