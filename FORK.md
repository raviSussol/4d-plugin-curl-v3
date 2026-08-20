# What this fork changes

This fork of [`miyako/4d-plugin-curl-v3`](https://github.com/miyako/4d-plugin-curl-v3) rebuilds the
**Windows x64** dependencies to move OpenSSL off a version with 27 CVEs and to drop c-ares, which fixes
DNS resolution on Windows sites where the OS resolver works but c-ares cannot find a nameserver.

**macOS is untouched**: `a/*.a` is byte-identical to upstream, and so are the shared `include/curl` and
`include/openssl`. **No plugin source file is modified** — not one line of `4DPlugin-cURL.cpp`. The whole
diff is `lib\64`, the vcxproj link line, the vcpkg manifest and triplet, CI, docs, and the deletion of
headers nothing compiled against.

```sh
git log --oneline main..msupply        # every change, in the order it happened
git diff main..msupply -- . ':!lib/64' # the same thing without the binaries
```

## Versions

Compared against upstream `v4.8.11`, which is what `main` here mirrors:

| | Upstream `v4.8.11` | This fork |
|---|---|---|
| libcurl | 8.18.0 | **8.21.0** |
| OpenSSL | 3.6.1 | **3.6.3** |
| DNS resolver | c-ares 1.34.6 | **Windows threaded resolver** — `getaddrinfo` |
| zlib / nghttp2 | 1.3.1 / 1.68.0 | 1.3.2 / 1.69.0 |
| CRT linkage | `/MT`, but importing `VCRUNTIME140.dll` + the UCRT | fully static |
| curl headers vs library | 8.20.0 headers, 8.18.0 library | 8.20.0 headers, 8.21.0 library |
| `features` bitmask | `1605193629` | `1605193629` — **unchanged** |
| `protocols` | 24 | 25 — `mqtts` added, none removed |

The last two rows are the point of the exercise: the plugin can do exactly what it could before. `mqtts`
is new because libcurl 8.21 can offer MQTT over TLS and 8.18 could not. Nothing was dropped, including
NTLM, `smb` and `smbs`, which curl 8.21 quietly turned from opt-out into opt-in — see
[NTLM and SMB](build.md#ntlm-and-smb).

Every CI run on `msupply` compiles and runs [`.github/probe/curl-probe.c`](.github/probe/curl-probe.c)
against the committed `lib\64` and **fails the build** if the bitmask, the protocol list, or the absence
of c-ares has moved. A red run means do not ship the artifact.

## What callers must know

* **`DNS_SERVERS`, `DNS_INTERFACE`, `DNS_LOCAL_IP4` and `DNS_LOCAL_IP6` do nothing on Windows now.**
  libcurl only implements those four when built against c-ares. They fail *silently*: libcurl rejects
  the option with `CURLE_NOT_BUILT_IN`, the plugin does not check that return value, and the transfer
  proceeds on the system's own DNS configuration. All four still work on macOS, which still links
  c-ares. `RESOLVE` is unaffected — that is `CURLOPT_RESOLVE` and has nothing to do with c-ares.
  See [the option reference](README.md#string-options-passed-through-as-is).
* **`CAINFO` is required for any HTTPS or FTPS request.** There is no built-in CA store, so without it
  you get libcurl error 60. This is not a change — it was equally true upstream — but it is the reason
  DNS-over-HTTPS cannot be switched on here: the DoH lookup cannot verify its own endpoint.
* **Nothing else changes.** Same 15 commands, same option keys, same `transferInfo` fields, same
  selectors.

## What is deliberately not changed

Each of these looks like an oversight until you know why:

* **`include/curl` stays at 8.20.0.** Windows now links 8.21.0 against 8.20.0 headers — older headers,
  newer library, which is the safe direction: you simply cannot reach anything added in 8.21. Before
  the rebuild it was the *dangerous* direction (8.20.0 headers over an 8.18.0 library), which compiles
  and then fails at runtime with `CURLE_UNKNOWN_OPTION`, and it produced one real bug. Refreshing the
  headers to 8.21 would move that hazard onto macOS, which still links 8.20.0. The only headers this
  fork deletes are the c-ares ones, now that nothing links c-ares, and the stale `include/curl (windows)`
  copy (8.4.0-DEV) that was on no include path.
* **libproxy is not rebuilt.** It backs the `AUTOPROXY` option. vcpkg's port installs no `modman.lib`
  at all and a `libproxy.lib` 26× smaller than the one committed here, which reads like PAC script
  evaluation no longer being in the library. libproxy carries no CVE here and no DNS behaviour, so it
  was left alone rather than risk a silent capability regression. Details in
  [build.md](build.md#libproxy-is-not-built-from-this-manifest).
* **The macOS libraries are not rebuilt.** They are not vcpkg-built, the DNS fault has never appeared
  there, and rebuilding them is upstream's call. macOS is on OpenSSL 4.0.0, which has its own CVEs
  fixed after it — that is a real issue, but not one this manifest can reach.
* **DNS-over-HTTPS is not enabled.** It was tried and retired: with no built-in CA store the DoH
  endpoint itself cannot be verified, so it broke machines that were otherwise working.
* **The version number is upstream's.** `MARKETING_VERSION` is left at whatever upstream set, and this
  fork's own releases are tagged separately — see below.

**One live follow-up:** OpenSSL 3.6 goes end-of-life on **2026-11-01**. 3.6.3 was the right target for
this build and it is not a resting place; see [build.md](build.md#follow-up).

## Getting a build

Releases are at **[github.com/raviSussol/4d-plugin-curl-v3/releases/latest](https://github.com/raviSussol/4d-plugin-curl-v3/releases/latest)**.

Tags are `msupply-<upstream version>-<n>`, e.g. `msupply-4.8.11-1`. That prefix is deliberate: it can
never be confused with an upstream tag, and it cannot match the `v*.*.*` pattern that upstream's
signing-and-notarizing release workflow triggers on — a workflow this fork has none of the Apple
secrets for. Do not tag anything here `v*`.

The **Windows `.4DX` is the deliverable.** The `cURL-bundle` asset in a release is convenient for
testing but its macOS binary is built unsigned and un-notarised: dropping it into a 4D project that is
itself notarised will fail notarisation. Take the macOS half from an upstream release.

Between releases, the same two artifacts come from any green `CI build` run on `msupply` — but CI
artifacts expire after 90 days and release assets do not.

## Rebuilding the dependencies

See **[build.md](build.md)**. In short: [`vcpkg.json`](vcpkg.json) plus
[`triplets/x64-windows-static.cmake`](triplets/x64-windows-static.cmake) pin the whole set,
`.github/workflows/vcpkg-deps.yml` builds it on a `windows-latest` runner, and the resulting `.lib`
files are committed into `lib\64` by hand in a commit containing nothing but binaries.

## How this fork is maintained

`main` is a **pristine mirror of `upstream/main`** and never carries our commits. That is what keeps
`git log main..msupply` a permanent, honest answer to "what does this fork change", and what keeps
pulling a future upstream release cheap — a fast-forward plus a rebase, not a merge.

| Branch | Role | Rule |
|---|---|---|
| `main` | mirror of `upstream/main` | never commit to it |
| `msupply` | our changes, rebased onto `main`; the default branch, and what mSupply builds from | the only branch that moves |

To take a new upstream release:

```sh
git fetch upstream
git checkout main && git merge --ff-only upstream/main   # must fast-forward; if it cannot, main was dirtied
git checkout msupply && git rebase main
```

**Do not use GitHub's "Sync fork" button, or `gh repo sync`.** Both act on the *default* branch, which
here is `msupply`, so they would merge `upstream/main` sideways into our changes instead of
fast-forwarding `main`. Sync `main` from the command line, as above.

## Upstream status

The fork is not a substitute for upstreaming; it is what makes the timeline not matter. The intended
exit is that these changes land in a `miyako/4d-plugin-curl-v3` release, at which point the fork is
deleted and everyone takes the signed, notarised upstream bundle again.

* **DNS / c-ares:** not yet filed. To be raised as an issue on upstream, framed precisely — not
  "c-ares is broken" but "on these Windows machines c-ares cannot determine a usable nameserver where
  the OS resolver can", since macOS links an ares-enabled libcurl too and has never shown the problem.
* **The dependency rebuild:** to be offered as `vcpkg.json` + `vcpkg-deps.yml` first rather than as
  twenty committed binaries, since `lib/64` is upstream's to own and he may prefer to run the build.

_Link the issue and PRs here as they are filed._
