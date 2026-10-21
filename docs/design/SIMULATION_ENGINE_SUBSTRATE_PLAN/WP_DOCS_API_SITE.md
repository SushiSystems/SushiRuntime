# WP-DOCS: Fluent API documentation site

**Problem:** the C++ Fluent API is now the *only* frontend (Python abandoned, L15) and
the product's public face for an **industry** audience. A README is not enough; the
owner wants a Unity3D-scripting-API-style experience: a browsable, searchable
**API-reference website** with per-type / per-method pages, signatures, parameters,
return values, and runnable usage snippets.

**Design:**
- **Source of truth = the Doxygen comments already mandated** by `CONTRIBUTING.md`
  (every public function documents *why* + *how* + every `@param`/`@return`). The site
  is generated from those, so docs cannot drift from the headers.
- **Pipeline:** Doxygen (XML) → a static-site generator (e.g. Doxygen+doxysphinx,
  or Doxygen XML → mkdocs-material / Sphinx-breathe) producing a Unity-manual-style
  layout: a hand-written **manual / guides** section (getting started, the
  build→compile→execute model, State/Buffer, run/run_for/run_until, regions once WP-2
  lands) alongside the **auto-generated scripting reference**.
- **Surface scoped to the public fluent layer** (`api/`): `Runtime`, `API::Graph`,
  `Buffer`/`State`, `RunReport`, `Error`/`Errc`. Lower layers stay in `ARCHITECTURE.md`.
- **Kept honest by CI:** the docs build runs in CI; a missing/!-documented public
  symbol fails the build, so the site stays complete as the API grows (WP-1 late-binding
  overloads, WP-6 residency tags, etc. each add their pages when they land).

**Files:** a `docs/` site config (generator of choice), a Doxyfile scoped to
`modules/surface/api/include/SushiRuntime/api/`, CI job to build+publish (e.g. GitHub Pages).

**Risks:** low. Main cost is choosing the generator and writing the hand-authored
manual pages. It must track the API, so it is *not* a one-shot: every public-seam WP
(WP-1, WP-2, WP-6, WP-7) adds its doc pages as part of that WP's "done".

**Acceptance:** a published site where a user can browse from `Runtime::create()` to
each `add()` overload with signatures and a working snippet, plus a getting-started
manual; CI fails if a public fluent symbol is undocumented.
