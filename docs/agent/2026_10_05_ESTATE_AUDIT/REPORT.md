# Estate audit: sushiruntime

**Status:** Shipped — 2026-10-05. Read-only audit; nothing in the repository was changed.

One auditing agent per dimension read the tree and a second agent checked each finding against
the files. A finding marked *confirmed* was re-read by the reviewer; *unclear* means the reviewer
could not settle it; *found by review* means the first pass missed it and nobody re-checked it.
Refuted findings are listed and not counted. No build, test or project CLI was run.

| Dimension | High | Medium | Low | Refuted |
| --- | --- | --- | --- | --- |
| Licence | 4 | 10 | 6 | 0 |
| Command line | 2 | 13 | 9 | 0 |
| Documentation | 8 | 14 | 2 | 0 |
| Layout and hygiene | 6 | 9 | 5 | 0 |
| Code shape | 5 | 21 | 4 | 0 |

## Licence

### Facts

- Tracked files: 292 (git ls-files). 108 .cpp, 86 .hpp, 1 .h, 25 .py, 19 .cmake, 1 .cmake.in, 5 CMakeLists.txt, 29 .md.
- D:/Projects/sushiruntime/LICENSE (224 lines): line 2 reads 'SushiRuntime is under the Apache License v2.0 with LLVM Exceptions:', line 5 'Copyright (c) 2026-present Mustafa Garip & Sushi Systems', full Apache-2.0 text, then line 210 '---- LLVM Exceptions to the Apache 2.0 License ----' with the embedding and GPLv2 paragraphs. Added by commit 0597255 on 2026-09-19.
- The LLVM-exception variant is declared in exactly one place: LICENSE lines 2 and 210-224. 'git grep -i "LLVM.exception|with LLVM|LLVM Exceptions"' outside docs/slop returns only those two lines. No source header, README, CONTRIBUTING, pyproject or manifest mentions it.
- D:/Projects/sushiruntime/docs/LICENSE (201 lines): plain Apache-2.0, no LLVM exception (0 matches for 'llvm'), appendix filled as 'Copyright 2026-present Mustafa Garip & Sushi Systems'. It is the only licence file in the trees of tags v0.2.0 and v0.3.0.
- No NOTICE, COPYING, AUTHORS, PATENTS or THIRD_PARTY file is tracked. No third_party/ or dependencies/ folder is tracked. No top-level README.md exists; the front door is docs/README.md.
- C++ header variant A (full Apache-2.0 notice, 22-line box, no LLVM exception): 193 files (107 .cpp, 85 .hpp, 1 .h). Examples: src/core/version.cpp, include/SushiRuntime/SushiRuntime.h, include/SushiRuntime/interop/external_memory.hpp.
- C++ header variant B (truncated notice: 'Licensed under...', 'you may not use...', URL, no 'AS IS' paragraph): 2 files, include/SushiRuntime/core/bit.hpp and tests/common/test_barrier.hpp. No third variant exists.
- C++ files without a header: 0 of 195. The first-line file name in the box matches the basename in all 195. All 195 carry 'Copyright (c) 2026-present Mustafa Garip & Sushi Systems', project URL https://github.com/SushiSystems/SushiRuntime and https://sushisystems.io.
- Python: 25 tracked files, 0 with a licence header. Examples: cli/sushiruntime/cli.py, cli/sushiruntime/services/docker.py, tools/packaging/verify_installed_package.py.
- CMake: 19 .cmake, 1 .cmake.in and 5 CMakeLists.txt, 0 with a licence header. Examples: CMakeLists.txt, cmake/Sycl.cmake, cmake/SushiRuntimeConfig.cmake.in.
- No SPDX-License-Identifier line exists anywhere in the tracked tree.
- cli/pyproject.toml: name 'sushiruntime-cli', version '1.0.0', authors Mustafa Garip <mustafagarip@sushisystems.io>; no 'license' field and no classifiers at all.
- sushi-module.toml: name, alias, distribution, repo only; no licence field. cli/sushistack.deps.toml: no licence fields. Doxyfile: no licence text (PROJECT_NUMBER 'v1 (unreleased)'). Dockerfile: no LABEL lines. No package.json, conda file or vcpkg.json tracked.
- CMakeLists.txt: project(SushiRuntime VERSION 0.3.0); CPACK_PACKAGE_VENDOR 'SushiSystems' (line 123); no CPACK_RESOURCE_FILE_LICENSE and no install() rule for LICENSE. 'git grep -i LICENSE|NOTICE' over CMake, Dockerfile, pyproject and tools finds no install of a licence file.
- Prose licence statements: docs/README.md:582 'licensed under the Apache License, Version 2.0 - see the `[LICENSE](LICENSE)`'; docs/CONTRIBUTING.md:7-8 contributions 'licensed under the project's Apache License, Version 2.0 (see LICENSE)'; docs/CONTRIBUTING.md:151 'Every source file carries the Apache 2.0 license header'.
- Copyright holder and year: one form only, 'Mustafa Garip & Sushi Systems', '2026-present', in LICENSE, docs/LICENSE and 195 C++ headers. First commit is dated 2026-02-02.
- git shortlog -sne HEAD: 256 Mustafa Garip <mustafa.garip3434@hotmail.com>, 21 Mustafa Garip <51916290+sushimg@users.noreply.github.com>. One person under two addresses; no other human author. One commit has committer GitHub <noreply@github.com> (web merge).
- Commit trailers: 209 'Co-Authored-By: Claude ...' lines (Opus 5 1M 100, Opus 4.8 83, Sonnet 5 10, Sonnet 4.6 7, Fable 5 4, Opus 4.8 1M 4, Opus 5.5 1). No Signed-off-by lines and no CLA or DCO document.
- Third-party, none vendored. hwloc (BSD-3-Clause upstream): apt libhwloc-dev / vcpkg hwloc via cli/sushistack.deps.toml, found by pkg_check_modules in cmake/Dependencies.cmake:22; no notice kept in the repo.
- GoogleTest (BSD-3-Clause upstream): find_package(GTest CONFIG REQUIRED) at CMakeLists.txt:138, apt libgtest-dev in Dockerfile:10; tests only, not installed; no notice kept.
- SYCL toolchains, fetched not vendored: intel/llvm nightly bundle (Apache-2.0 WITH LLVM-exception upstream) at Dockerfile:37-38; AdaptiveCpp v24.10.0 (BSD-2-Clause upstream) git clone at Dockerfile:66; Intel oneAPI DPC++ (Intel EULA, proprietary) apt at Dockerfile:89 and intel-oneapi-runtime-opencl at Dockerfile:30. No notice kept. Upstream licences were not read from source in this pass.
- NVIDIA: Dockerfile:2 'FROM nvidia/cuda:12.4.1-devel-ubuntu22.04' and the [cuda] entry in cli/sushistack.deps.toml (CUDA toolkit, NVIDIA EULA, proprietary). Optional, gpu_only.
- Python dependencies in cli/pyproject.toml: typer>=0.12 (MIT), rich>=13.0 (MIT), tomli>=2.0 (MIT), sushicore>=0.7.0 (sibling Sushi Systems package). Ranges, not exact versions.
- cmake/SushiRuntimeInstall.cmake stages third-party runtime binaries under component 'sushiruntime_redist': hwloc-15, sycl*.dll, ur_loader, ur_adapter_opencl, ur_adapter_level_zero(_v2), ur_win_proxy_loader. Lines 13-15 state the redistribution licence question is open (BB-5d).
- Tags: v0.2.0 (2026-08-17, 8f190c1) and v0.3.0 (2026-09-15, 3218aa8). 'git ls-remote --tags origin' shows only v0.3.0 on the remote; v0.2.0 is local. 'gh release list' shows one release, 'SushiRuntime 0.3.0', published 2026-09-14T22:39:33Z.
- GitHub repository SushiSystems/SushiRuntime is PRIVATE ('gh repo view' returned visibility PRIVATE, licenseInfo 'Other').
- No PyPI publication of sushiruntime-cli is recorded in the tree: the only PyPI mentions concern taking sushicore from PyPI (docs/reference/CHANGELOG.md:9). .github/workflows/ci.yml has no publish or release job.
- Two changelogs are tracked: docs/CHANGELOG.md (Unreleased, 0.3.0 2026-09-15, 0.2.0 2026-08-17) and docs/reference/CHANGELOG.md (Unreleased only). Neither records the 2026-09-19 licence change.

### Corrections from review

- Variant A split by language is wrong: the audit says 107 .cpp, 85 .hpp, 1 .h. Recomputed: 108 .cpp, 84 .hpp, 1 .h (total 193 is right). Both variant B files are .hpp, so all 108 .cpp files carry the full header.
- F3 says the v0.3.0 tree carries '195-file Apache headers'. 'git ls-tree -r v0.3.0' lists 169 .cpp/.hpp/.h files (v0.2.0: 161). 195 is the count at HEAD.
- The copyright line in docs/LICENSE is line 189, not 190 as cited in F12.
- F15 cites Dockerfile:17 'kitware-archive-latest.asc' as an unpinned fetch. That file is the Kitware apt signing key; the unpinned package is cmake at Dockerfile:21.
- The not_checked items on the 0.3.0 release and PyPI can be closed: 'gh release view v0.3.0' returns assets: [], forkCount is 0, the only collaborator is sushimg, and https://pypi.org/pypi/sushiruntime-cli/json returns 404.
- Recomputed and correct: 292 tracked files; 108/86/1/25/19/1/5/29 by type; 0 headers in Python and CMake; LLVM wording only at LICENSE:2 and :210; shortlog 256 + 21; 209 co-author trailers; 277 commits; first commit 2026-02-02; tag dates and hashes.

### Findings

#### L1. [high] Two tracked licence files state different licences

Evidence: D:/Projects/sushiruntime/LICENSE:2 'Apache License v2.0 with LLVM Exceptions' and LICENSE:210-224 (exception text); D:/Projects/sushiruntime/docs/LICENSE is plain Apache-2.0 with 0 occurrences of 'llvm'. Commit 0597255 (2026-09-19) added the root file and left docs/LICENSE in place.

Recommendation: Keep one licence file at the repository root and delete docs/LICENSE in the relicensing change, so the new licence text has a single home.

Review: confirmed. LICENSE:2 and :210-224 carry the LLVM exception; docs/LICENSE (201 lines) has 0 'llvm' matches. Deleting docs/LICENSE breaks the relative links at docs/README.md:582 and docs/CONTRIBUTING.md:8 unless they are rewritten in the same change, and deletion needs the owner's approval.

#### L2. [high] Every prose and header statement says plain Apache-2.0 while LICENSE says Apache-2.0 WITH LLVM-exception

Evidence: docs/README.md:582 and docs/CONTRIBUTING.md:7-8 and :151 name 'Apache License, Version 2.0'; all 195 C++ headers carry the plain Apache notice (e.g. src/core/version.cpp:11). The exception is declared only at LICENSE:2 and LICENSE:210. The README and CONTRIBUTING links '`[LICENSE](LICENSE)`' resolve to docs/LICENSE, the file without the exception.

Recommendation: The relicensing change rewrites all of these to the new licence in one commit; until then, treat LICENSE at the root as the authoritative statement and record that the siblings differ.

Review: confirmed. docs/README.md:582, docs/CONTRIBUTING.md:7-8 and :151 say plain Apache-2.0; git grep for the LLVM wording over the whole tree returns only LICENSE:2 and LICENSE:210; both '`[LICENSE](LICENSE)`' links sit under docs/ and resolve to docs/LICENSE.

#### L3. [high] v0.3.0 is tagged, pushed and released under Apache-2.0

Evidence: 'git ls-remote --tags origin' returns refs/tags/v0.3.0 (3218aa8); 'gh release list' returns 'SushiRuntime 0.3.0 Latest v0.3.0 2026-09-14'. The v0.3.0 tree carries docs/LICENSE (plain Apache-2.0, no LLVM exception) and 195-file Apache headers. docs/CHANGELOG.md:42 '## [0.3.0] — 2026-09-15'.

Recommendation: State in the relicensing note that 0.3.0 and earlier stay Apache-2.0 for anyone who received them. The repository is PRIVATE, so confirm with the owner who was given access or the release archive; that list is the whole set of Apache-2.0 recipients.

Review: confirmed. Tag v0.3.0 is on origin and 'gh release list' shows it; its tree has docs/LICENSE only. But the tree at v0.3.0 holds 169 C++ files, not 195, and 'high' overstates it: the release has 0 assets, the repo is PRIVATE with 0 forks and one collaborator (sushimg), so no outside recipient is recorded.

#### L4. [high] Redistribution of third-party runtime binaries is unlicensed and the gap is already acknowledged in source

Evidence: cmake/SushiRuntimeInstall.cmake:13-15: 'The SYCL toolchain bundle ships no licence or redistribution file, so that question is open'. The same file stages hwloc-15, sycl*.dll and five Unified Runtime libraries (lines 30-58) under component 'sushiruntime_redist'. docs/CHANGELOG.md:143-145 repeats 'Redistribution licensing is untouched and stays BB-5d's gate'. No NOTICE or third-party notice file is tracked.

Recommendation: Before any binary archive leaves the machine, add a third-party notices file covering hwloc (BSD-3-Clause) and the intel/llvm SYCL runtime and UR adapters (Apache-2.0 WITH LLVM-exception), read from each upstream source, and install it with the redist component.

Review: confirmed. cmake/SushiRuntimeInstall.cmake:10-15 and :30-58 read; docs/CHANGELOG.md:144-145 carries the BB-5d sentence; no NOTICE or third-party file is tracked.

#### L5. [medium] The Apache-2.0 WITH LLVM-exception state was never released

Evidence: LICENSE was added by 0597255 on 2026-09-19, four days after tag v0.3.0 (2026-09-15). 'git ls-tree -r v0.3.0' and v0.2.0 list docs/LICENSE only. Neither docs/CHANGELOG.md nor docs/reference/CHANGELOG.md has an entry for the licence change.

Recommendation: Record the outbound history as it is: released versions were plain Apache-2.0; the LLVM-exception text exists only on main after 2026-09-19. Add the missing changelog line when the new licence lands.

Review: confirmed. git log -- LICENSE shows 0597255 on 2026-09-19; tags v0.2.0 and v0.3.0 list docs/LICENSE only; neither changelog mentions the licence change.

#### L6. [medium] 50 source files carry no licence header

Evidence: 25 of 25 Python files (cli/sushiruntime/cli.py, cli/sushiruntime/services/docker.py, tools/packaging/verify_installed_package.py), 19 of 19 .cmake files (cmake/Sycl.cmake, cmake/Dependencies.cmake, cmake/gpu/Cuda.cmake), cmake/SushiRuntimeConfig.cmake.in, and 5 CMakeLists.txt (CMakeLists.txt, src/CMakeLists.txt, tests/package/CMakeLists.txt). The source-comments skill requires the licence block before the module docstring in Python.

Recommendation: Add the new licence header to the 25 Python files in the relicensing pass. Ask the owner whether CMake files take a header; cmake/SushiRuntimeConfig.cmake.in is installed into consumer prefixes, so it is the one CMake file that most needs one.

Review: confirmed. Grep for copyright/Licensed under/SPDX over all 25 .py, 19 .cmake, 1 .cmake.in and 5 CMakeLists.txt returned nothing; 50 is right. source-comments puts the licence block before the Python module docstring.

#### L7. [medium] The Python package declares no licence

Evidence: cli/pyproject.toml has no 'license' key and no 'classifiers'; authors is the only attribution. The package sets [tool.setuptools.package-data] but ships no licence file, and the Dockerfile installs it with pip (Dockerfile:121).

Recommendation: Add the licence expression and licence-file entry for the new licence when it is chosen. A non-commercial licence has no OSI classifier; use 'License :: Other/Proprietary License' or a LicenseRef expression.

Review: confirmed. cli/pyproject.toml read in full: no license key, no classifiers; Dockerfile:121 pip-installs it. PyPI returns 404 for sushiruntime-cli, so nothing is published.

#### L8. [medium] Installed and archived packages ship without any licence file

Evidence: CMakeLists.txt has install(FILES ...) for the package config only (line 107) and CPack settings at 122-131 with no CPACK_RESOURCE_FILE_LICENSE; 'git grep -i LICENSE|NOTICE' over *.cmake and CMakeLists.txt returns no install rule. Headers under include/ are installed with their per-file notice only.

Recommendation: Under a non-commercial licence the terms must travel with the SDK archive. Add an install rule for LICENSE to the default component; this is a build-configuration edit and needs the owner's go-ahead.

Review: confirmed. CMakeLists.txt:107 and 122-133, src/CMakeLists.txt:165-177 and cmake/SushiRuntimeInstall.cmake:125,149 are the only install rules; none names LICENSE. The recommendation is one install rule in the default component, which is brick-shaped.

#### L9. [medium] Proprietary vendor SDKs sit in the dependency set

Evidence: Dockerfile:2 'FROM nvidia/cuda:12.4.1-devel-ubuntu22.04' (NVIDIA EULA); Dockerfile:30 intel-oneapi-runtime-opencl and Dockerfile:89 intel-oneapi-compiler-dpcpp-cpp (Intel EULA); cli/sushistack.deps.toml [oneapi] is 'required = true' as one of three any-of SYCL toolchains, [cuda] is optional. The dependencies skill rejects vendor SDK licences that restrict redistribution.

Recommendation: These are build and run prerequisites the user installs, not code the repository redistributes, so they do not block a non-commercial outbound licence. Do not publish a built Docker image or stage oneAPI or CUDA libraries in the redist component without reading both EULAs; record the exception to the skill's table with the owner's approval.

Review: confirmed. Dockerfile:2, :30 and :89 and cli/sushistack.deps.toml [oneapi] required = true, [cuda] required = false read. The dependencies skill table does reject vendor SDK licences that restrict redistribution.

#### L10. [medium] Copyright holder is a two-party phrase with no stated owner

Evidence: 'Copyright (c) 2026-present Mustafa Garip & Sushi Systems' in LICENSE:5, docs/LICENSE:190 and 195 C++ headers. CPACK_PACKAGE_VENDOR is 'SushiSystems' (CMakeLists.txt:123), written without the space. Nothing in the tree says whether Sushi Systems is a legal entity or which of the two holds the rights.

Recommendation: A licence that reserves commercial use needs one named licensor who can grant commercial licences. Ask the owner to fix the holder line (person, company, or both with an assignment) before the header rewrite, and use the same spelling in CPack.

Review: confirmed. Holder phrase found in LICENSE:5, docs/LICENSE:189 (the audit says 190) and once in each of 195 C++ files; CMakeLists.txt:123 reads 'SushiSystems'.

#### L11. [medium] The repository has no header checker, so the header rewrite cannot be verified mechanically

Evidence: git ls-files tools lists one file, D:/Projects/sushiruntime/tools/packaging/verify_installed_package.py. tools/documentation/check_source_comments.py, which the source-comments skill names as the checker every repository carries, is absent, and .github/workflows/ci.yml has no licence-header step. The two truncated headers (include/SushiRuntime/core/bit.hpp, tests/common/test_barrier.hpp) are the drift that results.

Recommendation: Land the checker before the rewrite, with the header text held in one place it reads, so the 195 C++ files and the 25 Python files are checked against a single template instead of a one-off script. Adding tools/ is a layout decision for the owner.

Review: found by review.

#### L12. [medium] The Sushi skills themselves fix Apache-2.0 and reject non-commercial licences

Evidence: C:/Users/sushi/.claude/skills/source-comments/SKILL.md gives the header template as '... Apache-2.0 notice ...', and C:/Users/sushi/.claude/skills/dependencies/SKILL.md rejects 'Non-commercial, source-available, field-of-use limits'. D:/Projects/sushiruntime/cli/pyproject.toml:14 depends on sushicore>=0.7.0, a sibling that will carry the new licence, and docs/CONTRIBUTING.md:151 tells contributors to copy the Apache header.

Recommendation: Change the two skills in the same programme: the header template names the new licence, and the dependency table gains an explicit row for Sushi Systems' own packages. Otherwise the first audit after the change flags sushicore as a rejected dependency.

Review: found by review.

#### L13. [medium] The audit treats authorship as settled although 209 of 277 commits are machine co-authored

Evidence: git log in D:/Projects/sushiruntime shows 209 distinct commits with 'Co-Authored-By: Claude ...' trailers. F14 concludes that no consent is needed and stops there. A non-commercial licence is enforced through copyright, and whether machine-generated portions are protected is not settled law.

Recommendation: Put this to the owner as an open legal question in the relicensing record instead of closing it: the right to relicense is not in doubt, the strength of enforcement against a commercial user may be. It needs a lawyer, not a code change.

Review: found by review.

#### L14. [medium] Whether the repository was ever public is not established

Evidence: Commit c6bfc25 (2026-06-16) is titled 'chore: prepare repository for open-source release' and added the Apache-2.0 LICENSE; the GitHub repository was created 2026-01-12 and is PRIVATE today. The audit records current visibility only. Any period of public visibility would have granted Apache-2.0 to everyone who cloned in that window.

Recommendation: Ask the owner whether SushiSystems/SushiRuntime was public at any point after 2026-06-16. If it never was, F3 drops to low: no assets, no forks, one collaborator.

Review: found by review.

#### L15. [low] Two C++ files carry a truncated licence header

Evidence: include/SushiRuntime/core/bit.hpp:11-14 and tests/common/test_barrier.hpp:11-14 keep 'Licensed under...', 'you may not use...' and the URL, and drop 'You may obtain a copy' and the 'AS IS' paragraph that the other 193 files carry.

Recommendation: Replace both with the one new header block so all 195 files share a single variant; a header-rewrite script must match both shapes or it will skip these two.

Review: confirmed. include/SushiRuntime/core/bit.hpp:11-14 read; classification of all 195 files gives exactly 2 truncated headers, both .hpp.

#### L16. [low] No GPL or LGPL dependency found; the LLVM exception's GPLv2 clause disappears with the change

Evidence: Third-party set is hwloc, GoogleTest, intel/llvm, AdaptiveCpp, typer, rich, tomli, sushicore (cli/sushistack.deps.toml, cli/pyproject.toml, CMakeLists.txt:138). LICENSE:218-224 grants a waiver for combination with GPLv2 software; LICENSE:212-215 exempts embedded object code from sections 4(a), 4(b), 4(d).

Recommendation: Tell the owner what is lost: header-template kernels are instantiated in consumer code (docs/CHANGELOG.md:197, :228), so the embedding exemption matters here. The new licence needs its own wording for object code that embeds SushiRuntime templates, or every consumer binary carries the non-commercial terms and notice duty.

Review: confirmed. LICENSE:212-224 read; docs/CHANGELOG.md:197 and :228 do state that kernels are header templates instantiated in the consumer's translation units. This is an owner decision more than a defect, and it is the most consequential legal point in the audit, so 'low' undersells it.

#### L17. [low] CONTRIBUTING fixes the inbound licence to Apache-2.0

Evidence: docs/CONTRIBUTING.md:7-8 'By contributing you agree that your contributions are licensed under the project's Apache License, Version 2.0'; docs/CONTRIBUTING.md:151 tells contributors to copy the Apache header verbatim. No CLA or DCO exists. All 277 commits are by Mustafa Garip under two addresses, so no outside contribution exists today.

Recommendation: Rewrite the inbound clause with the new licence so that future contributions grant the owner the right to license commercially; without that, the first outside patch would block later commercial licensing of the file it touches.

Review: confirmed. docs/CONTRIBUTING.md:7-8 and :151 read; git rev-list --count HEAD is 277; shortlog shows one person under two addresses; no Signed-off-by lines.

#### L18. [low] 209 commits carry AI co-author trailers and no human co-author

Evidence: 'git log --format=%b | grep -i co-authored-by' counts 209 lines, all 'Claude ... <noreply@anthropic.com>'. git shortlog -sne lists only Mustafa Garip (256 + 21).

Recommendation: No contributor consent is needed to relicense: the sole human author is the owner. Note this in the relicensing record so the question is closed in writing.

Review: confirmed. 209 distinct commits carry a Co-Authored-By: Claude trailer, breakdown matches. The recommendation closes the question too firmly: it does not say that copyright in machine-generated portions is unsettled, which matters for a licence enforced through copyright.

#### L19. [low] Python dependencies are declared as ranges and Docker toolchains partly unpinned

Evidence: cli/pyproject.toml:11-15 'typer>=0.12', 'rich>=13.0', 'sushicore>=0.7.0', 'tomli>=2.0'. The dependencies skill requires an exact version, 'No ranges, no "latest"'. Dockerfile:17 fetches 'kitware-archive-latest.asc' and installs cmake unpinned.

Recommendation: A licence audit holds only for the versions audited. Pin the versions, or record in the third-party notices which versions the licence reading covers.

Review: confirmed. cli/pyproject.toml:11-15 are ranges. Dockerfile:17 is a signing-key URL, not a version; the unpinned install is 'apt-get install -y cmake' at Dockerfile:21. cli/sushistack.deps.toml also carries no version for hwloc, which the audit does not mention.

#### L20. [low] docs/README.md is the only README and there is no root front door carrying the licence statement

Evidence: 'ls README*' at D:/Projects/sushiruntime fails; the licence section lives at docs/README.md:580-582. GitHub reports licenseInfo 'Other' for the repository.

Recommendation: When the documentation skeleton is rebuilt, put the licence statement in the root README.md and link the root LICENSE from it.

Review: confirmed. No README* at the root; licence section at docs/README.md:580-582; gh reports licenseInfo 'Other'. Overlaps the documentation dimension.

### Not checked

- Upstream licence texts of hwloc, GoogleTest, intel/llvm, AdaptiveCpp, typer, rich and tomli were not read from source; the licences named are from general knowledge and need confirming against each upstream at the pinned version.
- sushicore's own licence and its PyPI metadata (it is a sibling repository's dimension).
- Transitive Python dependencies (click, markdown-it-py, pygments, shellingham and so on) and their licences.
- The Intel oneAPI and NVIDIA CUDA EULA terms on redistribution and on container images.
- Untracked directories: build/, build-asan/, venv/, .scratch_syntax/, .superpowers/ and the four untracked CXX-DetectStdlib-*.h files at the repository root.
- docs/slop/ content was excluded from the licence-mention search, apart from the BB-5d lines in ENGINE_BACKBONE_REFACTOR.md.
- Whether the 0.3.0 GitHub release carries binary assets, and who has access to the private repository.
- Whether sushiruntime-cli was ever uploaded to PyPI; only the tree was searched, the index was not queried.
- Markdown, YAML, TOML and JSON files were not checked for licence headers; examples/ sources were counted with the C++ files but not read individually.
- Git history before HEAD was not scanned for removed vendored code or for earlier header variants.

## Command line

### Facts

- Entry points: cli/pyproject.toml:18-20 declares two console scripts, `sr` and `sushiruntime`, both bound to `sushiruntime.cli:app`; cli/sushiruntime/__main__.py adds `python -m sushiruntime`. Distribution name `sushiruntime-cli`, version 1.0.0, requires-python >=3.10.
- Framework: Typer (`typer>=0.12`) with `rich>=13.0`; `add_completion=False`, `no_args_is_help=True`, `rich_markup_mode="rich"` (cli/sushiruntime/cli.py:33-40).
- sushicore requirement: `sushicore>=0.7.0` (cli/pyproject.toml:14); the sibling checkout D:/Projects/sushicore/pyproject.toml is at 0.7.0, and sa, sb, sd, se, st and hub pin the same floor.
- Command tree, locally defined: `toolchain [NAME]`; `build --type/-t {release,debug,relwithdebinfo,asan} --distributed --no-cuda --clean --force --no-test`; `test --suite/-s {unit,regression,integration,functional,stress,performance,benchmark,package,all} --distributed --filter/-f --repeat/-r --asan`; `run [TARGET] --sort --distributed ROLE --asan -- ARGS`; `package --prefix --no-archive`; `clean`; `doxygen`; `docker build --no-cache --oneapi`; `docker run --admin --no-gpu`; `config`; `env --all --asan`.
- Command tree, from sushicore (`register_provision_commands`, cli.py:283-290): `setup --dry-run --yes --toolchain (repeatable) --no-gpu`; `doctor --for GROUP`; `link --workspace`; `unlink --workspace`. Registered with `uses_base=True` under the Diagnostics panel.
- Help panels: Toolchain (toolchain), Project (build, test, run, package, clean, doxygen), Diagnostics (config, env, setup, doctor, link, unlink), Container (docker group). Constants K_TOOLCHAIN/K_PROJECT/K_DIAGNOSTICS/K_CONTAINER at cli.py:26-29.
- Help rendering: `sushicore.typer_help.help_group(console.current)` replaces Typer's help with a sushicore HelpPage for the root, the docker group and every child; examples are passed as each command's `epilog`. The logo shows only on a UTF-8 terminal with 256 colours or more and colour enabled.
- Taken from sushicore: LazyConsole (console.py), ModuleConfig/ModuleProfile/ProvisionSettings/write_tool_section (config.py), build_env helpers (env.py), CMakeDriver, Runner and cmake_cache (services/project.py), Diagnostics (services/diag.py), ExecutableIndex (services/discovery.py), provision.probe (services/select.py).
- Implemented locally: toolchain selection and its table (services/select.py), build policy, ASan flags and configure stamp (services/project.py), install plus CPack (services/packaging.py), docker build/run argv (services/docker.py), Linux oneAPI setvars snapshot and toolchain PATH injection (env.py), the legacy `.tools` config-dir fallback (config.py:49-63).
- Output: human-readable Rich output only, through the sushicore console (header, info, success, warn, error, command echo, Rich tables). `sr` exposes no `--json`, no `--describe` and never sets `LazyConsole.machine`; sushicore has a JSON renderer that `hub` uses (`hub --json`, `hub --describe`).
- Version flag: none. cli.py has no `@app.callback` and no `--version`; `se` has `--version` (sushiengine/cli/sushiengine/cli.py:111-115).
- Exit codes: every local command ends in `raise typer.Exit(<service return>)`. Services return 0, 1 for their own failures, or the child process's code (cmake, ctest, docker, the run target). sushicore's setup/doctor/link return 2 for an unusable request; Typer returns 2 for bad arguments; a missing executable returns 1 (Runner default).
- Error reporting: services print through `console.error` with a multi-line `  - ` hint list and return non-zero. Outside a checkout `find_project_root` raises `SystemExit(<message>)`, which Python prints as a bare unstyled line with exit 1.
- Colour: no flag in `sr`. sushicore resolves `[cli] color = auto|always|never` from cli/config.toml and config.local.toml, then `SUSHI_CLI_COLOR`, and `NO_COLOR` wins when set (sushicore/sushicore/config.py:103-116). There is no `--no-color` option.
- Help-string register: imperative one-line docstrings ending in a period ("Configure and build the project."), option help as full sentences with a period; the root help uses an em dash ("SushiRuntime developer CLI — build, test, run, and Docker management."). `test` and `package` carry multi-paragraph docstrings.
- CLI documentation lives in docs/CLI_GUIDE.md and docs/README.md (sections Building, Docker, Other CLI commands). The repository has no root README.md and no docs/guides/ directory.
- Tracked CLI tests: cli/tests/ holds 10 files (config, console laziness, docker, GPU backend, help screen, provision commands, thread sanitizer, toolchain selection). They were read by name only.

### Corrections from review

- Tracked file count: the audit's verification_output says `git ls-files cli` lists 27 files (13 package files, 10 tests). The command returns 28: 3 top-level files, 14 package files (py.typed included) and 11 files under cli/tests (conftest.py plus 10 test_*.py). The conclusion does not change.
- Tracked CLI tests fact: the topic list (config, console laziness, docker, GPU backend, help screen, provision commands, thread sanitizer, toolchain selection) leaves out that GPU backend is three files (test_gpu_backend_delegation.py, test_gpu_backend_resolution.py, test_gpu_probe_paths.py) and omits conftest.py.
- F14 lists two unused imports in cli.py (`sys`, `List`). After the recommended removal of DockerAction, `import enum` at cli.py:9 is unused as well, so the count is three.
- F8 says sr's services/docker.py and st's equivalent 'are the same two argv builders'. They differ: sushitrack/cli/sushitrack_cli/services/docker.py:14 takes the image from cfg.docker_image, sr hard-codes IMAGE = 'sushiruntime' (services/docker.py:10), and sushiengine/cli/sushiengine/services/docker.py:33 passes -f .config/docker/Dockerfile.
- Recomputed and correct: sushicore>=0.7.0 in all seven cli/pyproject.toml files and sushicore/pyproject.toml:7 at 0.7.0; the command tree and option lists for all eleven local commands and the four sushicore commands; the four help panels; no root README.md and no docs/guides/.

### Findings

#### C1. [high] `sr package --prefix` deletes any existing directory it is pointed at, with no guard and no confirmation

Evidence: cli/sushiruntime/services/packaging.py:54 resolves the prefix (`Path(cfg.expand(prefix)).resolve()`), and lines 60-62 run `shutil.rmtree(stage, ignore_errors=True)` whenever it is a directory. `sr package --prefix .`, `--prefix ~` or `--prefix /opt` removes that tree. docs/CLI_GUIDE.md:255 shows `sr package --prefix /opt/staging` as the example and line 259 states the wipe as a feature. `ignore_errors=True` also hides a partial delete.

Recommendation: Refuse a prefix that is the project root, an ancestor of it, the home directory or a filesystem root, and refuse a non-empty directory that the CLI did not create (a marker file in the stage tree) unless a `--force`/`--yes` is given. This is a behaviour change on a public command, so it needs the owner's decision.

Review: confirmed. cli/sushiruntime/services/packaging.py:54-62 resolves any --prefix and rmtree's it with ignore_errors=True; docs/CLI_GUIDE.md:255 and 259 match. Extra case the audit did not name: `--prefix build` deletes the build tree after the CMakeCache check at line 50 has passed. Recommendation is a guard in one place and correctly defers the behaviour change to the owner.

#### C2. [high] The project root is the nearest directory with any CMakeLists.txt, so sr run from tests/ or examples/ treats that subdirectory as the checkout

Evidence: cli/sushiruntime/config.py:33 sets root_marker='CMakeLists.txt'. sushicore/sushicore/module_config.py:73 walks up with has_marker(*markers()) and sushicore/sushicore/workspace.py:56-60 returns the nearest match. D:/Projects/sushiruntime/tests/CMakeLists.txt, examples/CMakeLists.txt and tests/package/CMakeLists.txt all exist. From tests/, `sr build` would configure tests/ as the top-level project into tests/build (services/project.py:451-456, 515), `sr clean` would report nothing to clean, and config_dir falls through to tests/cli (config.py:63), so config.local.toml is silently not read. Read from source, not run.

Recommendation: Give ModuleProfile a marker only the repository root carries (sushi-module.toml exists at the root of this checkout) and keep CMakeLists.txt out of the walk. The marker lives in sushicore and every module CLI inherits it, so the choice is the owner's.

Review: found by review.

#### C3. [medium] `hub install-cli` is documented with an option and a behaviour it does not have

Evidence: docs/CLI_GUIDE.md:27 and docs/README.md:78 say `hub install-cli sushiruntime      # add --no-editable for a non-dev install`. sushistack/cli/sushihub/cli.py:223-239 defines only `--dry-run` and its docstring says "Always installed editable". docs/CLI_GUIDE.md:30-31 says "On Windows this installs through pipx; on Linux and macOS it installs with pip into your current environment", while the same hub docstring says "into an isolated pipx venv" on every platform.

Recommendation: Remove the `--no-editable` remark from both files and rewrite CLI_GUIDE lines 30-31 to say pipx on every platform.

Review: confirmed. docs/CLI_GUIDE.md:27,30-31 and docs/README.md:78 read as quoted; sushistack/cli/sushihub/cli.py:223-239 has only --dry-run and says 'Always installed editable'; services/cli_install.py:108 calls pipx with editable=True on every platform. docs/README.md:75 already says 'isolated pipx venv', so the two documents also contradict each other.

#### C4. [medium] CLI_GUIDE and README describe `--no-cuda` as Linux-only; the code applies it on every platform

Evidence: docs/CLI_GUIDE.md:116 `sr build --no-cuda        # Linux only: turn the SYCL CUDA backend off` and docs/README.md:101 `# disable CUDA backend (Linux)`. cli/sushiruntime/services/project.py:481-486 sets `gpu_backend = "cpu"` for `no_cuda` before the platform branch, and the comment at 481-482 says "--no-cuda now means something on Windows rather than printing that it does not". The option help at cli.py:94-96 has no platform qualifier.

Recommendation: Drop the Linux qualifier in both documents and describe the flag as the option help does: build the portable CPU/OpenCL path instead of detecting accelerators.

Review: confirmed. CLI_GUIDE.md:116 and README.md:101 carry the Linux qualifier; services/project.py:483-486 sets gpu_backend='cpu' before the platform branch and the comment at 481-482 says so; cli.py:94-96 help has no qualifier.

#### C5. [medium] CLI_GUIDE names the MSVC annotation macros the code stopped using

Evidence: docs/CLI_GUIDE.md:144-146 says MSVC's STL container annotations are disabled with `_DISABLE_STRING_ANNOTATION` / `_DISABLE_VECTOR_ANNOTATION`. cli/sushiruntime/services/project.py:76-78 passes `-D_DISABLE_STL_ANNOTATION=1`, and lines 49-55 explain that the per-container macros went stale. The guide also omits `-fsanitize-address-use-after-return=never`, which the same constant passes.

Recommendation: Update the guide's two bullets to name `_DISABLE_STL_ANNOTATION` and add the use-after-return flag.

Review: confirmed. CLI_GUIDE.md:144-145 names the two per-container macros; services/project.py:76-78 passes -D_DISABLE_STL_ANNOTATION=1 and -fsanitize-address-use-after-return=never. Line 148 of the guide ('inside std::string and std::vector') is stale for the same reason and should be fixed with it.

#### C6. [medium] `sr test --distributed` is a dead option

Evidence: cli/sushiruntime/cli.py:123-124 declares `--distributed` with help "Run against a distributed-enabled build." cli/sushiruntime/services/project.py:546-547 only prints `Note: distributed tests only exist if built with --distributed.`; the value reaches neither the build directory choice nor the ctest arguments (lines 549-559). docs/CLI_GUIDE.md:189 documents it as selecting a build.

Recommendation: Remove the option, or make it do what the help says (for example fail when the tree was not configured with SR_ENABLE_DISTRIBUTED=ON). Removing a public option is the owner's call.

Review: confirmed. cli.py:123-124 declares the flag; services/project.py:546-547 only prints a note; the value is not passed to ctest_run at 556-559. CLI_GUIDE.md:189 documents it as selecting a build.

#### C7. [medium] `--distributed` has two shapes inside one CLI, and `run` accepts an invalid role and carries on

Evidence: cli.py:92-93 and 123-124: `--distributed` is a boolean on `build` and `test`. cli.py:157-159: on `run` it takes a `ROLE` string. cli.py:164-166 turns any value other than master/worker into a warning (`Unknown --distributed role ...; not forwarding.`) and still launches the program with exit code from the child, so a typo runs the wrong thing and reports success.

Recommendation: Type the role as an enum so Typer rejects a bad value with exit 2, and give the run option its own name (for example `--role`) so the flag means one thing across `sr`.

Review: confirmed. cli.py:92-93, 123-124 (bool) versus 157-159 (ROLE string); 164-166 warn and still launch. CLI_GUIDE.md:231-232 documents the warn-and-continue behaviour, so this is a design defect and not a doc drift. An enum is a brick-shaped fix; renaming to --role changes a public option and needs the hidden-alias step the audit applied in F7 but did not repeat here.

#### C8. [medium] The interactive-pick flag is spelled three ways across the sibling CLIs

Evidence: sushiruntime/cli/sushiruntime/cli.py:155-156 `--sort` ("Interactively pick an executable."); sushiai/cli/sushiai/cli.py:114 and sushiblas/cli/sushiblas/cli.py:116 also `--sort`; sushiengine/cli/sushiengine/cli.py:251-255 uses `--pick` and keeps `--sort` as a hidden, announced alias ("The old spelling of --pick."); sushitrack/cli/sushitrack_cli/cli.py:151 uses `--select/-s`. The shared helper still names its parameter `sort` (sushicore/sushicore/discovery.py:139-154).

Recommendation: Settle one spelling for the stack (se already moved to `--pick`), rename it in sr and keep `--sort` as a hidden alias for one release, following the api-stability rule.

Review: confirmed. sr cli.py:155-156, sa cli.py:114, sb cli.py:116 use --sort; se cli.py:251-255 uses --pick with a hidden --sort; st cli.py:151 uses --select/-s; sushicore/discovery.py:139-140 names the parameter `sort`.

#### C9. [medium] Container commands are a `docker` group here and a `container` group in se

Evidence: sushiruntime/cli/sushiruntime/cli.py:42-48 registers `docker` (build, run) under the Container panel. sushiengine/cli/sushiengine/cli.py:136 registers `container` under Environment and line 145 keeps `docker` as a hidden alias. sushitrack/cli/sushitrack_cli/cli.py:187 registers `docker` under the Project panel. Three CLIs, two names, three panels.

Recommendation: Choose one group name and one panel for the stack and take the build/run pair from a shared sushicore brick; sr's services/docker.py and st's equivalent are the same two argv builders.

Review: confirmed. sr cli.py:42-48 (docker, Container panel), se cli.py:136 (container, Environment) and 145 (hidden docker alias), st cli.py:187 (docker, Project). The claim that sr's and st's docker services are 'the same two argv builders' is overstated: st reads the image from cfg.docker_image, sr hard-codes IMAGE, se passes -f .config/docker/Dockerfile. A shared brick would need image, Dockerfile path and mount as parameters, and a new sushicore surface is an owner decision.

#### C10. [medium] `sr` has no `--version`, `--json` or `--describe`, which siblings already offer

Evidence: cli/sushiruntime/cli.py has no `@app.callback` (grep for `--version|--describe|--json|@app.callback` over all sibling cli.py files matched only sushiengine/cli/sushiengine/cli.py:111-115 for `--version` and sushistack/cli/sushihub/cli.py:50-57 for `--json` and `--describe`). The package version 1.0.0 in cli/pyproject.toml:7 cannot be read from the tool.

Recommendation: Provide the root flags once in sushicore (a registrar beside `register_provision_commands`) and call it from every module CLI, so sr, sa, sb, sd and st gain them in the same shape as se and hub.

Review: confirmed. sr cli.py has no callback; grep over sushi*/cli/*/cli.py finds --version only at sushiengine cli.py:111-115 and root --json/--describe only at sushihub cli.py:50-57. The recommendation is incomplete for --json: sr services print Rich directly (services/select.py:57-77 builds a rich Table and calls console.console.print; project.py:594), so a registrar alone would produce a flag that does not yield JSON.

#### C11. [medium] A malformed config file reaches the user as a traceback

Evidence: sushicore/sushicore/workspace.py:42-47 `read_toml` calls `tomllib.load` with no handler. `sr config`, `sr build`, `sr toolchain` and the console itself all load cli/config.toml and cli/config.local.toml through it (cli/sushiruntime/config.py:171-181, sushicore/sushicore/diag.py:75-83). No `except` for `tomllib.TOMLDecodeError` exists in cli/sushiruntime. A hand edit of config.local.toml, which docs/CLI_GUIDE.md:54-56 invites, is enough to trigger it.

Recommendation: Catch the decode error at the sushicore boundary and re-raise a config error that names the file and line; have the entry point print it as one line and exit non-zero, as python-code-style requires.

Review: confirmed. sushicore/workspace.py:42-47 read_toml has no handler; config_base.py:165, diag.py:80 and sushicore/config.py:55-59 (console appearance) all parse without one. The only TOMLDecodeError handlers in sushicore are workspace.py:238 (link edit) and provision/registry.py:76. None in cli/sushiruntime. Not observed at runtime, read from source.

#### C12. [medium] Install hints disagree: some point to `hub install`, others to `sr setup`

Evidence: cli/sushiruntime/services/select.py:47-49 returns `sr setup --toolchain <name>`. cli/sushiruntime/services/project.py:361-366 and 401 tell the user to install with `hub install`, and select.py:108 says `hub install --customize`. A standalone checkout has no `hub`, and docs/CLI_GUIDE.md:302-334 presents `sr setup` as the standalone path.

Recommendation: Route every hint through one function (select.py's `_install_hint` is the existing one) so a missing toolchain always names `sr setup --toolchain <name>`, with `hub install` mentioned only as the workspace alternative.

Review: confirmed. services/select.py:47-49 returns `sr setup --toolchain <name>`; services/project.py:361-366 and 401 say `hub install`; select.py:108 says `hub install --customize` (the option exists, sushihub cli.py:274). CLI_GUIDE.md:302-334 presents sr setup as the standalone path.

#### C13. [medium] The ASan lane is selected by --type asan on build and by --asan on test, run and env

Evidence: cli.py:89-91 (`build --type asan`), cli.py:128-129 (`test --asan`), cli.py:160-161 (`run --asan`), cli.py:276-277 (`env --asan`). `sr build --asan` and `sr test --type asan` are both rejected. `sr package` has no lane option and always reads build/ (services/packaging.py:48). The audit's F6 covers --distributed but not this second two-shape option.

Recommendation: Pick one spelling for lane selection across the four commands and keep the other as a hidden alias for one release, as api-stability requires. se already has a lane concept (sushiengine/cli/sushiengine/services/lane.py), so settle it for the stack, not for sr alone.

Review: found by review.

#### C14. [medium] A --json or --describe registrar cannot work in sr while services render Rich output themselves

Evidence: python-code-style: 'A service never imports the command framework or writes to the terminal.' cli/sushiruntime/services/select.py:12 imports rich.table.Table and lines 57-77 build and print a table; services/project.py:594 calls console.console.print with markup; every service calls console.header/info/error directly. The audit's F9 recommends a sushicore root-flag registrar without noting that sr's services bypass the renderer.

Recommendation: Have each service return a result object and let the command layer hand it to the sushicore renderer; then the shared root flags in F9 have something to serialise. This changes the service interface, so plan it as its own task before F9.

Review: found by review.

#### C15. [medium] The audit's SOLID check on its own recommendations has two gaps

Evidence: F18 recommends replacing SystemExit with a typed error but sushicore/sushicore/module_config.py:69-71 documents the SystemExit as deliberate and cli_console.py:60, build_env.py:196, module_config.py:100 and cli/sushiruntime/env.py:154 catch it. F6 recommends renaming `run --distributed` to `--role` without the hidden-alias step that F7 applies to --sort.

Recommendation: State in F18 that the four catch sites change in the same commit, and in F6 that --distributed stays as a hidden alias on `run` for one release.

Review: found by review.

#### C16. [low] `--force` is ignored on Windows and the help does not say so

Evidence: cli.py:99-102 documents `--force` as a fallback to clang++/clang. In cli/sushiruntime/services/project.py:501-507 the Windows branch calls `_check_windows_compiler` and `_configure_args_windows` and never reads `force`; only the non-Windows branch (509-514) passes it to `_resolve_compiler`. docs/CLI_GUIDE.md:119-120 also gives no platform note.

Recommendation: Either honour the flag on Windows or state in the option help and the guide that it applies to Linux only.

Review: confirmed. services/project.py:501-507 (Windows branch) never reads `force`; 509 passes it only on the non-Windows branch. cli.py:99-102 and CLI_GUIDE.md:119-120 carry no platform note.

#### C17. [low] An unknown toolchain name exits 1 while every other bad argument exits 2

Evidence: cli.py:66-68 takes `name` as a free `Optional[str]`; services/select.py:89-92 prints an error and returns 1. Typer rejects a bad `--type` or `--suite` with exit 2, and sushicore returns 2 for an unknown check group (sushicore/sushicore/provision/commands.py:286-289) and for an unknown `--toolchain` in setup (line 203).

Recommendation: Type the argument as an enum built from `TOOLCHAINS` so Typer validates it, lists the choices in help and exits 2.

Review: confirmed. cli.py:66-68 free Optional[str]; services/select.py:89-92 returns 1; sushicore/provision/commands.py:286-289 exits 2 for an unknown group and returns _EXIT_USAGE near 203.

#### C18. [low] Dead code in the CLI layer

Evidence: cli/sushiruntime/cli.py:51-53 defines `DockerAction`, referenced nowhere (grep over cli/sushiruntime and cli/tests matched only the definition). cli.py:10-11 imports `sys` and `List`, both unused. cli/sushiruntime/services/discovery.py:21-22 and 26-28 export `find_executables`, `match_by_name` and `select_interactive`, none of which has a caller; only `resolve` is used (services/project.py:585).

Recommendation: Delete the enum, the two imports and the three unused aliases.

Review: confirmed. grep over cli/sushiruntime and cli/tests: DockerAction only at cli.py:51; find_executables, match_by_name, select_interactive only at services/discovery.py:21,22,26; no `sys.` or `List[` use in cli.py. With DockerAction gone, `import enum` at cli.py:9 is unused too; the audit lists only two imports.

#### C19. [low] cli/config.toml names the wrong environment prefix

Evidence: cli/config.toml:4 documents the layering as `... -> config.local.toml  ->  SUSHI_* env vars  ->  CLI flags`. cli/sushiruntime/config.py:32-42 sets `env_prefix="SR"` and its docstring at line 4 says `SR_* env vars`. Only the appearance keys use `SUSHI_CLI_*` (config.toml:48-49).

Recommendation: Change line 4 to `SR_* env vars`.

Review: confirmed. cli/config.toml:4 says SUSHI_* env vars; cli/sushiruntime/config.py:32 env_prefix='SR' and the docstring at line 4 says SR_*. The same line also lists 'CLI flags' as a layer although no sr flag overrides a config key.

#### C20. [low] `sr setup` writes to `cli/` even when the legacy `.tools` config directory is the one being read

Evidence: cli/sushiruntime/config.py:58-63 resolves the config directory to `.tools` when only `.tools/config.toml` exists. sushicore/sushicore/provision/commands.py:208 writes with `ModuleSink(root / "cli", ...)` and lines 310 and 331 use `root / "cli"` for link and unlink. A checkout on the legacy layout would have setup write a file the loader does not read. No `.tools` directory exists in this checkout.

Recommendation: Ask the owner whether the `.tools` fallback is still needed; if not, remove `_RuntimeModuleConfig` and use `ModuleConfig` directly, which also removes the last local subclass of a sushicore type in this CLI.

Review: confirmed. cli/sushiruntime/config.py:58-63 falls back to .tools; sushicore/provision/commands.py:208, 310 and 331 use root / 'cli'. Hypothetical in this checkout (no .tools directory). Deleting the fallback is correctly left to the owner.

#### C21. [low] A missing `docker` produces guidance about cmake paths

Evidence: cli/sushiruntime/services/docker.py:26 and 41 run docker through `sushicore.proc.Runner`. On `FileNotFoundError` the runner prints "set its path in config.local.toml (e.g. cmake_exe / ctest_exe / ninja_exe), or run `sr config` to see what the CLI resolved" (sushicore/sushicore/proc.py:76-82). Neither applies to docker, which has no config key.

Recommendation: Let the caller pass the hint to the runner, or check for docker before running and print an install line the way `_DOXYGEN_HINT` does for doxygen.

Review: confirmed. services/docker.py:26 and 41 call Runner.run; sushicore/proc.py:76-82 prints the cmake_exe / ctest_exe / ninja_exe hint for any missing executable.

#### C22. [low] Running outside a checkout prints an unstyled line that bypasses the console

Evidence: sushicore/sushicore/module_config.py:73-76 raises `SystemExit(self._profile.not_a_project_message())`. Every service starts with `find_project_root()` (for example services/project.py:451, 534, 566; services/docker.py:17), so `sr build` in the wrong directory prints the raw string on stderr without the error styling or icon that every other failure uses.

Recommendation: Raise a typed error from sushicore and have one entry-point handler print it through `console.error` and exit 1.

Review: confirmed. sushicore/module_config.py:73-76 raises SystemExit(message); services call find_project_root first (project.py:451, 534, 566; docker.py:17). The docstring at module_config.py:69-71 says the SystemExit is deliberate and three callers catch it (cli_console.py:60, build_env.py:196, module_config.py:100; also sr env.py:154), so a typed error must update those catch sites in the same change. The audit does not say this.

#### C23. [low] `sr env --all` has no short flag while sd and st have `-a`

Evidence: sushiruntime/cli/sushiruntime/cli.py:274-275 declares `--all` only, as do sa (sushiai/cli/sushiai/cli.py:244) and sb (sushiblas/cli/sushiblas/cli.py:163). sushidsp/cli/sushidsp/cli.py:120 and sushitrack/cli/sushitrack_cli/cli.py:355 declare `--all`, `-a`. The `config` and `env` commands are each written out again in every CLI although the service behind them is the shared `sushicore.diag.Diagnostics`.

Recommendation: Register `config` and `env` from sushicore the way the provision commands are, with one option set; sr's extra `--asan` becomes a declared extension.

Review: confirmed. sr cli.py:274-275, sa cli.py:244, sb cli.py:163 and se cli.py:854 declare --all only; sd cli.py:120 and st cli.py:355 add -a.

#### C24. [low] The guide calls docs/README.md the top-level README, and the repository has none at the root

Evidence: docs/CLI_GUIDE.md:339-340 says "see the Building with CMake directly section of the top-level `[README](README.md)`". The link resolves to docs/README.md (the section is at line 110 there). A listing of D:/Projects/sushiruntime shows no README.md at the root.

Recommendation: Fix the wording here; the missing front-door README belongs to the documentation-architecture review.

Review: confirmed. docs/CLI_GUIDE.md:339-340 says 'top-level README' and links docs/README.md; `ls D:/Projects/sushiruntime` shows no README.md at the root; the section is at docs/README.md:110.

### Not checked

- No CLI command was run (the task forbids it), so the rendered help pages, real exit codes, colour behaviour and the traceback in F10 are read from source, not observed.
- cli/tests/*.py were listed but not read; whether the tests cover the findings above is unknown.
- sushicore internals beyond provision/commands.py, typer_help.py, module_config.py, proc.py, cli_console.py, diag.py, discovery.py and part of config.py: cmake_driver.py, build_env.py, console.py, renderer.py, profile.py, the provision pipeline, steps and checks were not read. Exceptions raised inside them (network, lock, package managers) during `sr setup` were not traced.
- `sushicore.config_base.write_tool_section` and `load_tool_config` were not read; OSError or permission failures while writing config.local.toml in `sr toolchain <name>` are unverified.
- Sibling CLIs were surveyed by grep of command and option names only (se, sa, sb, sd, st, hub); their help wording, panels and option semantics were not compared line by line.
- docs/INTRODUCTION.md, docs/CONTRIBUTING.md, docs/INTEGRATION.md, docs/DISTRIBUTED_GUIDE.md and docs/README.md beyond lines 24-188 were only grepped for CLI command names, not read for accuracy.
- The Dockerfile was not read, so the `WITH_ONEAPI` build argument and the `/workspace/sushiruntime` mount path that services/docker.py relies on are unverified.
- The suite list and the `sr_distributed_demo` target were not checked against tests/CMakeLists.txt beyond confirming the demo name appears in examples/CMakeLists.txt.
- Source-comment and licence-header conformance of the CLI files (no licence block, separator comment lines in cli.py, long `#` paragraphs in services/project.py) belongs to the code review dimension and was not assessed here.
- The untracked, ignored leftovers cli/build/, cli/sushiruntime_cli.egg-info/ and cli/config.local.toml.bak were skipped as instructed.
- macOS behaviour: the code treats every non-Windows platform as Linux (`_configure_args_linux`, `_snapshot_linux`); whether macOS is a supported target was not established.

## Documentation

### Facts

- Root README.md: MISSING. `ls README*` at D:/Projects/sushiruntime finds nothing; the only project README is docs/README.md (582 lines), which is written as the front door, not as a manual index.
- docs/README.md: EXISTS but is the product README (requirements, build, API tour, architecture, layout, license). It carries no index of the manual.
- docs/CONTRIBUTING.md: EXISTS (420 lines). docs/DOCUMENTATION_STYLE_GUIDE.md: MISSING.
- docs/getting_started/, docs/architecture/, docs/modules/, docs/guides/: all MISSING. Their content sits as loose top-level files: docs/INTRODUCTION.md, docs/ARCHITECTURE.md, docs/CLI_GUIDE.md, docs/DISTRIBUTED_GUIDE.md, docs/INTEGRATION.md.
- docs/reference/: EXISTS with CHANGELOG.md only (14 lines, 10 entries). docs/reference/GLOSSARY.md and docs/reference/KNOWN_ISSUES.md: MISSING.
- docs/design/: MISSING entirely, so no docs/design/README.md topic map and no docs/design/REMAINING_WORK.md.
- docs/agent/: EXISTS with reports/ only (2 files: 2026-09-22-help-screen-task-26.md, 2026-09-22-wave-1-task-7a.md). docs/agent/specs and docs/agent/plans: MISSING. File names are kebab-case dated, not the documentation skill's `YYYY_MM_DD_WORK_NAME/` folder with SPEC.md/PLAN.md/REPORT.md.
- docs/archive/: MISSING.
- Per-module README.md: NONE. No README.md under cli/, src/, include/SushiRuntime/ or any of its nine module folders (api, core, distributed, execution, graph, interop, memory, scheduler, topology), tests/, examples/, tools/, cmake/. The only other README is the untracked cli/.pytest_cache/README.md.
- Unplaced folder docs/slop/ (7 files, 3212 lines): BACKBONE_WAVE_STATUS, CODE_AND_ARCHITECTURE_REVIEW, ENGINE_BACKBONE_REFACTOR, PHYSICS_SUBSTRATE_IMPLEMENTATION, PHYSICS_SUBSTRATE_REQUIREMENTS, SIMULATION_ENGINE_SUBSTRATE_PLAN, TODO. Last commit touching TODO.md is 2026-09-15.
- Placement of docs/slop/: TODO.md is a backlog -> docs/design/REMAINING_WORK.md. ENGINE_BACKBONE_REFACTOR, PHYSICS_SUBSTRATE_REQUIREMENTS (R9 open), PHYSICS_SUBSTRATE_IMPLEMENTATION, SIMULATION_ENGINE_SUBSTRATE_PLAN are intent -> docs/design/ if a phase is open, else docs/archive/. BACKBONE_WAVE_STATUS and CODE_AND_ARCHITECTURE_REVIEW (dated 2026-07-22) are agent session output -> docs/agent/ or docs/archive/.
- Unplaced folder docs/superpowers/specs/ (6 files, 1049 lines): four *-design.md and two *-plan.md. They are agent-written specs and plans -> docs/agent/ (specs, plans); the three design documents the source cites as the reason for the code (g1-node-event-records, scan-and-counting-sort, graph-replay, usm-allocation-seam) are candidates for docs/design/<TOPIC>.md.
- Unplaced folder docs/api/ (mainpage.md, the Doxygen main page, wired in Doxyfile lines 28 and 35) and docs/api-site/ (generated output, gitignored except its own .gitignore). Neither name exists in the architecture; mainpage.md belongs under docs/reference/.
- Loose files under docs/ with no slot: ARCHITECTURE.md (1285 lines) -> docs/architecture/; INTRODUCTION.md (980) -> docs/getting_started/; CLI_GUIDE.md, DISTRIBUTED_GUIDE.md, INTEGRATION.md -> docs/guides/; CODE_OF_CONDUCT.md, SECURITY.md, CLAUDE.md -> repository root (or .github/ for the two community files).
- Duplicated CHANGELOG: docs/CHANGELOG.md (668 lines, Keep a Changelog format, sections [Unreleased], [0.3.0], [0.2.0], a template [x.y.z]) and docs/reference/CHANGELOG.md (14 lines, Unreleased only). Both are live and both have an Unreleased section.
- Duplicated LICENSE: root LICENSE (224 lines, 'Apache License v2.0 with LLVM Exceptions', copyright 2026-present Mustafa Garip & Sushi Systems) and docs/LICENSE (201 lines, plain Apache 2.0 text). `cmp` reports they differ from byte 1.
- Versions in the tree: library 0.3.0 (CMakeLists.txt:32, include/SushiRuntime/version.hpp:51), git tags v0.2.0 and v0.3.0, CLI package sushiruntime-cli 1.0.0 with sushicore>=0.7.0 (cli/pyproject.toml:7,14).
- Relative markdown links: a scripted check of every `[text](target)` in docs/**/*.md and .github/PULL_REQUEST_TEMPLATE.md found no broken file target and no broken heading anchor. Broken items are all backticked cited paths (see findings).
- CLI claims checked against cli/sushiruntime/cli.py and services/project.py: commands toolchain, build, test, run, package, clean, doxygen, docker build/run, config, env, plus setup/doctor/link/unlink from sushicore; every flag and suite named in docs/README.md and the docs/CLI_GUIDE.md headings exists. `hub install-cli` exists in D:/Projects/sushistack/cli/sushihub.
- CMake claims checked: SR_SYCL_TOOLCHAIN, SR_GPU_BACKEND, SR_ENABLE_CUDA, SR_CUDA_ARCH, SR_AMD_ARCH, SR_ENABLE_DISTRIBUTED, SR_BUILD_TESTS, SR_BUILD_BENCHMARKS, SR_DETERMINISTIC_FP, SR_NATIVE_ARCH and the three presets all exist; CI image intel/oneapi-basekit and the nightly cron match docs/README.md:230.
- Source files cite the misplaced folders: 13 files under include/, cmake/ and tests/ carry 15 references to docs/superpowers/specs/*.md or docs/slop/*.md, so moving those documents is a code-touching change.
- Root clutter relevant to layout (untracked, gitignored): four empty CXX-DetectStdlib-*.h files, .scratch_syntax/ (empty), .superpowers/sdd/, venv/.

### Corrections from review

- F2 says 20 of 26 markdown files are unreachable from docs/README.md. There are 28 markdown files under docs/ (11 top-level, 1 api, 1 reference, 7 slop, 6 superpowers/specs, 2 agent/reports); 24 are unreachable. The audit's own list adds up to 23 and leaves out docs/CLAUDE.md.
- The audit counts 15 source references to docs/slop and docs/superpowers in 13 files. There are 16 in 13 files: tests/benchmark/performance/test_chain_dispatch_perf.cpp:24 wraps the path across two lines ('docs/superpowers/specs/2026-09-19-graph-' then the rest), so a path-pattern search misses it. A move that updates '15 citations' leaves this one dangling.
- The facts say 'the three design documents the source cites' and then name four. Source and tests cite three (g1-node-event-records-design, scan-and-counting-sort-design, graph-replay-design). The usm-allocation-seam design is cited only by docs/ARCHITECTURE.md:639.
- F12 line numbers: 'the bitmask pool' is at docs/ARCHITECTURE.md:988, MemoryPage at :989, ThreadLocalMagazine at :990. The finding says 987 and 989-990; its own verification output says 990 and 991.
- F3 says 'docs/CONTRIBUTING.md:230 and Doxyfile point at neither consistently'. Doxyfile contains no reference to any changelog.
- F18 places the build rule at docs/CLAUDE.md lines 38-40; it is the paragraph at line 41 under the '### Building' heading.
- F4 reports one entry over 240 characters (line 45, 318). That is a physical-line measurement. Entries wrap over many lines (the one at lines 91-114 runs 24 lines), so the number of entries over 240 characters is far higher than one.

### Findings

#### D1. [high] No root README.md; the front door is filed as docs/README.md

Evidence: D:/Projects/sushiruntime has no README* at the root (`ls README*`: 'No such file or directory'). D:/Projects/sushiruntime/docs/README.md is the 582-line product README. GitHub falls back to it, but its line 582 link `[LICENSE](LICENSE)` then resolves to docs/LICENSE, and docs/INTRODUCTION.md:18 calls it 'the top-level README'.

Recommendation: Create a root README.md as the front door from the current docs/README.md content (what it is, install, one example, links into the manual), and move the API tour and layer descriptions into the manual pages they duplicate.

Review: confirmed. No README* at D:/Projects/sushiruntime root; docs/README.md is 582 lines and line 582 links `[LICENSE](LICENSE)`, which resolves to docs/LICENSE. The 'top-level README' wording is at docs/INTRODUCTION.md:17-18.

#### D2. [high] docs/README.md is not a manual index; most documents are unreachable from it

Evidence: docs/README.md links only CONTRIBUTING.md (line 61), INTEGRATION.md (68), ARCHITECTURE.md (158) and LICENSE (582). Not linked from it: docs/INTRODUCTION.md, docs/CLI_GUIDE.md, docs/DISTRIBUTED_GUIDE.md, docs/SECURITY.md, docs/CODE_OF_CONDUCT.md, docs/CHANGELOG.md, docs/reference/CHANGELOG.md, docs/api/mainpage.md, all 7 files in docs/slop/, all 6 in docs/superpowers/specs/, both in docs/agent/reports/ (20 of 26 markdown files).

Recommendation: Rewrite docs/README.md as the index: one line per document, grouped by getting_started, architecture, guides, reference, design, agent, archive.

Review: confirmed. docs/README.md has four relative links (lines 61, 68, 158, 582) and no plain-text mention of the other pages. The count is wrong: 28 markdown files under docs/, 24 unreachable (see wrong_facts).

#### D3. [high] Two live changelogs, each with its own Unreleased section

Evidence: docs/CHANGELOG.md:9 `## [Unreleased]` (entries dated 2026-09-15 to 2026-09-20, then [0.3.0] at line 42 and [0.2.0] at line 122) and docs/reference/CHANGELOG.md:3 `## Unreleased` (entries 2026-09-21 to 2026-10-04). docs/CONTRIBUTING.md:230 and Doxyfile point at neither consistently; CONTRIBUTING links `CHANGELOG.md`, the old one.

Recommendation: Keep docs/reference/CHANGELOG.md only. Merge the old Unreleased entries into it, keep the latest release section (v0.3.0), move v0.2.0 unchanged to docs/archive/changelog/v0.2.0.md, delete docs/CHANGELOG.md.

Review: confirmed. docs/CHANGELOG.md:9 '## [Unreleased]' and docs/reference/CHANGELOG.md:3 '## Unreleased' both exist; CONTRIBUTING.md:230 links the old file. Doxyfile does not mention a changelog at all, so that half of the sentence is empty.

#### D4. [high] docs/CHANGELOG.md breaks the one-line entry rule throughout

Evidence: Scripted count over docs/CHANGELOG.md: 104 top-level entries, 69 without a leading date (first at lines 58, 67, 75, 91, 125), 26 nested bullets (first at 547, 551, 553, 556, 561), one entry of 318 characters (line 45). Entries are multi-line paragraphs that explain why, e.g. lines 91-114 'The pooled USM allocation path...' running 24 lines and ending in a design-document citation. Grouped by Breaking/Added/Changed/Fixed instead of flat with a scope. Line 653 keeps a `## [x.y.z] — YYYY-MM-DD` template section in the live file.

Recommendation: Rewrite the retained release section as dated one-line entries in the `- <date> — <scope>: <Verb> <what> (<where>).` shape; move the reasons into the design documents the entries may link.

Review: confirmed. Recomputed on docs/CHANGELOG.md: 104 top-level entries, 35 dated so 69 undated, 26 nested bullets, template heading at line 653. The 318-character figure counts one physical line; wrapped entries are far longer, so the audit understates the breach.

#### D5. [high] CONTRIBUTING contradicts the tree on versioning and the changelog

Evidence: docs/CONTRIBUTING.md:228-234: 'Changelog entries start at v1.0.0. The project has not yet been assigned a semantic version — we are in prototype stage. Do not add entries to CHANGELOG.md until the project cuts its v1.0.0 release.' The tree has `project(SushiRuntime VERSION 0.3.0` (CMakeLists.txt:32), tags v0.2.0 and v0.3.0, 104 entries in docs/CHANGELOG.md and 10 in docs/reference/CHANGELOG.md. The same paragraph prescribes `## [Unreleased]` with Added/Changed/Fixed groups, which is not the house format.

Recommendation: Replace the bullet with the current rule: every change adds one line to docs/reference/CHANGELOG.md in the same commit.

Review: confirmed. docs/CONTRIBUTING.md:228-234 reads as quoted; CMakeLists.txt:32 is VERSION 0.3.0 and tags v0.2.0, v0.3.0 exist.

#### D6. [high] Two license texts that disagree, and the README names the wrong one

Evidence: LICENSE:2 'SushiRuntime is under the Apache License v2.0 with LLVM Exceptions' (224 lines). docs/LICENSE is plain Apache 2.0 (201 lines); `cmp LICENSE docs/LICENSE` differs at byte 1. docs/README.md:582 says 'licensed under the Apache License, Version 2.0' and links docs/LICENSE; docs/CONTRIBUTING.md:8 links the same docs/LICENSE; docs/CONTRIBUTING.md:151 requires the Apache 2.0 header on every source file.

Recommendation: Delete docs/LICENSE, link the root LICENSE from every page, and state the license once with its exception. This has to be redone when the company-wide license decision lands, so do both in one change.

Review: confirmed. LICENSE:2 names the LLVM exception, docs/LICENSE opens with plain Apache 2.0; docs/README.md:582 and docs/CONTRIBUTING.md:7-8 link docs/LICENSE; CONTRIBUTING.md:151 requires the Apache 2.0 header.

#### D7. [high] The backlog lives in docs/slop/TODO.md and competes with other open-work lists

Evidence: docs/design/REMAINING_WORK.md does not exist. docs/slop/TODO.md (241 lines, 'SushiRuntime — Open Work', line 7 an OPEN blocker dated 2026-09-14) is the backlog, mixed with RESOLVED history (lines 18-40). Open work is also tracked in docs/slop/PHYSICS_SUBSTRATE_REQUIREMENTS.md:5 ('R9 is open'), docs/slop/BACKBONE_WAVE_STATUS.md:3-4 ('what the next session has to pick up'), docs/superpowers/specs/2026-09-15-g1-node-event-records-plan.md:3 ('wave 4 open') and docs/superpowers/specs/2026-09-19-graph-replay-design.md:3-4 ('P2 is not started').

Recommendation: Create docs/design/REMAINING_WORK.md holding only open items, each linking its design document; move resolved history to docs/archive/.

Review: confirmed. docs/slop/TODO.md:7 is the OPEN Windows CUDA item with RESOLVED history below it; the other open-work lines sit at the cited places in PHYSICS_SUBSTRATE_REQUIREMENTS.md:3-5, BACKBONE_WAVE_STATUS.md:3-4, the g1 plan line 3 and the graph-replay design lines 3-4.

#### D8. [high] docs/slop/ and docs/superpowers/ are outside the architecture and hold live design intent

Evidence: docs/slop/ (7 files, 3212 lines) and docs/superpowers/specs/ (6 files, 1049 lines). The manual depends on them: docs/ARCHITECTURE.md:351 and :639 cite docs/superpowers/specs/2026-09-19-graph-replay-design.md and 2026-08-18-usm-allocation-seam-design.md as the reason for the design; cmake/SushiRuntimeInstall.cmake:15 cites docs/slop/ENGINE_BACKBONE_REFACTOR.md BB-5d as an open question; include/SushiRuntime/api/graph/graph.hpp:1749, 3115, 3201, 3260 cite the specs.

Recommendation: Move each document by the placement questions: open intent to docs/design/<TOPIC>.md with a status line, agent specs/plans to docs/agent/, finished work to docs/archive/. Update the 15 source citations in the same commit. File names move to UPPER_SNAKE_CASE.

Review: confirmed. ARCHITECTURE.md:351 and :639, cmake/SushiRuntimeInstall.cmake:15 and graph.hpp:1749, 3115, 3201, 3260 cite the folders as stated. The citation count is 16 in 13 files, not 15 (see wrong_facts).

#### D9. [medium] docs/reference/CHANGELOG.md entries lack the scope and are out of order

Evidence: docs/reference/CHANGELOG.md lines 5-12: 8 of 10 entries have no `<scope>:` prefix (only the two 2026-10-04 lines carry `cli:`). Order is oldest first (2026-09-21 at line 5, 2026-10-04 at line 13); the rule is newest first. Line 8 adds a reason clause: 'so `hub` knows it from the checkout'.

Recommendation: Add the commit scope to each entry, reverse the order, and drop the reason clause from line 8.

Review: confirmed. docs/reference/CHANGELOG.md lines 5-14: 8 of 10 entries without scope, oldest first, line 8 carries 'so `hub` knows it from the checkout'.

#### D10. [medium] Design documents without a conforming status line

Evidence: No status at all: docs/slop/TODO.md, docs/slop/BACKBONE_WAVE_STATUS.md (line 3 is prose), docs/slop/CODE_AND_ARCHITECTURE_REVIEW.md (line 3 `**Tarih:** 2026-07-22`). Status present but not as line 2 and not in the `**Status:** Open — phase N of M` / `Shipped` form: docs/slop/ENGINE_BACKBONE_REFACTOR.md:3 ('engineering plan, authored 2026-07-30', names an unmerged branch), PHYSICS_SUBSTRATE_IMPLEMENTATION.md:3, PHYSICS_SUBSTRATE_REQUIREMENTS.md:3, SIMULATION_ENGINE_SUBSTRATE_PLAN.md:3 ('No code has been changed by this document'), and all six docs/superpowers/specs files (e.g. 2026-09-16-scan-and-counting-sort-design.md:3 'Nothing is built' while docs/CHANGELOG.md records add_prefix_scan and add_counting_sort as added on 2026-09-16/17).

Recommendation: When each document is re-homed, set line 2 to the true status. The 'Nothing is built' / 'nothing dispatched' lines on the g1 and scan/sort specs are stale and must be corrected against the changelog.

Review: confirmed. First five lines of all 13 files read: three have no status, the rest carry free-form status prose on line 3. The scan/sort design says 'Nothing is built' while docs/CHANGELOG.md:18-22 records both primitives as added.

#### D11. [medium] Cited paths that do not resolve

Evidence: docs/ARCHITECTURE.md:87 cites `include/SushiRuntime/api/runtime.hpp`; the file is include/SushiRuntime/api/runtime/runtime.hpp. include/SushiRuntime/core/bit.hpp:21 cites `docs/slop/cpp_standard_migration.md`, which does not exist (docs/slop/CODE_AND_ARCHITECTURE_REVIEW.md:184 already flags it). docs/CONTRIBUTING.md:145 names `ProjectOptions.cmake` and `Vcpkg.cmake`; the files are cmake/SushiRuntimeProjectOptions.cmake and cmake/SushiRuntimeVcpkg.cmake (the sentence describes the pre-rename collision but gives no current name). docs/slop/TODO.md:27 cites a path in another repository's .superpowers scratch folder.

Recommendation: Fix the ARCHITECTURE path, remove or re-point the bit.hpp citation, and name the current CMake files in CONTRIBUTING.

Review: confirmed. ARCHITECTURE.md:87, bit.hpp:21 and TODO.md:27 check out. The CONTRIBUTING.md:145 item is weak: the sentence recounts a past collision between old file names and is not a citation that should resolve.

#### D12. [medium] Manual pages still describe the removed memory pool

Evidence: The pooled allocator was deleted (docs/ARCHITECTURE.md:634 'There is no allocation pool'; include/SushiRuntime/memory/ holds allocation.hpp, epoch_reclamation.hpp, usm_allocation_owner.hpp, usm_registry.hpp). Yet docs/README.md:524 lists 'memory/  USM allocator, memory pool, epoch reclamation'; docs/ARCHITECTURE.md:987 says 'the bitmask pool ... are all lock-free' and :989-990 lists `MemoryPage` and `ThreadLocalMagazine` as `alignas(64)` types (neither identifier exists in include/ or src/); docs/SECURITY.md:24 and :61 name 'the memory pool' / 'the lock-free pool' as components; docs/CONTRIBUTING.md:79 requires an ASAN pass for changes to the 'memory pool'.

Recommendation: Remove the pool from all five places. ARCHITECTURE.md:6 promises that every named class exists in the headers, so lines 987-990 break the page's own contract.

Review: confirmed. Pool mentions at README.md:524, CONTRIBUTING.md:79, SECURITY.md:24 and :61, ARCHITECTURE.md:988 (not 987); MemoryPage and ThreadLocalMagazine are at :989-990 and occur nowhere in include/ or src/.

#### D13. [medium] Other claims that no longer match the code

Evidence: docs/ARCHITECTURE.md:112, :114 name `range_fn` and `raw_kernel_fn`, and :689 names a `bool is_fused` node field; none of the three identifiers occurs anywhere in include/ or src/. docs/README.md 'Architecture' and 'Project layout' sections (lines 416-544) omit the interop/ layer that exists at include/SushiRuntime/interop and src/interop and that docs/ARCHITECTURE.md:1272 lists. docs/README.md:542 says docs/ holds 'the fluent API reference (api/, Doxyfile)'; Doxyfile is at the repository root. docs/CONTRIBUTING.md:355-363 field reference omits the `provides` field that cli/sushistack.deps.toml uses on every toolchain entry. .gitignore:9-11 still describes a repo-local `dependencies/` folder fetched by `sr setup`, which docs/CLI_GUIDE.md:324-325 says `sr` does not read.

Recommendation: Correct each line against the header it describes; add interop/ to the README layout or drop the duplicated layout in favour of ARCHITECTURE section 16.

Review: confirmed. range_fn, raw_kernel_fn and is_fused have zero hits in include/ and src/; 'interop' has zero hits in docs/README.md; Doxyfile is at the root; `provides` is on the three sycl-toolchain entries and absent from the field table; .gitignore:9-11 as quoted.

#### D14. [medium] Required manual files and folders are absent

Evidence: Missing: docs/DOCUMENTATION_STYLE_GUIDE.md, docs/reference/GLOSSARY.md, docs/reference/KNOWN_ISSUES.md, docs/design/README.md, docs/archive/, docs/getting_started/, docs/architecture/, docs/guides/, docs/modules/. Known issues exist but are scattered: docs/DISTRIBUTED_GUIDE.md:373 'v1 limitations', docs/slop/TODO.md:7 (host read of Shared memory faults on Windows CUDA, 0xC0000006).

Recommendation: Build the skeleton as its own approved task. Seed KNOWN_ISSUES.md from the Windows CUDA fault and the distributed v1 limitations; seed GLOSSARY.md from the terms ARCHITECTURE.md defines inline (hazard, region, residency, chain, late-bound).

Review: confirmed. None of the listed files or folders exist; docs/DISTRIBUTED_GUIDE.md:373 is '## 12. v1 limitations'.

#### D15. [medium] No module README anywhere

Evidence: No README.md in cli/, tools/, tests/, examples/, cmake/, src/ or any of include/SushiRuntime/{api,core,distributed,execution,graph,interop,memory,scheduler,topology}. Module facts live only in docs/ARCHITECTURE.md sections 3-12 (lines 85-1070) and are duplicated in docs/README.md:416-512.

Recommendation: Give each of the nine runtime modules and cli/ a README.md with its own facts, moved out of ARCHITECTURE.md sections 5-12; keep ARCHITECTURE for what crosses modules.

Review: confirmed. The only README files are docs/README.md and cli/.pytest_cache/README.md.

#### D16. [medium] ARCHITECTURE.md and the README duplicate each other, and ARCHITECTURE nears the split limit

Evidence: docs/ARCHITECTURE.md is 1285 lines (the documentation skill splits a design document above 1500). docs/README.md:416-512 restates its per-layer descriptions, :514-544 restates its directory map (docs/ARCHITECTURE.md:1256-1282), and :232-414 restates the API tour of docs/INTRODUCTION.md sections 2 and 4. The two directory maps already disagree (interop/ and the memory line, see F12, F13).

Recommendation: One home per fact: README keeps the pitch and install, getting_started keeps the tour, architecture keeps the layers. Split ARCHITECTURE.md into docs/architecture/ chapters when it moves.

Review: confirmed. The duplication between README.md:416-544 and ARCHITECTURE.md is real. The 1500-line split rule in the documentation skill covers design documents, not manual pages, so the 'nears the split limit' half of the title rests on the wrong rule.

#### D17. [medium] docs/agent/ does not follow the work-folder shape

Evidence: docs/agent/reports/2026-09-22-help-screen-task-26.md and docs/agent/reports/2026-09-22-wave-1-task-7a.md are loose dated files. The matching specs and plans are elsewhere (docs/superpowers/specs/, and the 7a report describes a plan held in the sushistack repository). The 7a report line 5 records `sushicore>=0.1.0`; cli/pyproject.toml:14 is now `sushicore>=0.7.0`, and the work is closed, so both reports are archive candidates.

Recommendation: Settle one shape (the global CLAUDE.md names specs|plans|reports, the documentation skill names one dated folder per work with SPEC/PLAN/REPORT; they differ and the owner should pick), then re-home the reports and the docs/superpowers files into it.

Review: confirmed. Both reports are loose dated files; 7a report line 5 records sushicore>=0.1.0 against cli/pyproject.toml:14 >=0.7.0. The skill and the global CLAUDE.md do name different shapes for docs/agent/.

#### D18. [medium] docs/CLAUDE.md is misplaced and conflicts with the global rules

Evidence: docs/CLAUDE.md sits under docs/, where Claude Code does not load it at session start for work at the repository root; there is no root CLAUDE.md. Line 8 tells the agent to 'read every `.md` file under `docs/`' (10 000+ lines including docs/slop). Lines 38-40 make `sr build` / `sr test` mandatory, which conflicts with the delegation rule that subagents run no build. Line 49 ends 'love you boss, kolay gelsin.'

Recommendation: Move it to the repository root, cut it to the repository-specific rules, and reconcile the build paragraph with the global delegation rule.

Review: confirmed. docs/CLAUDE.md read in full (49 lines); no root CLAUDE.md. The build paragraph is at line 41, not 38-40. It routes builds through `sr`, which drives cmake, so it does collide with delegation rule 3.

#### D19. [medium] Manual prose runs at 3 to 4 em dashes per 300 words and leans on emphasis framing

Evidence: Measured: docs/README.md 60 em dashes in 4695 words (3.8 per 300), docs/ARCHITECTURE.md 143 in 10644 (4.0), docs/CONTRIBUTING.md 34 in 2856 (3.6), docs/DISTRIBUTED_GUIDE.md 30 in 2560 (3.5), docs/INTRODUCTION.md 67 in 5831 (3.4), docs/INTEGRATION.md 15 in 1336 (3.4), docs/CLI_GUIDE.md 25 in 2251 (3.3); the humanizer ceiling is about one per 300. Framing tells: docs/INTRODUCTION.md:8 'The big idea: **you never wire up task dependencies by hand.** You just say', docs/DISTRIBUTED_GUIDE.md:14 '## 1. The one idea you must internalize', docs/README.md:320 'Deterministic reductions are first-class', docs/README.md:313 'a graph worth measuring is worth naming', docs/CONTRIBUTING.md:219-224 'Documentation is not a follow-up task — it ships ...' (corrective contrast), docs/ARCHITECTURE.md:1230 'Invariants worth never breaking'. Horizontal-rule separators between every section: 16 in ARCHITECTURE.md, 9 in CONTRIBUTING.md, 7 in INTRODUCTION.md.

Recommendation: The content is specific and mostly sound, so this is a humanizer pass during the re-homing, not a rewrite: replace dash asides with full stops or commas, drop the bold 'big idea' framings and the rule separators. docs/SECURITY.md and docs/api/mainpage.md (1.2 per 300) need none.

Review: confirmed. Recounted five files; words and em dashes match exactly (README 4695/60, ARCHITECTURE 10644/143, INTRODUCTION 5831/67, CONTRIBUTING 2856/34, SECURITY 488/2). Horizontal rules in markdown are not a humanizer tell, so that part of the evidence does not carry weight.

#### D20. [medium] Feature commits of 2026-09-19 to 2026-09-21 have no changelog entry in either file

Evidence: git log shows 'feat(core): carry a dispatch's launched and live width on its event record', 'feat(execution): record each dispatch's launched and live width' and 'feat(graph): stamp a node's dispatch width the way its name is stamped' on 2026-09-21. D:/Projects/sushiruntime/docs/reference/CHANGELOG.md lines 5-7 hold only two fixes and one doc note for that day, and D:/Projects/sushiruntime/docs/CHANGELOG.md has no entry mentioning the dispatch width. 'chore: license SushiRuntime under Apache-2.0 with LLVM exceptions' (2026-09-19) is in neither file either.

Recommendation: When the two changelogs are merged, walk git log from the v0.3.0 tag and add one line per user-visible commit that has none, starting with the dispatch-width fields and the licence change.

Review: found by review.

#### D21. [medium] No documentation checker in tools/

Evidence: D:/Projects/sushiruntime/tools holds only packaging/. The documentation skill relies on check_docs_layout.py to list archive candidates, and the global CLAUDE.md names tools/documentation/check_source_comments.py as the checker every repository carries. Nothing enforces layout, link resolution or the changelog line shape here, which is how two changelogs and two licence texts stayed live.

Recommendation: Add tools/documentation/ with the layout and changelog checkers in the same shape the sibling repositories use, as part of the skeleton build-out task and before the documents are re-homed.

Review: found by review.

#### D22. [medium] The audit did not rule on which agent-work shape or status-line position applies before grading against them

Evidence: F10 grades status lines against 'line 2' and F17 against the dated work-folder shape, while the relayed global CLAUDE.md names docs/agent/specs|plans|reports and puts archive before design in the placement order; the documentation skill reverses those two questions and adds a 90-day rule. The placement facts for docs/slop ('docs/design/ if a phase is open, else docs/archive/') give the same answer under both orders only because the audit never established which phases are open (it lists this under not_checked).

Recommendation: Get the owner's ruling on the two conflicts (agent folder shape, placement order) first, then read the six slop and six spec bodies to fix each document's open or shipped state before anything is moved.

Review: found by review.

#### D23. [low] A Turkish document in an English-only tree

Evidence: docs/slop/CODE_AND_ARCHITECTURE_REVIEW.md (354 lines) is written in Turkish: line 3 `**Tarih:** 2026-07-22`, line 4 `**Kapsam:** Tüm C++ çekirdeği ...`. The global rule puts every project artifact in English.

Recommendation: It is a finished review dated 2026-07-22; archive it as it is under docs/archive/ rather than translate it, and carry any still-open item into REMAINING_WORK.md in English.

Review: confirmed. docs/slop/CODE_AND_ARCHITECTURE_REVIEW.md lines 3-5 are Turkish (Tarih, Kapsam, Yöntem).

#### D24. [low] Community and API files have no defined home

Evidence: docs/CODE_OF_CONDUCT.md, docs/SECURITY.md and docs/api/mainpage.md are in docs/ with no slot in the architecture; docs/api-site/ is a build output directory inside docs/ (Doxyfile:12 `OUTPUT_DIRECTORY = docs/api-site`, :51 warnings log). .github/ holds PULL_REQUEST_TEMPLATE.md and issue templates only.

Recommendation: Move CODE_OF_CONDUCT.md and SECURITY.md to .github/ or the root (GitHub finds them in either), move mainpage.md to docs/reference/, and point the Doxygen output at the build tree. Doxyfile is a build configuration file, so that edit needs the owner's go-ahead.

Review: confirmed. Doxyfile:12, :28, :35, :51 as stated; .github/ holds only the PR template, two issue templates and workflows.

### Not checked

- Line-by-line accuracy of docs/ARCHITECTURE.md (1285 lines) and docs/INTRODUCTION.md (980 lines): only backticked identifiers and file paths were machine-checked against include/, src/, cli/, cmake/, tests/, examples/; described behaviour, numeric constants and algorithms were not compared with the code.
- Whether the C++ samples in docs/README.md, docs/INTRODUCTION.md, docs/INTEGRATION.md, docs/DISTRIBUTED_GUIDE.md and docs/api/mainpage.md compile against the current headers (no build allowed; signatures not compared).
- docs/CLI_GUIDE.md body text: section headings, command names and flags were checked against cli.py; per-flag descriptions, default values, the config precedence paragraph (lines 44-60) and the `sr doctor --for` groups were not.
- docs/DISTRIBUTED_GUIDE.md and docs/INTEGRATION.md content beyond headings, version numbers and cited paths.
- The body of the six docs/superpowers/specs files and six of the seven docs/slop files: only the first lines (status) and a few greps were read, so which phases are really open or shipped is not established.
- docs/CHANGELOG.md entries were measured by script for shape only; their factual accuracy and the content of the [0.2.0] section (lines 122-652) were not read.
- External URLs (sushisystems.io, install.sh/install.ps1, keepachangelog, GitHub links) were not fetched.
- Doxygen comments in headers and the generated docs/api-site output; source-comment conformance belongs to another dimension.
- .github/ISSUE_TEMPLATE/*.yml wording, Dockerfile comments, cli/config.toml comments and sushi-module.toml's cited docs/reference/MODULE_MANIFEST.md in the SushiStack repository.
- The `hub install-cli` behaviour on each platform described at docs/CLI_GUIDE.md:30 (pipx on Windows, other on Linux/macOS) was not compared with sushihub's code; only the command's existence was confirmed.
- The untracked .superpowers/sdd/ folder at the repository root was not opened.
- A full humanizer read of each page: the prose finding rests on em dash counts, a tell-word grep and sampled passages, not a sentence-by-sentence pass.

## Layout and hygiene

### Facts

- Top-level tracked: .dockerignore, .gitattributes, .gitignore, .github/ (4 files), CMakeLists.txt, CMakePresets.json, Dockerfile, Doxyfile, LICENSE, sushi-module.toml, cli/ (28), cmake/ (20), docs/ (30), examples/ (8), include/ (85), src/ (21), tests/ (86), tools/ (1). 292 tracked files in total.
- Top-level on disk but ignored or untracked: build/ (325 MB), build-asan/ (17 KB, two dotfiles), venv/ (29 MB), .superpowers/ (293 KB), .scratch_syntax/ (empty), four zero-byte CXX-DetectStdlib-*.h files.
- Absent from the root: README.md, AGENTS.md, CLAUDE.md, .clang-format, .editorconfig. docs/README.md and docs/CLAUDE.md exist instead.
- Single-module layout: include/SushiRuntime/<area>/ and src/<area>/ (areas: core, distributed, execution, graph, interop, memory, scheduler, topology; include also has api/). tests/ holds functional/{unit,integration,regression}, benchmark/{performance,stress}, common/, package/.
- C++ version: CMakeLists.txt:32 `project(SushiRuntime VERSION 0.3.0 LANGUAGES CXX)`; include/SushiRuntime/version.hpp:51 hand-written "0.3.0", guarded by a configure-time equality check at CMakeLists.txt:64-76. These agree.
- CLI version: cli/pyproject.toml:7 `version = "1.0.0"` and cli/sushiruntime/__init__.py:3 `__version__ = "1.0.0"`. Doxyfile:10 `PROJECT_NUMBER = "v1 (unreleased)"`. sushi-module.toml carries no version. No package.json.
- Git tags: v0.2.0 (annotated, 2026-08-17) and v0.3.0 (annotated, 2026-09-15). 46 commits on main since v0.3.0, one of them breaking (b5acd1a `feat(cli)!`).
- Changelogs: docs/reference/CHANGELOG.md (14 lines, `## Unreleased` with 10 entries, none over 240 characters) and docs/CHANGELOG.md (668 lines, Keep-a-Changelog form, `[Unreleased]` with 23 entries plus full `[0.3.0]` and `[0.2.0]` sections).
- Remote: origin https://github.com/SushiSystems/SushiRuntime.git; sushi-module.toml gives repo = https://github.com/sushisystems/sushiruntime.git (same repository, lower case). Name matches the product.
- Branches: main (current), feature/sushicore_help_screen (local only, fully merged into main, tip d41f2c6), origin/main. main is 1 commit ahead of origin/main (b5acd1a, dated 2026-10-04).
- git status: clean, no modified and no untracked files. Ignored on disk: .superpowers/, 4 CXX-DetectStdlib-*.h, build/, build-asan/, venv/, cli/build/, cli/.pytest_cache/, cli/sushiruntime_cli.egg-info/, cli/config.local.toml(.bak), __pycache__ dirs, docs/api-site/html/, docs/api-site/doxygen-warnings.log.
- CI: one workflow, .github/workflows/ci.yml, triggers push to main, pull_request and a nightly cron. Jobs: functional, sanitizers (TSan), cli (pyflakes + pytest), docs (doxygen), benchmark (nightly only). All run on ubuntu-latest.
- tools/: one file, tools/packaging/verify_installed_package.py (packaging oracle that builds a consumer against an installed tree and runs it in a scrubbed environment). None of the four standard checkers exist.
- .gitignore (51 lines) covers: build/, build-asan/, build-tsan/, bin/, lib/, out/, dependencies/, docs/html, docs/api-site/html/ and its warnings log, venv/, .vscode/, .vs/, swap files, cli/config.local.toml(.bak), cli/build/, cli/*.egg-info/, __pycache__/, *.py[cod], CMake scratch (CMakeFiles/, CMakeCache.txt, compile_commands.json, _deps/ ...), html/, latex/, CXX-DetectStdlib-*.h.
- .gitignore has no rule for .superpowers/ (ignored only by its own nested .superpowers/sdd/.gitignore), .scratch_syntax/, .pytest_cache/, .venv/ or imgui.ini. docs/api-site/.gitignore duplicates two root rules.
- No tracked binaries, images, egg-info, imgui.ini or CMake discovery JSON. Largest tracked file is include/SushiRuntime/api/graph/graph.hpp at 201,982 bytes (a source header).
- LICENSE (12,548 bytes) states Apache License v2.0 with LLVM Exceptions. docs/LICENSE (11,568 bytes) is a second, different licence file.
- docs/ holds 12 loose UPPER_CASE files at its top level, plus agent/reports/ (2), api/ (1), reference/ (CHANGELOG only), slop/ (7), superpowers/specs/ (6), api-site/ (.gitignore). No getting_started/, architecture/, modules/, guides/, design/ or archive/ folder.

### Corrections from review

- .gitignore has 47 lines, not 51. The CXX-DetectStdlib-*.h rule is at line 47 (F12 cites line 51) and dependencies/ is at line 11 (F16 cites line 9). The conclusions do not change.
- ci.yml runs-on lines are 15, 47, 87, 122 and 154; the audit gives 123 and 155 for the last two.
- The git status fact omits the stash: git stash list shows two entries dated 2026-06-20 on refactor/v1-toolchains-acpp, a branch that no longer exists. Each changes one line of cli/sushiruntime_cli.egg-info/SOURCES.txt, so no real work is at risk.
- Recomputed and correct: 292 tracked files with the stated per-folder counts; 46 commits since v0.3.0 with one breaking subject; changelog sizes 668 and 14 lines with 23 and 10 Unreleased entries; LICENSE 12,548 and docs/LICENSE 11,568 bytes; largest tracked file graph.hpp at 201,982 bytes; only CMakePresets.json among tracked binary or JSON types.

### Findings

#### L1. [high] Two changelogs are live at once and both carry an Unreleased section

Evidence: D:/Projects/sushiruntime/docs/CHANGELOG.md (668 lines; line 9 `## [Unreleased]` with 23 entries, line 42 `## [0.3.0] — 2026-09-15`, line 122 `## [0.2.0] — 2026-08-17`) and D:/Projects/sushiruntime/docs/reference/CHANGELOG.md (line 3 `## Unreleased`, 10 entries dated 2026-09-21 to 2026-10-04). The two Unreleased lists hold different entries, so neither is the full record since v0.3.0.

Recommendation: Make docs/reference/CHANGELOG.md the only changelog: merge the 23 Unreleased entries from docs/CHANGELOG.md into it, move the 0.3.0 and 0.2.0 sections to docs/archive/changelog/v0.3.0.md and v0.2.0.md, then remove docs/CHANGELOG.md (owner approval needed for the deletion).

Review: confirmed. Both files exist: docs/CHANGELOG.md is 668 lines with 23 entries under [Unreleased] (lines 10-41), docs/reference/CHANGELOG.md has 10 entries under Unreleased. docs/CONTRIBUTING.md:230 still points contributors at docs/CHANGELOG.md, which the recommendation should also fix. The fix is one-file-one-job, brick-shaped.

#### L2. [high] CLI declares version 1.0.0 while the product is 0.3.0

Evidence: D:/Projects/sushiruntime/cli/pyproject.toml:7 `version = "1.0.0"`; D:/Projects/sushiruntime/cli/sushiruntime/__init__.py:3 `__version__ = "1.0.0"`; D:/Projects/sushiruntime/Doxyfile:10 `PROJECT_NUMBER = "v1 (unreleased)"`; against D:/Projects/sushiruntime/CMakeLists.txt:32 `VERSION 0.3.0`, version.hpp:51 "0.3.0" and tag v0.3.0. The skill reserves v1.0.0 for the owner's decision and requires one source of truth.

Recommendation: Derive or check the CLI version and the Doxygen PROJECT_NUMBER against the CMake project version (the existing configure-time check at CMakeLists.txt:64-76 is the pattern to extend), and drop the duplicated literal in __init__.py in favour of package metadata.

Review: confirmed. cli/pyproject.toml:7 and cli/sushiruntime/__init__.py:3 say 1.0.0, Doxyfile:10 says v1 (unreleased), CMakeLists.txt:32 and version.hpp:51 say 0.3.0. The recommendation is sound but leaves one choice to the owner unstated: whether the CLI package shares the product version or keeps its own.

#### L3. [high] None of the four standard checkers exist under tools/

Evidence: `git ls-files tools` returns only tools/packaging/verify_installed_package.py. Missing: tools/documentation/check_source_comments.py, tools/documentation/check_docs_layout.py, tools/documentation/check_changelog.py, tools/layering/check_layering.py.

Recommendation: Add the four checkers with the project-tools contract (Python 3.11 stdlib, rule table, --report, --rule), shaped the same as in the sibling repositories.

Review: confirmed. git ls-files tools returns only tools/packaging/verify_installed_package.py; none of the four project-tools checkers exist.

#### L4. [high] CI departs from the required shape: Linux only, raw cmake/ctest, no checkers, no release workflow, one job cannot fail

Evidence: D:/Projects/sushiruntime/.github/workflows/ci.yml: every job is `runs-on: ubuntu-latest` (lines 15, 47, 87, 123, 155), no Windows job; lines 35-43, 68-83, 171-180 invoke cmake and ctest directly and line 118 invokes pytest directly rather than the `sr` CLI; line 114 `python -m pyflakes sushiruntime || true` can never fail; lines 139-143 report Doxygen warnings without failing; the single file is named ci.yml and mixes push, pull_request and schedule; there is no tag-triggered release.yml and no packaging job although tools/packaging/verify_installed_package.py exists; line 103 pins the SushiStack checkout to `ref: main`, so CI is not reproducible.

Recommendation: Split into push.yml and release.yml, add a Windows leg, route build and test through `sr`, run the four checkers, remove `|| true`, and pin the sushicore source to a tag or take it from the index the changelog says it is published to.

Review: confirmed. Read ci.yml in full. All cited defects are present. Two line numbers are off by one (runs-on for docs is line 122 and for benchmark line 154). The audit missed that the cli job cannot work as written, see missed item 1.

#### L5. [high] Root has no README.md, AGENTS.md or CLAUDE.md

Evidence: `ls README.md AGENTS.md CLAUDE.md` in D:/Projects/sushiruntime fails for all three. The front door lives at docs/README.md and the agent instructions at docs/CLAUDE.md, where neither GitHub nor an agent entering the root finds them.

Recommendation: Add a root README.md (what it is, one build line, link to docs/README.md) and move docs/CLAUDE.md to the root with an AGENTS.md beside it, naming the skills the repository follows.

Review: confirmed. Root listing has no README.md, AGENTS.md or CLAUDE.md; docs/README.md and docs/CLAUDE.md are tracked.

#### L6. [high] The CI cli job installs sushicore from a path that no longer exists

Evidence: D:/Projects/sushiruntime/.github/workflows/ci.yml:100-109 checks out SushiSystems/SushiStack at main and runs `pip install -e ./.sushistack/sushicore`. SushiStack no longer carries that folder: `git ls-files` in D:/Projects/sushistack returns 0 paths under sushicore/, `git ls-tree origin/main` lists no sushicore entry, and commit 016c081 is `refactor!: move sushicore out of this repository`. sushicore now lives in D:/Projects/sushicore (remote SushiSystems/SushiCore, version 0.7.0). The comment at ci.yml:97 (`published to no index`) also contradicts docs/reference/CHANGELOG.md:9 (`Took sushicore from PyPI`). The job state on GitHub was not checked; no network call was made.

Recommendation: Install the CLI package itself (`pip install -e cli`), so sushicore resolves from the index through the `sushicore>=0.7.0` bound in cli/pyproject.toml:14, and delete the SushiStack checkout step. This also makes CI exercise the declared bound, which it does not today.

Review: found by review.

#### L7. [medium] A release is due and one commit is unpushed

Evidence: 46 commits since v0.3.0 (tagged 2026-09-15); b5acd1a `feat(cli)!: provision this checkout with sr setup, without hub` is breaking and is the one commit ahead of origin/main; docs/CHANGELOG.md:11 opens a `### Breaking` block under Unreleased; 33 Unreleased entries across the two changelogs exceeds the 15-entry threshold. Pre-1.0 a breaking change means MINOR.

Recommendation: Next release: v0.4.0 (minor, breaking). Propose it to the owner once F1 and F2 are settled, and push b5acd1a when the owner says so.

Review: confirmed. 46 commits since v0.3.0, b5acd1a is the only subject with '!' and the only commit ahead of origin/main, 23 + 10 = 33 Unreleased entries. v0.4.0 follows from the pre-1.0 table. docs/CHANGELOG.md:11 also holds an older Breaking entry dated 2026-09-17.

#### L8. [medium] docs/ does not follow the documentation architecture

Evidence: Loose manual pages at D:/Projects/sushiruntime/docs/: ARCHITECTURE.md, CLI_GUIDE.md, DISTRIBUTED_GUIDE.md, INTEGRATION.md, INTRODUCTION.md, CODE_OF_CONDUCT.md, SECURITY.md. No docs/getting_started/, architecture/, modules/, guides/, design/ (so no REMAINING_WORK.md), archive/, DOCUMENTATION_STYLE_GUIDE.md, reference/GLOSSARY.md or reference/KNOWN_ISSUES.md. Agent specs and plans sit in docs/superpowers/specs/ (6 files) instead of docs/agent/specs and docs/agent/plans; docs/agent/ has only reports/.

Recommendation: Run the documentation build-out as its own approved task: place each page with the five-question rule, move docs/superpowers/specs/* into docs/agent/specs and docs/agent/plans, and create the missing reference and design files.

Review: confirmed. git ls-files docs shows the loose pages and the missing folders and files as stated; docs/agent holds only reports/ (2 files), docs/superpowers/specs holds 6.

#### L9. [medium] A tracked folder named docs/slop holds the backlog and design documents

Evidence: D:/Projects/sushiruntime/docs/slop/ tracks 7 files: BACKBONE_WAVE_STATUS.md, CODE_AND_ARCHITECTURE_REVIEW.md, ENGINE_BACKBONE_REFACTOR.md, PHYSICS_SUBSTRATE_IMPLEMENTATION.md, PHYSICS_SUBSTRATE_REQUIREMENTS.md, SIMULATION_ENGINE_SUBSTRATE_PLAN.md, TODO.md. The folder name is not in the architecture and TODO.md is a backlog outside docs/design/REMAINING_WORK.md.

Recommendation: Sort each file into docs/design/ (open intent, with a status line), docs/archive/ (closed work) or docs/agent/reports/, fold TODO.md into docs/design/REMAINING_WORK.md, and remove the folder. Ask the owner before discarding any of them.

Review: confirmed. docs/slop tracks exactly the 7 files listed, TODO.md among them.

#### L10. [medium] Two different licence files are tracked

Evidence: D:/Projects/sushiruntime/LICENSE (12,548 bytes, opens `SushiRuntime is under the Apache License v2.0 with LLVM Exceptions`) and D:/Projects/sushiruntime/docs/LICENSE (11,568 bytes); `cmp` reports they differ from byte 1.

Recommendation: Keep one licence file at the root and remove docs/LICENSE. This matters for the pending licence change: both copies and every source header must move together.

Review: confirmed. Root LICENSE opens with the LLVM-exception banner; docs/LICENSE is plain Apache 2.0, so the two state different terms. The recommendation omits that docs/README.md:582 and docs/CONTRIBUTING.md:8 link to docs/LICENSE by relative path and break when it is removed.

#### L11. [medium] Doxyfile sits in the root, where the layout forbids it

Evidence: D:/Projects/sushiruntime/Doxyfile (tracked, 2,500 bytes); repository-layout lists `Doxyfile` among the entries forbidden in the root. ci.yml:135 and the `sr doxygen` command reference it by that path.

Recommendation: Move it under docs/api/ (beside mainpage.md) and update ci.yml and the CLI's doxygen service in the same commit.

Review: confirmed. Doxyfile is tracked in the root; cli/sushiruntime/services/project.py:615 and ci.yml:135 read it by that path. Its INPUT and OUTPUT_DIRECTORY are relative to the root, so a move also needs those paths or the working directory handled; the recommendation does not say so.

#### L12. [medium] Source and test trees use names the sibling layout does not

Evidence: D:/Projects/sushiruntime/src/ instead of source/; top-level D:/Projects/sushiruntime/examples/ (8 files, executables) instead of applications/; tests/functional/{unit,integration,regression} and tests/benchmark/{performance,stress} and tests/package/ instead of tests/{unit,integration,regression,common}/; umbrella header include/SushiRuntime/SushiRuntime.h is PascalCase with a .h extension where every other header is snake_case .hpp.

Recommendation: Decide with the owner whether to rename (src to source, examples to applications/examples, flatten tests/functional). The umbrella header is public interface, so a rename needs a deprecation path under api-stability.

Review: confirmed. src/ (21 files), examples/ (8), tests/functional/{unit 23, integration 41, regression 1}, tests/benchmark/{performance 12, stress 2}, tests/package (2) and include/SushiRuntime/SushiRuntime.h all present. Asking the owner before renaming public paths is the right shape.

#### L13. [medium] The sushicore bound moved to 0.7.0 without a changelog entry

Evidence: D:/Projects/sushiruntime/cli/pyproject.toml:14 declares `sushicore>=0.7.0`; commit b5acd1a changed that file. The last entry on the subject, docs/reference/CHANGELOG.md:12, says the bound was raised to 0.4.0, and neither 2026-10-04 entry (lines 13-14) names the new bound. The audit listed this under not_checked instead of reporting it.

Recommendation: Add one line to docs/reference/CHANGELOG.md for the raise to 0.7.0 (`cli/pyproject.toml`).

Review: found by review.

#### L14. [medium] CONTRIBUTING forbids changelog entries and denies the project has a version

Evidence: D:/Projects/sushiruntime/docs/CONTRIBUTING.md:227-230: `Changelog entries start at v1.0.0. The project has not yet been assigned a semantic version ... Do not add entries to CHANGELOG.md until the project cuts its v1.0.0 release.` The repository has tags v0.2.0 and v0.3.0, CMakeLists.txt:32 declares 0.3.0, and both changelogs carry Unreleased entries. The same lines link to docs/CHANGELOG.md, the file F1 proposes removing.

Recommendation: Rewrite that rule in the same change as F1: name docs/reference/CHANGELOG.md as the one changelog, state the one-line entry form, and drop the v1.0.0 gate.

Review: found by review.

#### L15. [medium] The root CMakeLists.txt carries logic the layout assigns to cmake/

Evidence: D:/Projects/sushiruntime/CMakeLists.txt:64-76 (version equality check with file(STRINGS) and a regex), lines 87-109 (package config and version file generation, install rules) and lines 117-125 (CPack settings). repository-layout allows the root file `project()`, options and `add_subdirectory`; no logic. The audit read the file only for its version lines and listed the rule as not assessed.

Recommendation: Move each block into its own function file under cmake/: the version check into a version module, the package files into the existing cmake/SushiRuntimeInstall.cmake, and CPack into a packaging module. The root file then holds project(), the includes and add_subdirectory. This edits build configuration, so it needs the owner's go-ahead.

Review: found by review.

#### L16. [low] CMake compiler-probe headers leak into the root

Evidence: D:/Projects/sushiruntime/CXX-DetectStdlib-footer-b8e62b.h, CXX-DetectStdlib-footer-eb53d1.h, CXX-DetectStdlib-header-39cc75.h, CXX-DetectStdlib-header-c2efdb.h, all zero bytes, dated 2026-08-30 and 2026-09-14. They are ignored by .gitignore line 51 (`CXX-DetectStdlib-*.h`), which hides the leak without stopping it.

Recommendation: Delete the four files and fix the cause: the probe runs with the source root as its working directory, so the configure step in the CLI should run from the build directory.

Review: confirmed. Four zero-byte files in the root with the stated dates. The ignore rule is at .gitignore line 47, not 51. The stated cause holds: cli/sushiruntime/services/project.py:515 runs configure with cwd=root.

#### L17. [low] Agent and tool leftovers on disk, not covered by the root .gitignore

Evidence: D:/Projects/sushiruntime/.superpowers/sdd/2026-09-21-device-solver-instrument/ holds 11 review-*.diff files (293 KB), ignored only by the nested .superpowers/sdd/.gitignore; D:/Projects/sushiruntime/.scratch_syntax/ is an empty directory from 2026-09-16 with no ignore rule; D:/Projects/sushiruntime/build-asan/ holds only .sushiruntime_dist and .sushiruntime_env.json from 2026-08-25; D:/Projects/sushiruntime/venv/ (29 MB) dates from 2026-06-14; cli/.pytest_cache/ is ignored by its own generated file only.

Recommendation: Remove .scratch_syntax/, the stale build-asan/ and, with the owner's consent, .superpowers/ and venv/. Add `.superpowers/`, `.scratch_syntax/`, `.pytest_cache/` and `.venv/` to the root .gitignore so scratch never depends on a nested ignore file.

Review: confirmed. Sizes and contents match (293K, 11 diff files, empty .scratch_syntax, two dotfiles in build-asan, 29M venv). Not mentioned: git stash holds two entries from 2026-06-20 on the deleted branch refactor/v1-toolchains-acpp, each a one-line egg-info change, which are leftovers of the same kind.

#### L18. [low] Merged local branch left behind

Evidence: `git branch --merged main` lists feature/sushicore_help_screen (tip d41f2c6, already an ancestor of main; `git log main..feature/sushicore_help_screen` is empty). It has no remote counterpart.

Recommendation: Delete the branch after the owner confirms; nothing on it is at risk.

Review: confirmed. git branch -a --merged main lists feature/sushicore_help_screen; no remote counterpart exists.

#### L19. [low] No .clang-format or .editorconfig in the root

Evidence: `ls .clang-format .editorconfig` in D:/Projects/sushiruntime fails for both; repository-layout lists both among the root entries. 85 headers and 21 sources have no committed formatting contract.

Recommendation: Add the two files shared by the sibling repositories.

Review: confirmed. Neither file is in the root listing; 85 tracked files under include/ and 21 under src/.

#### L20. [low] Ignore rules are duplicated and partly stale

Evidence: D:/Projects/sushiruntime/docs/api-site/.gitignore repeats root .gitignore lines 13-14 (`docs/api-site/html/`, `docs/api-site/doxygen-warnings.log`) and is the only tracked file in that folder; root .gitignore line 12 `docs/html` and line 9 `dependencies/` name paths that the changelog entry of 2026-10-04 says the CLI no longer uses (`stopped reading a repo-local dependencies folder`); D:/Projects/sushiruntime/.dockerignore lists `.gemini/`, a tool with no other trace in the tree.

Recommendation: Keep the rules in the root .gitignore only, drop docs/api-site/.gitignore, and remove the stale entries once it is confirmed nothing still writes those paths.

Review: confirmed. docs/api-site/.gitignore is the folder's only tracked file and repeats root lines 13-14; .dockerignore:8 lists .gemini/. dependencies/ is at .gitignore line 11, not 9. docs/CLI_GUIDE.md:324-325 confirms sr no longer reads a repo-local dependencies folder.

### Not checked

- Contents of build/ (325 MB), build-asan/ beyond its listing, venv/, cli/build/ and cli/*.egg-info/ were skipped as instructed; whether build/ holds anything worth keeping was not examined.
- Whether the v0.2.0 and v0.3.0 tags exist on the remote, and the state of CI runs on GitHub (red or green), were not checked; no network call was made.
- The seven documents under docs/slop/ and the six under docs/superpowers/specs/ were not read, so which are open intent and which are closed work is undetermined.
- The content difference between LICENSE and docs/LICENSE was established by size and cmp only; the texts were not compared clause by clause.
- Link and cited-path resolution inside docs/ was not checked (documentation dimension).
- File naming inside include/, src/ and tests/ was sampled with one pattern (uppercase in .hpp/.cpp/.h/.py names); one-symbol-per-header and the tier/layer table in cmake/ were not audited.
- CMakeLists.txt was read only for version lines; the 'no logic in the root CMakeLists' rule was not assessed. CMakePresets.json, Dockerfile and .github issue/PR templates were not read.
- Whether sushicore>=0.7.0 in cli/pyproject.toml matches what is published was not verified; the reference changelog still says the bound was raised to 0.4.0.
- Per-module README.md files: the repository is single-module, and the absence of area READMEs under include/ or src/ was not evaluated.
- Git history was not scanned for large or binary blobs that were once tracked and later removed.

## Code shape

### Facts

- HEAD is b5acd1a on D:/Projects/sushiruntime; 292 tracked files: 108 .cpp, 86 .hpp, 1 .h, 25 .py, 19 .cmake, 29 .md.
- Languages: C++17 (CXX_STANDARD 17, src/CMakeLists.txt:138) on SYCL for the library; Python for the `sr` CLI under cli/ and one tool under tools/packaging/; CMake. No TypeScript, no GLSL.
- Layout is single-library, not modules/<tier>/<module>: public headers in include/SushiRuntime/<module>/, sources in src/<module>/, all tests in root tests/. No module has its own README or tests/ folder.
- Top-level line counts (tracked source): include 85 files / 19,652 lines; src 21 / 8,491; tests 86 / 18,299; cli 24 / 2,740; cmake 19 / 1,397; examples 8 / 879; tools 1 / 160.
- Module sizes (include + src): api 21 headers / 8,449 lines, header-only; core 18 / 2,674 + 1 src / 45; distributed 9 / 1,544 + 7 / 2,673; execution 5 / 1,380 + 4 / 2,254; graph 5 / 1,497 + 2 / 1,018; interop 6 / 454 + 1 / 302; memory 4 / 832, header-only; scheduler 8 / 1,997 + 1 / 1,330; topology 7 / 636 + 4 / 681.
- Documented layer order (docs/ARCHITECTURE.md:43-72): api > execution > graph > scheduler > memory | topology > core, with distributed 'beside execution/scheduler'. The text says lower layers never depend up.
- Measured include edges: api -> core, execution, graph, interop, memory, topology; execution -> core, distributed, graph, memory, scheduler, topology; graph -> core, distributed, execution, memory, scheduler; scheduler -> core, distributed, execution, graph, memory, topology; distributed -> core, execution, graph; interop -> api, core, execution, memory; memory -> core, execution; topology -> core; core -> core only.
- Only core and topology are clean leaves. execution, graph, scheduler and distributed form one strongly connected group; memory and interop reach upward into execution and api.
- Twenty largest source files: api/graph/graph.hpp 3626; src/scheduler/scheduler.cpp 1330; api/graph/dynamic_graph.hpp 1226; src/execution/runtime_context.cpp 1123; tests/.../test_distributed_e2e.cpp 1088; src/graph/task_graph.cpp 853; src/distributed/distributed_coordinator.cpp 757; src/execution/execution_engine.cpp 722; tests/.../test_counting_sort.cpp 712; graph/node.hpp 685; src/distributed/worker_agent.cpp 683; scheduler/task_scheduler.hpp 627; cli/sushiruntime/services/project.py 617; execution/runtime_context.hpp 536; api/data/buffer.hpp 532; tests/.../test_dynamic_graph.cpp 520; src/distributed/tcp_transport.cpp 517; execution/execution_engine.hpp 499; scheduler/event_polling.hpp 498; tests/.../test_fixed_order_reduce.cpp 474.
- Test layout: tests/functional/unit (23 files), tests/functional/integration (41), tests/functional/regression (1 file, 3 tests), tests/benchmark/performance (12), tests/benchmark/stress (2), tests/common (3), tests/package (2); cli/tests holds 10 pytest files plus conftest. There is no tests/golden.
- Logging: one logger in include/SushiRuntime/core/logger.hpp with macros SR_LOG_ERROR/WARN/INFO/DEBUG (17/30/26/18 call sites in include+src). The macros take no category argument and no *_log_category.hpp exists. No TRACE or FATAL level.
- No std::cout, printf or std::cerr in include/ or src/. No TODO, FIXME, HACK or XXX in any C++ source. No @date tag anywhere.
- Every C++ file carries the Apache-2.0 license box with its file name on the first line; 16 of 195 C++ files carry an @file block and 8 carry @author.
- tools/ holds only tools/packaging/verify_installed_package.py; there is no tools/documentation/check_source_comments.py.
- sycl/sycl.hpp is included by 19 public headers across api, core, distributed, execution, graph, interop, scheduler and topology; hwloc appears in no public header.
- Module verdicts: core - needs targeted fixes; topology - needs targeted fixes; memory - needs targeted fixes; interop - needs targeted fixes; distributed - needs targeted fixes; scheduler - needs restructuring; graph - needs restructuring; execution - needs restructuring; api - needs restructuring; cli (Python) - needs targeted fixes; tests - needs targeted fixes; tools - needs restructuring (the checker set is absent).

### Corrections from review

- F3 and F8 state that include/SushiRuntime/execution/runtime_context.hpp:42-47 includes the six distributed headers unconditionally. They are inside `#ifdef SR_DISTRIBUTED` (opened at line 38, closed at line 48). Likewise src/scheduler/scheduler.cpp:45-46 (cited in F1) is inside the `#ifdef SR_DISTRIBUTED` at line 44. The execution -> distributed and scheduler -> distributed edges exist only in distributed builds.
- Log call sites in include+src are ERROR 15, WARN 28, INFO 24, DEBUG 16 once the macro definitions in core/logger.hpp are excluded; the audit's 17/30/26/18 counts the two #define lines per level.
- Top-level counts 'src 21 / 8,491', 'tests 86 / 18,299', 'examples 8 / 879' include CMakeLists.txt and other non-source files. Counting .cpp/.hpp/.h/.py/.cmake only: src 20 files / 8,303 lines, tests 83 / 17,888, examples 7 / 817. include 85 / 19,652, cli 24 / 2,740, cmake 19 / 1,397 and tools 1 / 160 match.
- F8: include/SushiRuntime/graph/node.hpp contains 6 SR_DISTRIBUTED occurrences, not 4.
- F16: 46 UPPER_SNAKE constexpr constants without K_ by my grep, not 63 (1 with K_ matches).
- F21: ordering assertions appear in 11 of 12 performance files, not 10; every one of the 12 holds at least 3 EXPECT/ASSERT calls.
- F24: `from __future__ import annotations` is missing in 8 tracked Python files, not 5; the audit omits cli/sushiruntime/__init__.py, cli/sushiruntime/__main__.py and cli/sushiruntime/services/__init__.py.
- F17: include/SushiRuntime/graph/node.hpp declares 10 struct/class types by grep, not 8.
- F10: the device_free_mem catch in src/distributed/worker_agent.cpp is at lines 491-494, not 493-496.
- Confirmed as stated: HEAD b5acd1a; 292 tracked files (108 .cpp, 86 .hpp, 1 .h, 25 .py, 19 .cmake, 29 .md); 16 files with @file and 8 with @author out of 195 C++ files, all 195 carrying the Apache box; the module include-edge table (recomputed, identical); test folder counts 23/41/1/12/2/3/2; 19 public headers including sycl/sycl.hpp; 19 catch (...) sites; six switch statements.

### Findings

#### C1. [high] execution, graph, scheduler and distributed form an include cycle that contradicts the documented layering

Evidence: docs/ARCHITECTURE.md:46-47 says lower layers never depend up. Upward edges: include/SushiRuntime/scheduler/task_scheduler.hpp:43-44 (scheduler -> execution/runtime_context.hpp, runtime_settings.hpp); include/SushiRuntime/scheduler/event_polling.hpp:41 and src/scheduler/scheduler.cpp:45 (scheduler -> graph/node.hpp); src/scheduler/scheduler.cpp:46 (scheduler -> distributed/distributed_coordinator.hpp); include/SushiRuntime/graph/task_graph.hpp:38 and src/graph/task_graph.cpp:38 (graph -> execution); include/SushiRuntime/execution/runtime_context.hpp:42-47 (execution -> six distributed headers) while src/distributed/worker_agent.cpp:40-42 includes graph and execution back. task_scheduler.hpp:92 also declares `friend class SushiRuntime::Graph::TaskGraph`.

Recommendation: Fix the layer order as a decision first (owner's call: module boundary), then break each upward edge with an interface owned by the lower layer. scheduler should see only Scheduler::Task and a settings struct it owns; graph should not include execution; execution should hold distributed behind an interface it declares. Add a layering checker under tools/ so the order is enforced.

Review: confirmed. Read docs/ARCHITECTURE.md:43-72, task_scheduler.hpp:43-44 and :92, event_polling.hpp:41, task_graph.hpp:38, task_graph.cpp:37-38, worker_agent.cpp:40-42; upward edges and the friend exist. Two cited edges are conditional, not unconditional: runtime_context.hpp:42-47 and scheduler.cpp:45-46 sit inside #ifdef SR_DISTRIBUTED (opened at runtime_context.hpp:38 and scheduler.cpp:44). The cycle holds without them. SOLID: the fix (interface owned by the lower layer, owner decides order first) is brick-shaped.

#### C2. [high] The scheduler downcasts Task to Graph::Node and reads graph fields in its dispatch path

Evidence: src/scheduler/scheduler.cpp:708-720: `if (task->is_graph_node()) { node = static_cast<SushiRuntime::Graph::Node*>(task); ... node->metadata.numa_affinity`; lines 723-724 branch on `node->get_execution_mode()` and `node->submits_device_work()`; lines 908-911 call `Graph::snapshot_widths(*node)`. include/SushiRuntime/scheduler/event_polling.hpp:41 includes graph/node.hpp for the same downcast. include/SushiRuntime/scheduler/ready_sink.hpp:36-44 describes the seam that is meant to keep this boundary one-way.

Recommendation: Move what the scheduler needs (NUMA affinity, inline-host flag, dispatch widths) onto the Scheduler::Task interface as virtuals or a small value struct, and remove the is_graph_node() type tag and both includes of graph/node.hpp from scheduler.

Review: confirmed. scheduler.cpp:708-724 and 908-911 read as quoted; event_polling.hpp:41 includes graph/node.hpp. Recommendation is brick-shaped, though 'virtuals on Task' adds a virtual call in the per-task dispatch path; the value-struct option is the safer of the two it offers.

#### C3. [high] RuntimeContext carries at least seven responsibilities

Evidence: include/SushiRuntime/execution/runtime_context.hpp:61-532: hardware probing (102), settings (126), transfer byte counters (137-149), queue ownership with three queue families (156-241), USM allocation and registry (287-386), NUMA topology queries (404-414), engine ownership and lifecycle (425-439), and the distributed role, buffer registry and liveness settings (447-511). src/execution/runtime_context.cpp is 1,123 lines. Six distributed headers are included at lines 42-47, so every consumer of the context compiles the distributed layer's types.

Recommendation: Split into bricks that RuntimeContext composes: a device/queue set, a USM allocator plus registry, a topology view, a transfer counter, and a distributed session held behind an interface. Ask the owner before fixing the public shape; later wave should start here.

Review: confirmed. Class and the 1,123-line source exist and the responsibilities listed are present. One evidence sentence is wrong: the six distributed includes at runtime_context.hpp:42-47 are guarded by #ifdef SR_DISTRIBUTED (line 38), so consumers compile them only in distributed builds. Recommendation composes bricks and defers the public shape to the owner.

#### C4. [high] API::Graph is one 3,300-line class that owns node registration, algorithms, run drivers and device plumbing

Evidence: include/SushiRuntime/api/graph/graph.hpp:271 to about 3600 is a single class. It holds 17 `add` overloads (454 onward), add_host (896), add_reduce (997), add_segmented_reduce (1301), add_prefix_scan (1492), add_counting_sort (1769), offload emitters (2146), the run/run_for/run_until/run_async/try_run drivers (2326-2552), drive_impl (2730), chain and scheduled runners (2773, 2837), late-count resolution (3049-3118), fault-record allocation (3193) and host/device slot registration (3328-3428). Adding a new primitive means editing this class.

Recommendation: Restructure: keep Graph as the registration surface, move each device primitive (reduce, segmented reduce, prefix scan, counting sort) into its own header as a free function or builder that emits nodes through a narrow Graph interface, and move the run drivers into a shared driver brick (see F5).

Review: confirmed. graph.hpp is 3,626 lines; class Graph opens at 271; 17 add overloads counted; add_host 896, add_reduce 997, add_segmented_reduce 1301, add_prefix_scan 1492, add_counting_sort 1769 all found. Recommendation is brick-shaped (one primitive per header through a narrow interface).

#### C5. [high] Exceptions are the public error mechanism, against the cpp-code-style error rule

Evidence: cpp-code-style: 'exceptions never cross a module's public surface'; failures outside the program return a status. include/SushiRuntime/api holds 52 `throw` sites (e.g. api/data/buffer.hpp:184, 214, 500; api/detail/device_fault.hpp:82, 85) throwing Core::SushiException (core/error.hpp:96), and src/ holds 36 more `throw`/SR_THROW_IF sites across modules (runtime_context.cpp 10, interop.cpp 8, tcp_transport.cpp 6, execution_engine.cpp 4). The scheduler carries exceptions between modules with std::current_exception (src/scheduler/scheduler.cpp:541). The audit mentions only where sycl::exception is caught (F9) and sentinel returns (F19).

Recommendation: Put the policy to the owner as one decision: either record in a design document that SushiRuntime's public surface throws SushiException and has try_* status twins, or schedule the conversion to Core::Result under api-stability. The module wave needs that answer before it touches error paths.

Review: found by review.

#### C6. [medium] Graph and DynamicGraph duplicate the run surface and the guarded drive

Evidence: include/SushiRuntime/api/graph/graph.hpp:2326-2552 and include/SushiRuntime/api/graph/dynamic_graph.hpp:237-334 each declare run(), run(steps), run(reuse), run(steps, reuse), run_for, run_until and four try_run overloads. The guard and catch ladder at graph.hpp:2698-2727 is repeated at dynamic_graph.hpp:1058-1088 with the same messages ('A step started with run_async() is still in flight...', 'unknown exception'); a third copy of the ladder is at include/SushiRuntime/api/graph/run_handle.hpp:262-281. dynamic_graph.hpp:1056 points at Graph::drive() in a comment instead of sharing it.

Recommendation: Extract one step-driver brick (the overload set plus the exception-to-Error conversion) that both graphs and RunHandle use; each graph supplies only its drive_impl.

Review: confirmed. Same catch ladder and the same two messages at graph.hpp:2698-2727 and dynamic_graph.hpp:1054-1088; third ladder at run_handle.hpp:276-279; the 'See Graph::drive()' comment is at dynamic_graph.hpp:1057. One shared driver brick is the right fix.

#### C7. [medium] Message handling is a switch over MsgType in two files that every new message must extend

Evidence: src/distributed/distributed_coordinator.cpp:159 `switch (type)` with inline case bodies (REGISTER_HELLO at 161-193, BUFFER_REQUEST at 194 onward); src/distributed/worker_agent.cpp:284 `switch (type)` in WorkerAgent::Impl::on_message. A new message type needs an enumerator in include/SushiRuntime/distributed/protocol.hpp:99 plus an edit in both switches. Same pattern for USM kinds at include/SushiRuntime/api/data/buffer.hpp:99 and 121 (Residency) and src/distributed/in_process_transport.cpp:178 (FaultAction).

Recommendation: Register one handler per message type in a table keyed by MsgType, each handler in its own function or file, so a new message adds a file. For Residency a three-entry table of allocator functions is enough.

Review: confirmed. switch (type) at distributed_coordinator.cpp:159 and worker_agent.cpp:284; the other switches at buffer.hpp:99/121 and in_process_transport.cpp:178 exist. core/error.hpp:65 is a sixth switch the finding does not list (it is in the audit's own scan output). Handler table per MsgType is brick-shaped.

#### C8. [medium] Lower layers reach upward: memory includes execution, interop includes api

Evidence: include/SushiRuntime/memory/usm_allocation_owner.hpp:29 includes execution/runtime_context.hpp and holds `std::shared_ptr<Execution::RuntimeContext>` (line 47); memory sits two layers below execution in docs/ARCHITECTURE.md:63-66. include/SushiRuntime/interop/host_export.hpp:34 and include/SushiRuntime/interop/interop.hpp:36 include api/data/buffer.hpp while include/SushiRuntime/api includes interop once, so api and interop depend on each other. interop is absent from the layer diagram.

Recommendation: Move UsmAllocationOwner to execution (or give memory an allocator interface that RuntimeContext implements). Place interop in the documented order and remove one direction of the api/interop edge.

Review: confirmed. usm_allocation_owner.hpp:29 includes execution/runtime_context.hpp and :47 takes shared_ptr<Execution::RuntimeContext>; interop/host_export.hpp:34 and interop.hpp:36 include api/data/buffer.hpp; api/runtime/runtime.hpp:34 includes interop back. Fix offers two options without choosing; acceptable since it is a boundary decision.

#### C9. [medium] The 'optional' distributed layer is woven into five other modules by preprocessor

Evidence: docs/ARCHITECTURE.md:70-71 says distributed 'is invisible when the flag is off'. SR_DISTRIBUTED conditionals appear in include/SushiRuntime/graph/node.hpp (4, including the include at 41-43), src/graph/task_graph.cpp (6), src/execution/runtime_context.cpp (5), include/SushiRuntime/scheduler/event_polling.hpp (3), include/SushiRuntime/scheduler/task_scheduler.hpp (3), src/scheduler/scheduler.cpp (3), src/execution/execution_engine.cpp (2) and six api headers. Meanwhile include/SushiRuntime/execution/runtime_context.hpp:42-47 includes the distributed headers unconditionally.

Recommendation: Give graph, scheduler and execution one offload seam (an interface with a null implementation) and let distributed implement it, so the flag selects a link-time brick instead of changing struct layouts in public headers.

Review: confirmed. SR_DISTRIBUTED counts recomputed: task_graph.cpp 6, runtime_context.cpp 5, event_polling.hpp 3, task_scheduler.hpp 3, scheduler.cpp 3, execution_engine.cpp 2, six api headers. Two errors inside the finding: node.hpp has 6 occurrences, not 4, and the last sentence is false, runtime_context.hpp:42-47 is inside #ifdef SR_DISTRIBUTED. The defect (flag changes public struct layouts) stands; the null-implementation seam is brick-shaped.

#### C10. [medium] sycl types are part of the public surface of eight modules

Evidence: `#include <sycl/sycl.hpp>` at include/SushiRuntime/api/data/buffer.hpp:33, api/graph/graph.hpp:41, core/event_timestamps.hpp:27, core/sycl_compat.hpp:28, distributed/kernel_registry.hpp:30, execution/runtime_context.hpp:29, execution/execution_engine.hpp:30, graph/task_graph.hpp:32, scheduler/task.hpp:30, scheduler/task_scheduler.hpp:37, scheduler/event_polling.hpp:34, topology/hw_discovery.hpp:27 and seven more. RuntimeContext returns `sycl::queue&` (runtime_context.hpp:156, 176, 205, 241, 257). sycl::exception is caught in api headers (graph.hpp:2714) rather than at a module boundary.

Recommendation: SYCL is the substrate, so the api layer exposing it is likely intended; confirm that with the owner and record it in a design document. Below api, limit sycl to the headers that need it: core, topology and scheduler public headers should not require it, and core/event_timestamps.hpp belongs with the scheduler or execution code that uses it.

Review: confirmed. 19 public headers include sycl/sycl.hpp (recounted); the listed paths exist. The audit's companion fact 'hwloc appears in no public header' holds for includes and types (only comments and a bool named hwloc_initialized at topology/numa_mapper.hpp:65).

#### C11. [medium] Failures swallowed with neither a status nor a log

Evidence: src/topology/hw_discovery.cpp:157 `} catch (...) {}` around the PCI vendor-string parse; include/SushiRuntime/core/event_timestamps.hpp:58-61 and 68-72 return false or substitute submit_ns silently; src/distributed/worker_agent.cpp:493-496 returns 0 from device_free_mem() on any exception; src/interop/interop.cpp:114-117 and 153-156 swallow release failures in destructors with only a comment; include/SushiRuntime/api/graph/run_handle.hpp:303-306 'Deliberately swallowed' in settle().

Recommendation: Each site should log one WARN naming the operation, the value and the fallback (destructors included, since logging does not throw past noexcept if guarded), or return a status. The interop and run_handle cases are defensible as noexcept destructors but still need the log line.

Review: confirmed. hw_discovery.cpp:157 `} catch (...) {}`; event_timestamps.hpp:58-61 and 68-72; interop.cpp:114-117 and 153-156; run_handle.hpp:303-306 all read as described. worker_agent.cpp line range is 491-494, not 493-496. event_timestamps returns a bool status, so it belongs under F19 more than here.

#### C12. [medium] catch (...) sites log without the cause

Evidence: src/distributed/worker_agent.cpp:459-461 `SR_LOG_ERROR("Worker kernel for OpID {} threw.", ...)` with no exception text; src/scheduler/scheduler.cpp:538-540 `"poll_events threw; aborting the current run."`; src/scheduler/scheduler.cpp:915-917 `"Task '{}' threw; aborting the current run."`; include/SushiRuntime/scheduler/event_polling.hpp:401 `catch (...) { SR_LOG_WARN("Stall-drive wait() threw; treating event as complete."); }` on one line; src/topology/hw_discovery.cpp:219-222; src/topology/numa_mapper.cpp:52 `SR_LOG_ERROR("Failed to initialize hwloc topology.")` states no value and no consequence.

Recommendation: Add a `catch (const std::exception&)` arm that logs what() before the catch-all, as src/execution/chain_run.cpp:78-85 and src/distributed/worker_agent.cpp:622-633 already do, and make every WARN/ERROR state the values and what the program does next.

Review: confirmed. worker_agent.cpp:459, scheduler.cpp:538 and 915, hw_discovery.cpp:219 are catch-all sites whose log line carries no cause. Mitigation the audit omits: scheduler.cpp:541 calls record_run_error(std::current_exception()), so the cause reaches the caller as a status; only the log line is thin there.

#### C13. [medium] Logger has no categories and several call sites break the level rules

Evidence: include/SushiRuntime/core/logger.hpp:357-372 defines SR_LOG_ERROR/WARN/INFO/DEBUG with no category parameter; no `*_log_category.hpp` file exists in any module. Levels are OFF/ERR/WARN/INFO/DEBUG (logger.hpp:50-57): no TRACE, no FATAL. Per-task log in the dispatch path: src/scheduler/scheduler.cpp:705 `SR_LOG_DEBUG("Executing task: {}", ...)`. Per-node logs: src/graph/task_graph.cpp:291, 362, 445, 670. Per-step logs: src/execution/execution_engine.cpp:475 and 614 (the comment at 614 admits 'per-step'). logger.hpp also defines SR_THROW_IF (line 392), which is error handling, not logging.

Recommendation: Add a category argument and one K_<MODULE>_LOG_CATEGORY per module; add TRACE and move the per-task, per-node and per-step lines to it or to counters; move SR_THROW_IF to core/error.hpp. This is a public-macro change, so it needs the owner's approval.

Review: confirmed. logger.hpp:50-57 has OFF/ERR/WARN/INFO/DEBUG only; macros at 357-372 take no category; no *log_category* file in the tree; SR_THROW_IF at 392; the per-node and per-step DEBUG lines at the cited task_graph.cpp and execution_engine.cpp lines exist. Fix is brick-shaped and correctly flagged as an owner decision.

#### C14. [medium] 179 of 195 C++ files have no @file block and 187 have no @author

Evidence: Only 16 files contain `@file`: include/SushiRuntime/SushiRuntime.h, api/fluent.hpp, core/delegate.hpp, core/export.hpp, core/resource_liveness.hpp, core/span.hpp, core/strong_id.hpp, version.hpp, the six interop headers, src/interop/interop.cpp and tests/functional/unit/test_interop_vocabulary.cpp. Example of the common shape: include/SushiRuntime/scheduler/ready_sink.hpp:1-24 goes from the license box straight to `#pragma once`. All 7 examples, all src files except interop.cpp, and all tests but one lack it.

Recommendation: Add the three-tag file block to every file in one mechanical pass, and add tools/documentation/check_source_comments.py so it stays true.

Review: confirmed. Recounted: 16 of 195 C++ files contain @file, 8 contain @author; the list of 16 matches exactly (it includes interop/detail/bindless_detection.hpp among 'the six interop headers').

#### C15. [medium] Design rationale and history live in source as long comment blocks and stacked // paragraphs

Evidence: 778 runs of two or more consecutive `//` lines across 127 files: include 185 runs in 29 files, src 179 in 18, tests 386 in 73, examples 28 in 7. Worst: include/SushiRuntime/api/graph/graph.hpp (59 runs), src/scheduler/scheduler.cpp (48), api/graph/dynamic_graph.hpp (42), src/distributed/distributed_coordinator.cpp (25). Longest run is 17 lines at tests/functional/integration/test_steady_state_allocation.cpp:51. About 190 Doxygen blocks in include+src exceed eight lines (44 in graph.hpp, 13 in runtime_context.hpp, 11 in graph/task_graph.hpp); include/SushiRuntime/core/logger.hpp:75-91 is a 16-line shutdown-protocol essay and scheduler/ready_sink.hpp:36 onward explains why the seam exists. History wording: logger.hpp:99 'Was atomic; the atomic gave a false sense of safety', src/scheduler/scheduler.cpp:480 'This branch used to be', scheduler.cpp:711 'dynamic_cast here was the one hot-path RTTI query left'. Separator lines at tests/functional/unit/test_distributed.cpp:330 and 338.

Recommendation: Move the reasoning into docs/design/ documents and cite them by path; cut each block to @brief plus rule-bearing tags; keep single-line invariants only. The same applies to cli/sushiruntime/services/project.py:26-60 and 466-480, where `#` paragraphs of 30+ lines explain ASan and GPU-backend history.

Review: confirmed. My own run counter gives include 182 runs/29 files, src 173/18, tests 360/73, examples 21/7 against the audit's 185/179/386/28; file counts match, run counts differ by under 8% from a different comment-line definition. History wording at logger.hpp:99 and scheduler.cpp:480, and separators at test_distributed.cpp:330 and 338, are present. The 190 over-length Doxygen blocks were not recounted.

#### C16. [medium] Naming and shape rules of cpp-code-style are broken consistently across modules

Evidence: Interfaces carry an I prefix: include/SushiRuntime/distributed/i_transport.hpp:41 ITransport, i_offload_policy.hpp:56, execution/execution_engine.hpp:167 IExecutionEngine, memory/allocation.hpp:38, scheduler/rebalancer_interfaces.hpp:39/72/93, topology/i_topology_provider.hpp:44/78. Constants are not K_-prefixed: 63 constexpr constants versus 1 with K_, e.g. api/detail/device_fault.hpp:39 DEVICE_FAULT_WORDS, api/detail/fixed_order_reduce.hpp:49 REDUCE_TILE, core/logger.hpp ACTIVE_LOG_LEVEL. Internal namespace is `detail` in 15 files and `Detail` in 5. Enumerators are UPPER_SNAKE: distributed/protocol.hpp:99 REGISTER_HELLO, scheduler/task.hpp:46 `enum class ExecutionMode { KERNEL, HOST }`, core/logger.hpp:52-56. Enums without a sized underlying type: api/data/buffer.hpp:85, api/graph/graph.hpp:2560, core/common.hpp:39, distributed/in_process_transport.hpp:41, execution/runtime_context.hpp:447, execution/runtime_settings.hpp:42, scheduler/task.hpp:46. Classes list private members first: core/logger.hpp:92-94 and five others.

Recommendation: Renaming public interfaces, enumerators and constants is an API break; put it to the owner as one versioned rename wave under api-stability. Sized enums, the Detail spelling and the public-first order can go in without a break.

Review: confirmed. Nine I-prefixed interfaces found at the cited lines; `namespace detail` in 15 files and `Detail` in 5 (recounted); the seven unsized enums exist at the cited lines; LogType enumerators are UPPER_SNAKE. My grep finds 46 UPPER_SNAKE constexpr constants against 1 K_-prefixed, not 63; the conclusion is unchanged.

#### C17. [medium] Several headers hold many primary types, and source file names do not match their headers

Evidence: include/SushiRuntime/distributed/protocol.hpp holds 11 types (GlobalBufferID 71, BufferHandle 85, FrameHeader 115, ByteWriter 130, ByteReader 168 and six message structs 234-419). include/SushiRuntime/graph/node.hpp holds 8 (SmallEventVector 62, SuccessorNode 151, RegionLink 166, NodeProfiling 238, OffloadState 253, NodeExtensions 308, Node 324, NodeWidths 671). include/SushiRuntime/core/run_report.hpp holds 7 (41-155). include/SushiRuntime/execution/execution_engine.hpp holds CompiledGraph 67, ExecutionMetrics 153, the interface IExecutionEngine 167 and its implementation ExecutionEngine 361 in one file. scheduler/rebalancer_interfaces.hpp holds three interfaces. Name mismatches: src/scheduler/scheduler.cpp implements task_scheduler.hpp; src/distributed/offload_policy.cpp implements i_offload_policy.hpp; src/core/version.cpp implements include/SushiRuntime/version.hpp, which sits outside core/.

Recommendation: Split to one primary type per header named after it, with interface and implementation in separate files; rename scheduler.cpp to task_scheduler.cpp and offload_policy accordingly. Header moves change include paths, so stage them with forwarding headers.

Review: confirmed. protocol.hpp holds 11 types, run_report.hpp 7, execution_engine.hpp holds interface and implementation; node.hpp has 10 struct/class declarations by my grep against the audit's 8. src/scheduler/scheduler.cpp, src/distributed/offload_policy.cpp and src/core/version.cpp mismatches confirmed by directory listing. Forwarding headers for staged moves is a sound, brick-shaped plan.

#### C18. [medium] Large implementations live in public headers

Evidence: include/SushiRuntime/scheduler/event_polling.hpp is 498 lines of inline polling logic including a try/catch at 400-401; include/SushiRuntime/memory/epoch_reclamation.hpp is 416 lines; include/SushiRuntime/core/logger.hpp is 400 lines containing AsyncLogger (92) and SyncLogger (190) with a worker thread, queue and condition variable in the header and includes <thread>, <queue>, <sstream>, <condition_variable> for every consumer; include/SushiRuntime/scheduler/task_scheduler.hpp:155 nests SubmissionQueue and line 577 WorkerTimeSplit. scheduler has 8 headers / 1,997 lines against one .cpp.

Recommendation: Move non-template bodies into src/<module>/<same_name>.cpp; keep the logger header to the macros, the level enum and the formatter declaration.

Review: confirmed. event_polling.hpp carries inline logic with the try/catch near 400; logger.hpp holds AsyncLogger and SyncLogger in the header; src/scheduler has one .cpp and src/memory is empty. Line totals were taken from the audit's facts, not recounted per file.

#### C19. [medium] Sentinel returns stand in for a status

Evidence: src/execution/runtime_context.cpp:767 and 771 `return -1` from context_index_for_ptr (declared runtime_context.hpp:271); src/execution/chain_plan.cpp:78 `return -1`; include/SushiRuntime/api/data/buffer.hpp:117-127 malloc_residency_on returns nullptr on failure and on an out-of-range enum; src/distributed/worker_agent.cpp:495 returns 0 for 'unknown'; core/event_timestamps.hpp:51 returns bool for 'failed for some reason'. core/result.hpp and core/error.hpp already provide Result and Errc.

Recommendation: Return std::optional or Core::Result with an Errc at these sites; the later wave should grep `return -1` and `return nullptr` in src/execution and api/data.

Review: confirmed. runtime_context.cpp:767 and 771 and chain_plan.cpp:78 are `return -1`; event_timestamps.hpp:51 returns bool; worker_agent.cpp returns 0 at 493. buffer.hpp:117-127 was not opened.

#### C20. [medium] Test layout differs from the testing rules and whole areas have no unit test

Evidence: Tests sit under tests/functional/{unit,integration,regression} and tests/benchmark/{performance,stress}, not beside modules and not at tests/integration, tests/regression, tests/benchmark. tests/functional/regression/test_regression.cpp is the only regression file and holds 3 tests in one file named after no defect. execution has no unit test: nothing includes execution/chain_plan.hpp, chain_run.hpp or runtime_settings.hpp. api has no unit tests at all (41 integration files cover it through fluent.hpp). core has unit tests only for delegate, logger and ref_counted; none named for result.hpp, error.hpp, strong_id.hpp, bit.hpp, profiling_ledger.hpp, resource_region.hpp. TcpTransport is exercised only by tests/functional/integration/test_distributed_e2e.cpp. interop has one vocabulary unit test and one integration test. scheduler/ready_sink.hpp and memory/usm_allocation_owner.hpp are included by no test.

Recommendation: Decide with the owner whether to move tests to the yardstick layout; independently add unit tests for execution (ChainPlan, ChainRun), core Result/Error/StrongId, and a loopback unit test for TcpTransport. Split test_regression.cpp into one file per defect.

Review: confirmed. tests/ holds functional/ and benchmark/ only; the 23 unit files listed contain none for execution, api, Result, Error, StrongId; chain_plan.hpp, chain_run.hpp, runtime_settings.hpp, ready_sink.hpp and usm_allocation_owner.hpp are included by no test; tcp_transport.hpp only by test_distributed_e2e.cpp; test_regression.cpp has 3 TEST entries.

#### C21. [medium] Benchmarks pass or fail, and functional tests depend on wall-clock time

Evidence: The testing rule says a benchmark never passes or fails. EXPECT/ASSERT ordering checks appear in 10 of 12 files under tests/benchmark/performance (3 each in test_graph_compile_perf.cpp, test_profiling_queue_perf.cpp, test_steal_numa_effect_perf.cpp) and they are registered with ctest labels at tests/CMakeLists.txt:248-256. Wall-clock or sleep use in functional tests: tests/functional/integration/test_distributed_e2e.cpp (10 occurrences), test_scheduler_integration.cpp (6), test_distributed_jepsen.cpp (5), test_workload_coordinator.cpp (4), test_dynamic_graph.cpp (4). 38 DISABLED_/GTEST_SKIP uses exist; whether each skip is device-labelled was not verified.

Recommendation: Later wave: read each perf assertion and either turn it into a reported measurement or move it to a functional test with a stated tolerance; replace sleeps with the latch in tests/common/test_barrier.hpp.

Review: confirmed. Assertions exist in all 12 performance files (3 to 8 each); ordering comparisons (LT/LE/GT/GE) in 11 of 12, not 10, with test_dynamic_graph_perf.cpp the only one at zero. tests/CMakeLists.txt:248-256 registers them under ctest labels. 38 DISABLED_/GTEST_SKIP uses recounted. The wall-clock counts per integration file were not recounted.

#### C22. [medium] CLI services write to the terminal and one service file owns six jobs

Evidence: python-code-style: a service never writes to the terminal or imports the command framework. Every service imports the console: cli/sushiruntime/services/project.py:21, diag.py:20, discovery.py:16, docker.py:7, packaging.py:15, select.py:15; select.py:12 imports rich.table and prints at 77 and 97; project.py:594 prints. project.py (617 lines) holds build (447), test (531), run (562), clean (602), doxygen (610), compiler resolution (256), sanitizer checks (373) and the configure stamp (217). Commands are not under a commands/ package: all eleven live in cli/sushiruntime/cli.py:65-273. Package root is cli/sushiruntime, not cli/source/sushiruntime.

Recommendation: Split project.py into one service per verb plus a toolchain-resolution brick, have services return result objects that cli.py renders, and move commands into commands/. Layout moves need the owner's approval.

Review: confirmed. Six services import the console at the cited lines; select.py:12 imports rich.table; project.py is 617 lines with build 447, test 531, run 562, clean 602, doxygen 610, _resolve_compiler 256, _check_sanitizer_runtime 373, _configure_stamp 217; cli.py has 11 command decorators. Fix (one service per verb, result objects rendered by the CLI) is brick-shaped.

#### C23. [medium] The repository carries none of the checkers the rules assume

Evidence: tools/ contains only tools/packaging/verify_installed_package.py. There is no tools/documentation/check_source_comments.py and no layering checker, which is why F1, F13, F14 and F15 accumulated unchecked. docs/ARCHITECTURE.md:75-78 names an `IAllocatorStrategy` seam that `git grep` does not find in include/ or src/.

Recommendation: Add the checker set from project-tools as its own task before the fix waves, so each wave has a pass/fail gate. Correct the ARCHITECTURE reference in the documentation dimension.

Review: confirmed. git ls-files tools returns only tools/packaging/verify_installed_package.py. IAllocatorStrategy appears at docs/ARCHITECTURE.md:78 and nowhere in include/ or src/; docs/CHANGELOG.md:92 records its removal, so the ARCHITECTURE page is stale. The absent .clang-format (see missed) belongs in this finding too.

#### C24. [medium] Process-wide singletons in memory and core

Evidence: cpp-code-style class shape rule 9: dependencies arrive through the constructor, never through a global or a singleton. include/SushiRuntime/memory/epoch_reclamation.hpp:279 `static EpochManager& instance()` with a function-local static, used at lines 395, 396, 405 and from another module at src/graph/dependency_tracker.cpp:50. include/SushiRuntime/core/logger.hpp:218-222 `is_sync_logging_enabled()` is a function-local static atomic, and the logger itself is reached only through macros. The audit lists the Python `_DRIVER` singleton (F24) but not the C++ ones.

Recommendation: Give EpochManager an owner (RuntimeContext or the graph that retires nodes) and pass it to DependencyTracker and EpochGuard by reference. The logger singleton is the one the logging skill permits ('one per project'); record that exception in the design document so the checker can whitelist it.

Review: found by review.

#### C25. [medium] No .clang-format in the repository

Evidence: cpp-code-style: 'The repository's .clang-format is authoritative'. `git ls-files` returns no .clang-format or .clang-tidy and none exists at D:/Projects/sushiruntime root. The audit left this under not_checked. Visible drift follows from it: K&R and same-line bodies at src/topology/hw_discovery.cpp:157 (`} catch (...) {}`), include/SushiRuntime/scheduler/event_polling.hpp:401, include/SushiRuntime/execution/runtime_context.hpp:447 (single-line enum), src/scheduler/scheduler.cpp:703 (trailing space after `try`).

Recommendation: Add the shared Sushi .clang-format as part of the checker task in F26, then run one formatting-only commit before the module wave so later diffs stay readable.

Review: found by review.

#### C26. [medium] Test-only friend hooks sit in public API headers

Evidence: include/SushiRuntime/api/graph/graph.hpp:87, 89, 2576, 2577 declare and befriend detail::RegistrationIndexTestAccess and detail::NodeEventRecordsTestAccess; include/SushiRuntime/api/graph/dynamic_graph.hpp:53 and 426 do the same with DynamicGraphNodeEventsTestAccess. testing: 'Private details are not tested directly. If a private function seems to need its own test, it is a brick that wants its own module.' tests/functional/unit/test_registration_index.cpp and test_node_event_record.cpp are the consumers.

Recommendation: When F4 splits Graph, lift the registration index and the node event records into their own types with a public surface the unit tests use, and delete the three friend declarations.

Review: found by review.

#### C27. [low] About 190 data members have no @brief

Evidence: A scan for trailing-underscore members with no preceding doc line finds 187 in include+src: include/SushiRuntime/api/graph/graph.hpp (39), api/graph/dynamic_graph.hpp (19), api/data/buffer.hpp (14), scheduler/event_polling.hpp (9), scheduler/workload_coordinator.hpp (8), memory/allocation.hpp (8), core/profiling_ledger.hpp (8), core/logger.hpp (7, e.g. lines 94-97 queue_, mutex_, cv_, worker_). The count is heuristic and excludes members without a trailing underscore.

Recommendation: Add `/// @brief` to each member; let the checker in F13 report the exact list.

Review: unclear. The per-file member counts come from a heuristic the audit does not reproduce and I did not rebuild; logger.hpp:94-99 members without @brief were seen. Direction is right, the number is unverified.

#### C28. [low] Platform siblings in the CLI are shaped differently and the build function branches on platform inline

Evidence: cli/sushiruntime/services/project.py:315 `_configure_args_linux(cfg, build_dir, build_type, gpu_backend, dist, asan, tests, cc, cxx)` versus 406 `_configure_args_windows(cfg, build_dir, build_type, gpu_backend, dist, asan, tests)`; the caller at 503-517 runs `_check_windows_compiler` and `_check_sanitizer_runtime` only on Windows and `_resolve_compiler` only on Linux, with different failure returns (rc versus None -> 1). Functions take boolean flag lists: `build(build_type, distributed, no_cuda, clean, force, tests)` at 447.

Recommendation: Give both platforms one protocol (preflight() -> status, configure_args() -> list) with two implementations, and pass an options dataclass instead of six booleans.

Review: confirmed. project.py:315 and 406 signatures differ (cc, cxx on Linux only); _check_windows_compiler 349 and _check_sanitizer_runtime 373 are Windows-only preflights; build() at 447 takes a flag list. A shared protocol with two implementations is the sibling-shape fix.

#### C29. [low] Python files lack the license header, some lack the future import, and constants are not K_-prefixed

Evidence: Every Python file starts directly with its docstring (e.g. cli/sushiruntime/services/project.py:1, tools/packaging/verify_installed_package.py:1); none carries the license block that source-comments requires. `from __future__ import annotations` is missing in cli/tests/test_docker.py, test_gpu_backend_delegation.py, test_gpu_backend_resolution.py, test_gpu_probe_paths.py and test_toolchain_selection.py. Module constants are unprefixed: project.py:30 ASAN_FLAGS, STAMP_NAME, _DRIVER (a module-level singleton, used at 496). Enum members are lowercase: `BuildType.asan` (project.py:104, used at 454). Module docstring of project.py runs 7 lines and narrates history ('This replaces the project/*.sh and project/*.bat scripts'). No module defines a logger or its own exception base; services return int exit codes.

Recommendation: Apply the header and naming pass together with F13; replace the _DRIVER singleton with a constructor argument.

Review: confirmed. project.py:1 opens with the docstring and the 'This replaces the project/*.sh' history line; ASAN_FLAGS at 30, STAMP_NAME at 101, _RUNNER/_DRIVER singletons at 167-168; no getLogger and no exception class in cli/sushiruntime. The missing-future-import list is incomplete: cli/sushiruntime/__init__.py, __main__.py and services/__init__.py also lack it, 8 files not 5.

#### C30. [low] CLI tests do not mirror the sources and five service modules have no test file

Evidence: cli/tests holds test_config.py, test_console_lazy.py, test_docker.py and seven behaviour-named files (test_gpu_backend_delegation.py, test_gpu_probe_paths.py, test_thread_sanitizer_applies.py, ...). No test imports cli/sushiruntime/services/packaging.py, discovery.py, diag.py, or cli/sushiruntime/env.py; select.py is reached only through the CLI app in test_toolchain_selection.py. cli/tests/test_gpu_backend_resolution.py imports the private `_configure_stamp` from services/project.py. tools/packaging/verify_installed_package.py (160 lines) has no test.

Recommendation: Add test_packaging.py, test_discovery.py, test_env.py and test_diag.py; stop importing private names by making the stamp a small public brick.

Review: confirmed. cli/tests holds 10 test files plus conftest; no test_packaging, test_discovery, test_diag or test_env file exists. Which tests import which service was not traced import by import.

### Not checked

- Line-by-line reading: I read outlines, include graphs, grep scans and targeted excerpts. No file was read in full; graph.hpp, scheduler.cpp, runtime_context.cpp, task_graph.cpp, distributed_coordinator.cpp, worker_agent.cpp and tcp_transport.cpp need a full read in the module wave.
- Sample depth per module: topology, memory and interop were covered by scans and one or two excerpts each, not three files read through. Their verdicts rest on the include graph, the catch sites and the comment scans.
- Dead code: no unused-symbol analysis was done. Only one stale reference (IAllocatorStrategy in docs) was confirmed.
- Formatting against .clang-format (Allman braces, 100 columns, include group order, access-specifier indent): a brace grep found 3 K&R lines in include+src; nothing else was measured, and whether a .clang-format file exists was not checked.
- Class-shape rules: explicit single-argument constructors, rule of five, [[nodiscard]] coverage, final on non-base classes, const and noexcept coverage were not scanned.
- C++20 use: only core/span.hpp:43 `#include <span>` was seen, presumably behind a guard; the guard was not read.
- Format-string rules at log call sites (string concatenation, pre-formatted values): one grep returned no hits; not verified by reading.
- Whether the logger's sink is synchronous in tests, and the compile-time cut of DEBUG argument evaluation, were not verified beyond the macro definitions at logger.hpp:357-379.
- Test determinism details: fixed seeds, float tolerances, device labels on the 38 DISABLED_/GTEST_SKIP uses, and test naming were not read.
- The cmake/ modules (19 files, 1,397 lines), CMakeLists.txt files, CMakePresets.json, Dockerfile and .github/workflows/ci.yml were not reviewed for shape.
- examples/ (8 files) were only scanned for @file and stacked comments.
- cli/sushiruntime/config.py, env.py, console.py, diag.py, discovery.py, docker.py, packaging.py and select.py were scanned by grep, not read. cli/tests content was not read.
- platform-portability rules (the _WIN32 checks in core/logger.hpp, topology/affinity.hpp, src/distributed/tcp_transport.cpp) were counted, not reviewed.
- Export macro coverage (SUSHIRUNTIME_API) and ABI concerns in public headers were not reviewed.
- Documentation, license choice and CLI experience belong to other dimensions and were not assessed here.
- No build, test or CLI was run, so nothing here is confirmed by a compiler or a test result.
