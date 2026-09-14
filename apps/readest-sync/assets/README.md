# TLS trust bundle

`ca-certificates.crt` is curl's Mozilla CA extraction dated 2026-08-13,
downloaded from <https://curl.se/ca/cacert.pem> on 2026-09-09.
SHA-256: `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`.
Source and licensing: <https://curl.se/docs/caextract.html>.
The converted bundle is licensed under [MPL 2.0](../../../licenses/MPL-2.0.txt).
Its source form is the PEM bundle included here; the dated upstream copy is
<https://curl.se/ca/cacert-2026-08-13.pem>.

The app uses this explicit bundle with hostname and peer verification enabled.
Updates require replacing this file and its recorded/package checksum.
