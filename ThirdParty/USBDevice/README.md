# USB Device snapshot

AL94 I-CUBE-USBD-COMPOSITE 1.0.3, copied from prototype `4ae5428c6fb9447b12a79a5cc30a8f47334f8f11`.
Source: `Middlewares/Third_Party/AL94_USB_Composite/COMPOSITE`.
Only Core, COMPOSITE and CDC_ACM are included; source files are byte-identical.
AL94 pack license is preserved in LICENSE-AL94.txt (installed 1.0.3 package).
ST component license text is preserved in LICENSE-ST.txt; individual file notices remain intact.
SHA256SUMS records this vendored snapshot. No nested Git metadata is included.

SDK adaptations are in Source/Services/USB and Board/Support/USB/usb_device_board.c.
The component build derives two class files in the build directory: CDC interface labels,
IAD/data-interface string indices, and unaligned-safe descriptor copying reproduce the
prototype patches without changing this upstream snapshot.
Two CDC ACM functions only: CDC0 ASCII logging, CDC1 SRN-Link. No DFU, MSC, HID or network class.

## Integrity and integration

From the SDK root:

```sh
(cd ThirdParty/USBDevice && shasum -a 256 -c SHA256SUMS)
```

The vendored files remain the hash baseline; generated build copies carry the declared adaptations. Application setup uses the [SDK USB service](../../Source/Services/USB/README.md). Local import, patch and validation history belongs in [CHANGELOG](CHANGELOG.md).
