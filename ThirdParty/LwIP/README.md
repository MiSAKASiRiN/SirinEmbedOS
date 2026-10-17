# lwIP 2.2.1

Pinned upstream tag `STABLE-2_2_1_RELEASE`, unmodified `src/` and `COPYING`.
Source: https://github.com/lwip-tcpip/lwip/tree/STABLE-2_2_1_RELEASE
Archive: https://codeload.github.com/lwip-tcpip/lwip/tar.gz/refs/tags/STABLE-2_2_1_RELEASE
Archive SHA-256: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`.
License: BSD-3-Clause; see COPYING and individual file notices.
No upstream patches. SDK adaptation/configuration is in Source/Services/Network.
The explicit component source list compiles only IPv4/ARP/UDP/ICMP and required core.
Other upstream sources are retained for provenance, not built. SHA256SUMS covers
all imported files; update version, provenance and hashes together on upgrades.

## Integrity check

From the SDK root:

```sh
(cd ThirdParty/LwIP && shasum -a 256 -c SHA256SUMS)
```

Use the SDK [Network API](../../Source/Services/Network/README.md) for application traffic; do not expose lwIP private headers to parent applications. Record local upgrades, patches and validation in [CHANGELOG](CHANGELOG.md).
