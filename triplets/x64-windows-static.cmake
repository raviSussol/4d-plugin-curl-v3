# Overlay triplet, selected by --overlay-triplets in .github/workflows/vcpkg-deps.yml.
#
# It overrides vcpkg's builtin x64-windows-static triplet of the same name, and is
# identical to it apart from the curl block below.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PROVIDED_FORTRAN ON)

# curl 8.21 turned NTLM and SMB from opt-out into opt-in: 8.18 had
# option(CURL_DISABLE_NTLM ... OFF) and 8.21 has option(CURL_ENABLE_NTLM ... OFF),
# with the same flip for SMB. Nothing in vcpkg's curl port sets either, so a
# straight rebuild silently dropped NTLM, smb and smbs - smb and smbs because
# curl gates them on the NTLM crypto core as well as on CURL_ENABLE_SMB.
#
# This rebuild exists to move OpenSSL off a version with 27 CVEs and to drop
# c-ares. Quietly reducing what the plugin can do is not part of that, so the
# two options are set back to what the shipping build had. Retiring NTLM is a
# decision worth making on its own terms, not a side effect of a CVE fix.
#
# The probe in the same workflow is what proves this took effect: it asserts the
# feature bitmask still contains NTLM and that smb and smbs are still in the
# protocol list.
#
# The guard scopes this to curl, since vcpkg evaluates the triplet once per port.
# If PORT is ever not set, applying it to every port is harmless - an option a
# port's CMakeLists does not read only produces a warning from
# vcpkg_cmake_configure, never an error.
if(NOT DEFINED PORT OR PORT STREQUAL "curl")
    list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS
        -DCURL_ENABLE_NTLM=ON
        -DCURL_ENABLE_SMB=ON
    )
endif()
