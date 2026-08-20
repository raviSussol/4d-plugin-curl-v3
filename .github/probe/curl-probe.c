/*
 * Acceptance probe for the Windows x64 dependency set.
 *
 * Links the static libraries directly - no 4D, no plugin - and asserts that
 * the libcurl in them has exactly the capabilities the shipping build had,
 * minus c-ares. Compiled by two workflows against two different lib
 * directories, so it is the same test either side of the swap:
 *
 *   vcpkg-deps.yml  ->  vcpkg_installed\x64-windows-static\lib  (before commit)
 *   ci-build.yml    ->  lib\64                                  (after commit)
 *
 * Run against the pre-rebuild libraries it fails exactly two checks - the two
 * c-ares ones - and passes the rest. That is the point: it is a before/after
 * discriminator, not just a smoke test.
 *
 * Build (see the workflows for the full library list):
 *   cl /nologo /MT /DCURL_STATICLIB /DNGHTTP2_STATICLIB /I include
 *      .github\probe\curl-probe.c /link /LIBPATH:lib\64 ...
 */

#include <winsock2.h>
#include <ws2tcpip.h>
#include <curl/curl.h>
#include <stdio.h>
#include <string.h>

/*
 * cURL_VersionInfo from the build this set replaces: libcurl 8.18.0, OpenSSL
 * 3.6.1, with c-ares. Removing c-ares must not move either of these - no
 * feature bit tracks c-ares, and ASYNCHDNS stays set because the threaded
 * resolver sets it too. curl 8.21.0 defines no feature bits that 8.20.0 did
 * not, so there is no legitimate reason for the bitmask to change at all.
 *
 * If one of these fails, the vcpkg feature list in vcpkg.json is wrong. Do not
 * update the constant to make the build pass without accounting for every
 * differing bit or protocol first.
 */
#define EXPECTED_FEATURES 1605193629 /* 0x5fad4f9d */

static const char *const EXPECTED_PROTOCOLS[] = {
    "dict", "file", "ftp", "ftps", "gopher", "gophers", "http", "https",
    "imap", "imaps", "ldap", "ldaps", "mqtt", "pop3", "pop3s", "rtsp",
    "scp", "sftp", "smb", "smbs", "smtp", "smtps", "telnet", "tftp", NULL
};

static const struct { int bit; const char *name; } FEATURE_BITS[] = {
    { CURL_VERSION_IPV6,         "IPV6" },
    { CURL_VERSION_KERBEROS4,    "KERBEROS4" },
    { CURL_VERSION_SSL,          "SSL" },
    { CURL_VERSION_LIBZ,         "LIBZ" },
    { CURL_VERSION_NTLM,         "NTLM" },
    { CURL_VERSION_GSSNEGOTIATE, "GSSNEGOTIATE" },
    { CURL_VERSION_DEBUG,        "DEBUG" },
    { CURL_VERSION_ASYNCHDNS,    "ASYNCHDNS" },
    { CURL_VERSION_SPNEGO,       "SPNEGO" },
    { CURL_VERSION_LARGEFILE,    "LARGEFILE" },
    { CURL_VERSION_IDN,          "IDN" },
    { CURL_VERSION_SSPI,         "SSPI" },
    { CURL_VERSION_CONV,         "CONV" },
    { CURL_VERSION_CURLDEBUG,    "CURLDEBUG" },
    { CURL_VERSION_TLSAUTH_SRP,  "TLSAUTH_SRP" },
    { CURL_VERSION_NTLM_WB,      "NTLM_WB" },
    { CURL_VERSION_HTTP2,        "HTTP2" },
    { CURL_VERSION_GSSAPI,       "GSSAPI" },
    { CURL_VERSION_KERBEROS5,    "KERBEROS5" },
    { CURL_VERSION_UNIX_SOCKETS, "UNIX_SOCKETS" },
    { CURL_VERSION_PSL,          "PSL" },
    { CURL_VERSION_HTTPS_PROXY,  "HTTPS_PROXY" },
    { CURL_VERSION_MULTI_SSL,    "MULTI_SSL" },
    { CURL_VERSION_BROTLI,       "BROTLI" },
    { CURL_VERSION_ALTSVC,       "ALTSVC" },
    { CURL_VERSION_HTTP3,        "HTTP3" },
    { CURL_VERSION_ZSTD,         "ZSTD" },
    { CURL_VERSION_UNICODE,      "UNICODE" },
    { CURL_VERSION_HSTS,         "HSTS" },
    { CURL_VERSION_GSASL,        "GSASL" },
    { CURL_VERSION_THREADSAFE,   "THREADSAFE" },
    { 0, NULL }
};

static int failures;
static int checks;

static void check(const char *what, int pass, const char *detail)
{
    checks++;
    if(!pass)
        failures++;
    printf("  [%s] %-46s %s\n", pass ? "PASS" : "FAIL", what,
           detail ? detail : "");
}

static void note(const char *what, const char *detail)
{
    printf("  [info] %-46s %s\n", what, detail ? detail : "(null)");
}

static const char *nz(const char *s)
{
    return (s && *s) ? s : "(none)";
}

static void print_bits(int features)
{
    int i;
    printf("      set:  ");
    for(i = 0; FEATURE_BITS[i].name; i++)
        if(features & FEATURE_BITS[i].bit)
            printf("%s ", FEATURE_BITS[i].name);
    printf("\n      clear:");
    for(i = 0; FEATURE_BITS[i].name; i++)
        if(!(features & FEATURE_BITS[i].bit))
            printf(" %s", FEATURE_BITS[i].name);
    printf("\n");
}

/* Account for every differing bit, by name, rather than printing two ints. */
static void diff_bits(int actual, int expected)
{
    int i;
    int diff = actual ^ expected;
    printf("      differing bits:\n");
    for(i = 0; FEATURE_BITS[i].name; i++) {
        if(diff & FEATURE_BITS[i].bit)
            printf("        %-14s %s\n", FEATURE_BITS[i].name,
                   (actual & FEATURE_BITS[i].bit) ? "gained" : "LOST");
    }
    /* bits this header does not know about */
    for(i = 0; i < 32; i++) {
        int bit = 1 << i;
        int known = 0;
        int j;
        if(!(diff & bit))
            continue;
        for(j = 0; FEATURE_BITS[j].name; j++)
            if(FEATURE_BITS[j].bit == bit)
                known = 1;
        if(!known)
            printf("        bit %-10d %s (not defined by these headers)\n", i,
                   (actual & bit) ? "gained" : "LOST");
    }
}

static int protocol_count(const char *const *protocols)
{
    int n = 0;
    while(protocols && protocols[n])
        n++;
    return n;
}

static int has_string(const char *const *list, const char *want)
{
    int i;
    for(i = 0; list && list[i]; i++)
        if(!_stricmp(list[i], want))
            return 1;
    return 0;
}

static void compare_protocols(const char *const *actual)
{
    int i;
    int missing = 0;
    int extra = 0;
    char detail[128];

    for(i = 0; EXPECTED_PROTOCOLS[i]; i++) {
        if(!has_string(actual, EXPECTED_PROTOCOLS[i])) {
            printf("      MISSING protocol: %s\n", EXPECTED_PROTOCOLS[i]);
            missing++;
        }
    }
    for(i = 0; actual && actual[i]; i++) {
        if(!has_string(EXPECTED_PROTOCOLS, actual[i])) {
            printf("      EXTRA protocol:   %s\n", actual[i]);
            extra++;
        }
    }
    sprintf(detail, "%d expected, %d present, %d missing, %d extra",
            (int)(sizeof(EXPECTED_PROTOCOLS) / sizeof(EXPECTED_PROTOCOLS[0])) - 1,
            protocol_count(actual), missing, extra);
    check("protocols identical to the baseline", !missing && !extra, detail);
}

/* Does this libcurl accept CURLOPT_DNS_SERVERS at all? On an ares build it
 * returns CURLE_OK. Without ares curl 8.21 does not compile the case in - the
 * option is inside #ifdef USE_RESOLV_ARES - so setopt falls through to
 * CURLE_UNKNOWN_OPTION. Older curl returned CURLE_NOT_BUILT_IN for the same
 * situation, so accept either; what matters is that it is no longer OK. */
static void check_dns_servers(void)
{
    CURL *c = curl_easy_init();
    CURLcode rc;
    char detail[160];

    if(!c) {
        check("curl_easy_init", 0, "returned NULL");
        return;
    }
    rc = curl_easy_setopt(c, CURLOPT_DNS_SERVERS, "1.1.1.1");
    sprintf(detail, "returned %d (%s)", (int)rc, curl_easy_strerror(rc));
    check("CURLOPT_DNS_SERVERS is rejected (no c-ares)",
          rc == CURLE_UNKNOWN_OPTION || rc == CURLE_NOT_BUILT_IN, detail);
    curl_easy_cleanup(c);
}

/* CONNECT_ONLY stops after the TCP connect, so this exercises the resolver and
 * nothing else - no HTTP, no TLS. */
static CURLcode connect_only(const char *url, long timeout_ms)
{
    CURL *c = curl_easy_init();
    CURLcode rc;

    if(!c)
        return CURLE_FAILED_INIT;
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(c);
    curl_easy_cleanup(c);
    return rc;
}

static void check_resolution(void)
{
    CURLcode rc;
    char detail[160];

    rc = connect_only("http://example.com:80/", 20000L);
    sprintf(detail, "returned %d (%s)", (int)rc, curl_easy_strerror(rc));
    check("a real hostname resolves and connects", rc == CURLE_OK, detail);

    rc = connect_only("http://no-such-host.invalid:80/", 20000L);
    sprintf(detail, "returned %d (%s)", (int)rc, curl_easy_strerror(rc));
    check("a bogus hostname fails with status 6",
          rc == CURLE_COULDNT_RESOLVE_HOST, detail);
}

/* Informational, deliberately not an assertion. Whether this build can verify
 * a certificate with no CAINFO supplied has never been confirmed, and it is
 * the reason DNS-over-HTTPS could not be relied on. A failure here is a
 * finding to record, not a regression in this rebuild: the plugin's callers
 * pass their own CAINFO (cacert.pem) and are unaffected either way. */
static void report_tls(const struct curl_version_info_data *d)
{
    CURL *c = curl_easy_init();
    CURLcode rc;
    char detail[160];

    note("built-in CURLOPT_CAINFO", nz(d->cainfo));
    note("built-in CURLOPT_CAPATH", nz(d->capath));

    if(!c)
        return;
    curl_easy_setopt(c, CURLOPT_URL, "https://curl.se/");
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 30000L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    rc = curl_easy_perform(c);
    sprintf(detail, "%d (%s)", (int)rc, curl_easy_strerror(rc));
    note("HTTPS with no CAINFO supplied", detail);
    curl_easy_cleanup(c);
}

int main(void)
{
    const struct curl_version_info_data *d;
    const char *vstr;
    char detail[256];
    int i;

    /* the plugin does exactly this in OnStartup() */
    curl_global_init(CURL_GLOBAL_DEFAULT);

    vstr = curl_version();
    d = curl_version_info(CURLVERSION_NOW);

    printf("=== curl_version() ===\n%s\n\n", vstr ? vstr : "(null)");

    if(!d) {
        printf("curl_version_info returned NULL\n");
        return 1;
    }

    printf("=== curl_version_info(CURLVERSION_NOW) ===\n");
    printf("  age              %d (headers built for %d)\n", (int)d->age,
           (int)CURLVERSION_NOW);
    printf("  version          %s\n", nz(d->version));
    printf("  version_num      %u (0x%06x)\n", d->version_num, d->version_num);
    printf("  host             %s\n", nz(d->host));
    printf("  features         %d (0x%08x)\n", d->features,
           (unsigned int)d->features);
    print_bits(d->features);
    if(d->age >= CURLVERSION_ELEVENTH && d->feature_names) {
        printf("      names:");
        for(i = 0; d->feature_names[i]; i++)
            printf(" %s", d->feature_names[i]);
        printf("\n");
    }
    printf("  ssl_version      %s\n", nz(d->ssl_version));
    printf("  libz_version     %s\n", nz(d->libz_version));
    printf("  libidn           %s\n", nz(d->libidn));
    printf("  libssh_version   %s\n", nz(d->libssh_version));
    printf("  brotli_version   %s\n", nz(d->brotli_version));
    printf("  nghttp2_version  %s\n", nz(d->nghttp2_version));
    printf("  quic_version     %s\n", nz(d->quic_version));
    printf("  zstd_version     %s\n", nz(d->zstd_version));
    printf("  ares             %s\n", nz(d->ares));
    printf("  protocols        (%d)", protocol_count(d->protocols));
    for(i = 0; d->protocols && d->protocols[i]; i++)
        printf(" %s", d->protocols[i]);
    printf("\n\n");

    printf("=== parity with the shipping build ===\n");

    sprintf(detail, "got %d (0x%08x), expected %d (0x%08x)", d->features,
            (unsigned int)d->features, EXPECTED_FEATURES,
            (unsigned int)EXPECTED_FEATURES);
    check("features bitmask identical to the baseline",
          d->features == EXPECTED_FEATURES, detail);
    if(d->features != EXPECTED_FEATURES)
        diff_bits(d->features, EXPECTED_FEATURES);

    compare_protocols(d->protocols);

    /* Only one TLS backend was ever compiled in: MULTI_SSL is clear in the
     * baseline and ssl_version is a bare OpenSSL string. If the vcpkg 'ssl'
     * meta-feature crept back in, Schannel would appear here in parentheses. */
    check("exactly one TLS backend (MULTI_SSL clear)",
          !(d->features & CURL_VERSION_MULTI_SSL), nz(d->ssl_version));
    check("TLS backend is OpenSSL",
          d->ssl_version && !strncmp(d->ssl_version, "OpenSSL/", 8),
          nz(d->ssl_version));

    /* The threaded resolver sets ASYNCHDNS too - this must not regress to a
     * synchronous resolver, which would block 4D uncancellably. */
    check("ASYNCHDNS set (asynchronous resolver present)",
          (d->features & CURL_VERSION_ASYNCHDNS) != 0, NULL);

    printf("\n=== c-ares is gone ===\n");

    check("curl_version_info reports no c-ares",
          !(d->ares && *d->ares), nz(d->ares));
    check("curl_version() does not mention c-ares",
          !(vstr && strstr(vstr, "c-ares")), NULL);
    check_dns_servers();

    printf("\n=== resolution still works ===\n");
    check_resolution();

    printf("\n=== TLS, informational ===\n");
    report_tls(d);

    curl_global_cleanup();

    printf("\n%d checks, %d failed -- %s\n", checks, failures,
           failures ? "PROBE FAILED" : "probe ok");
    return failures ? 1 : 0;
}
