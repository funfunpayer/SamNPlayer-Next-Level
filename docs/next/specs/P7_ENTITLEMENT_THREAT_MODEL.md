# P7 — Licence pass (entitlement): design and threat model

**Status:** spec, not built · input to STU-04 (gate N7) · 12 Oct 2026
**Not legal advice.** Terms, VAT and refunds need professional review (STU-03).

## Goal

A buyer of SamNPlayer Next gets a pass that works **offline**, survives a PC
change, and can be re-sent if lost. Free and all safety functions never depend
on it.

## What we reuse and what we must not

The pre-10-Oct design (SamNPlayer history, last at `533457e`, packages
`license/`, `cmd/license-tool`) is a good base: Ed25519, token `SNP1.payload.sig`,
offline verify, feature ids, a gate in `GenerateScript`.

**Must not reuse:** the DEV issuer key `license/testdata/issuer.ed25519` and its
public key `license.EmbeddedPublicKeyHex`. The private key is in git history,
so it is known to everyone with repo access. Next generates a new issuer key
(`genkey`) on an offline machine; its private key never enters any repo, CI
secret or cloud drive. A test that fails if the old public key appears in the
build guards this.

## Pass format (`SNP2`)

`SNP2.<base64url(payload)>.<base64url(ed25519 sig)>`

```json
{
  "v": 2,
  "pid": "pass serial, random 128-bit",
  "sub": "hash of buyer email (no plain email)",
  "product": "next",
  "major": 1,
  "features": ["batch", "autodirector", "editor_pro"],
  "tier": "retail | supporter | internal",
  "iat": 1790000000,
  "exp": null,
  "kid": "issuer key id"
}
```

- `major`: the pass covers Next major version 1 (D3). Updates within 1.x work;
  2.x needs an upgrade pass.
- `exp: null` = no expiry for retail/supporter (D3); internal/invite may expire.
- `kid` lets us rotate the issuer key: the app embeds a small list of trusted
  public keys; a revoked key id is removed in the next release.

## Where the check happens

- In Go, at the start of each Next-only operation (batch start, AutoDirector
  run, pro-editor transaction) — **not** in CSS or JS.
- Without a valid pass, Next runs as a Free-equivalent: play, create, edit,
  device control, emergency stop all work. Only Next-only actions return
  `errNeedsPass` with a clear message.
- User data (projects, `.samn`, batch folders) stays readable and exportable
  without a pass.

## Threats

| # | Threat | Impact | Mitigation |
|---|---|---|---|
| T1 | Forged pass | Free use of Next | Ed25519 signature; only offline private key can sign |
| T2 | Leaked issuer key | All passes forgeable | Key offline, `kid` rotation, new release drops the key |
| T3 | Pass shared publicly | Lost sales | Accepted risk (offline design). `pid` lets us blacklist a leaked serial in the next release; no online check |
| T4 | Patched binary skips the check | Lost sales | Accepted; no DRM arms race. Don't spend effort here |
| T5 | Clock set back/forward | Wrong expiry | Retail passes don't expire; internal ones tolerate ±48 h |
| T6 | Refund / chargeback after pass issued | Paid nothing | Serial goes on the revoke list in the next release; terms say so |
| T7 | Lost pass | Support cost | Re-issue from order record (same `pid`); self-service later |
| T8 | Personal data in pass | Privacy | Only hashed contact; no name, no email, no device id |
| T9 | Check blocks safety | Injury risk | Pass check never in device, playback or e-stop paths; test enforces it |
| T10 | Payment webhook replay | Duplicate passes | Issuer stores provider event id; same event → same pass |
| T11 | Supporter amount at the threshold | Disputes | One fixed price tier (D1); tests at 39.99 / 40.00 / 40.01 if a threshold stays |

## Issuer side (outside the app)

Not in this repo until STU-03 picks a provider. Minimum: verify the provider's
signed webhook server-side, store `{provider_event_id, pid, status}`, sign with
the offline key (manual batch signing is fine at launch volume), e-mail the
pass. No video or usage data ever reaches the issuer.

## Tests (L7)

- Valid / bad signature / wrong product / wrong major / revoked `pid` / revoked `kid`.
- Old DEV public key not trusted (guard test).
- No pass: every Free function works; each Next-only binding returns `errNeedsPass`.
- Device, playback and e-stop code paths do not import the licence package
  (static test on the import graph).
- Offline: no network calls during verify.
