# Building the third-party dependencies

The plugin links prebuilt static libraries that are committed to the repository:

* `lib\64\*.lib` — Windows x64, built with vcpkg. **This document covers those.**
* `a/*.a` — macOS universal. Not built by vcpkg and not covered here.

Headers live in `include/` and are shared by both platforms — see [Headers](#headers) before touching them.

## Windows x64

Triplet **`x64-windows-static`** (static library, static CRT — the vcxproj is `/MT`).

The dependency set is pinned by [`vcpkg.json`](vcpkg.json), so a refresh is a baseline bump rather than a
manual job on one machine. With a vcpkg checkout at the baseline commit:

```cmd
vcpkg install --triplet x64-windows-static
```

Run from the repository root; vcpkg picks up `vcpkg.json` in manifest mode. The libraries land in
`vcpkg_installed\x64-windows-static\lib` and are copied into `lib\64` by hand, in a commit containing
nothing but binaries.

`.github/workflows/vcpkg-deps.yml` does exactly this on a `windows-latest` runner and uploads the result
as an artifact. It is `workflow_dispatch` only — the build takes tens of minutes on a cold cache, and it
only needs to run when `vcpkg.json` changes.

### Current baseline

| | |
|---|---|
| vcpkg release | `2026.07.29` |
| baseline commit | `9e593bb18ea69cc5095e012465dcd675a822ed0d` |
| OpenSSL | 3.6.3 |
| curl | 8.21.0 (port-version 1) |
| libssh2 | 1.11.1 |
| nghttp2 / nghttp3 / ngtcp2 | 1.69.0 / 1.18.0 / 1.25.0 |
| brotli / zstd / zlib | 1.2.0 / 1.5.7 / 1.3.2 |
| libidn2 / libunistring / libiconv | 2.3.7 / 1.2 / 1.19 |
| libproxy | 0.4.18 |

### The nineteen libraries

`Release|x64` links these out of `lib\64`:

| Library | vcpkg port |
|---|---|
| `libcurl.lib` | `curl` |
| `libcrypto.lib`, `libssl.lib` | `openssl` |
| `libssh2.lib` | `libssh2` (curl `ssh`) |
| `nghttp2.lib` | `nghttp2` (curl `http2`) |
| `nghttp3.lib`, `ngtcp2.lib`, `ngtcp2_crypto_ossl.lib` | `nghttp3`, `ngtcp2[openssl]` (curl `http3`) |
| `brotlicommon.lib`, `brotlidec.lib`, `brotlienc.lib` | `brotli` (curl `brotli`) |
| `zstd.lib` | `zstd` (curl `zstd`) |
| `zlib.lib` | `zlib` (curl, always) |
| `iconv.lib`, `charset.lib` | `libiconv` |
| `idn2.lib` | `libidn2` (curl `idn2`) |
| `unistring.lib` | `libunistring`, via `libidn2` |
| `libproxy.lib`, `modman.lib` | `libproxy` |

`libidn2.lib` is also present in `lib\64` but is on no link line; only `idn2.lib` is linked.

### Why this feature list

The historical invocation recorded here was
`curl[brotli,c-ares,core,http2,non-http,openssl,schannel,ssh,ssl,sspi,winssl]:windows-static`. Do not
reuse it: `core`, `schannel`, `winssl` and `ssl` are no longer curl port features, and two of them were
not doing what their names suggest anyway. The list in `vcpkg.json` was derived instead from
`cURL_VersionInfo` on the shipping build:

* **No Schannel.** `MULTI_SSL` is clear and `ssl_version` is a bare `OpenSSL/...`, so only one TLS backend
  was ever compiled in. In the current port, Schannel arrives only through the `ssl` meta-feature, which
  is why `default-features` is `false`.
* **`idn2`, not `winidn`.** `IDN` is set and `libidn` reports a version, so libidn2 links fine. The old
  note here that "static idn2 is failing" is stale.
* **No `psl`, `gsasl`, `websockets` or `httpsrr`.** The corresponding feature bits are clear and `ws`/`wss`
  are absent from `protocols`. `include/libpsl.h` exists but nothing uses it.
* **`ldap` and `non-http`** are required by the `protocols` list — `ldap`/`ldaps` and the long tail from
  `dict` through `tftp`.
* **`sspi`** covers SSPI, SPNEGO and Kerberos 5; `GSSAPI` is clear, so Kerberos comes via Windows SSPI.

### No c-ares

c-ares was removed on purpose. On several Windows machines c-ares could not determine a usable nameserver
where the OS resolver could, so name resolution failed for every transfer. Without it libcurl uses the
Windows **threaded resolver** (`getaddrinfo` on a worker thread), which resolves names the same way every
other program on the machine does, keeps the multi loop spinning, and stays cancellable from 4D.

`ASYNCHDNS` stays set — the threaded resolver sets it too — and no feature bit tracks c-ares, so this
change is invisible in the feature bitmask. Two consequences:

* **`CURLOPT_DNS_SERVERS` now returns `CURLE_NOT_BUILT_IN`.** It requires c-ares. The plugin's
  `DNS_SERVERS` option is inert on Windows; it degrades quietly rather than failing the transfer.
* That `CURLE_NOT_BUILT_IN` is a precise before/after test, and the Windows CI job asserts it.

### Debug configurations are not supported

`Debug|x64` links `libcurl-d.lib` against the *release* `libcrypto.lib`/`libssl.lib` and pulls in both
`msvcrtd.lib` and `libcmtd.lib`. It was already incoherent, `libcurl-d.lib` in `lib\64` is stale, and CI
only ever builds `Release|x64`. Build `Release`. The `Win32` configurations point at a `lib\32` that does
not exist and are equally dead.

## Headers

`include/` is shared by both platforms and holds the **macOS** set: curl 8.20.0, OpenSSL 4.0.0. It is
deliberately not refreshed to match the Windows libraries, because the direction of the mismatch is what
matters:

* Before this rebuild, Windows compiled against curl 8.20.0 headers and linked libcurl **8.18.0** — newer
  headers, older library. That is the dangerous direction: an option added in 8.19 or 8.20 compiles and
  then fails at runtime with `CURLE_UNKNOWN_OPTION`. It produced one real bug.
* Now Windows links **8.21.0** against 8.20.0 headers — older headers, newer library. Safe: you simply
  cannot reach anything added in 8.21.

Refreshing `include/curl` to 8.21 would move the dangerous direction onto macOS, which still links 8.20.0.
So leave it, and raise the macOS libraries upstream instead.

`include/openssl` is OpenSSL 4.0.0 while Windows now links 3.6.3. This is inert — no plugin source file
includes an OpenSSL header. Do not "fix" it by introducing one.

## Follow-up

**OpenSSL 3.6 is end-of-life on 2026-11-01.** 3.6.3 was the right target at the time of this build — it is
what vcpkg carries and it clears the 27 CVEs, including the High-severity `CVE-2026-45447`, that were open
against 3.6.1. It is not a resting place. Before November, check whether vcpkg has a 4.0.x port or a
current 3.5 LTS, and converge both platforms onto one branch built by one system.

macOS is on OpenSSL 4.0.0, which also has CVEs fixed after it — including the same `CVE-2026-45447`. Those
libraries are not vcpkg-built and this manifest cannot fix them.

## Parity reference

`cURL_VersionInfo` from the build this set replaces (Windows x64, libcurl 8.18.0, OpenSSL 3.6.1, with
c-ares). `protocols` and `features` must come back **identical** after a rebuild; anything else means the
feature list is wrong. curl 8.21.0 defines no feature bits that 8.20.0 did not, so there is no legitimate
reason for the bitmask to move.

```json
{
  "version": "8.18.0",
  "version_num": 528896,
  "host": "Windows",
  "features": 1605193629,
  "ssl_version": "OpenSSL/3.6.1",
  "libz_version": "1.3.1",
  "protocols": ["dict","file","ftp","ftps","gopher","gophers","http","https","imap","imaps","ldap","ldaps",
                "mqtt","pop3","pop3s","rtsp","scp","sftp","smb","smbs","smtp","smtps","telnet","tftp"],
  "libidn": "2.3.7",
  "libssh_version": "libssh2/1.11.1_DEV",
  "brotli_version": "brotli/1.2.0",
  "nghttp2_version": "1.68.0",
  "zstd_version": "zstd/1.5.7"
}
```

`features = 1605193629` is `0x5fad4f9d`: IPV6, SSL, LIBZ, NTLM, ASYNCHDNS, SPNEGO, LARGEFILE, IDN, SSPI,
TLSAUTH_SRP, HTTP2, KERBEROS5, UNIX_SOCKETS, HTTPS_PROXY, BROTLI, ALTSVC, HTTP3, ZSTD, UNICODE, HSTS,
THREADSAFE. Note what is *clear*: MULTI_SSL, PSL, GSASL and GSSAPI.

Expected to change after the rebuild, and only these: `version` and `version_num` (8.21.0 / 529664),
`ssl_version` (OpenSSL/3.6.3), `libz_version` (1.3.2), `nghttp2_version` (1.69.0), and `libssh_version`
losing its `_DEV` suffix.
