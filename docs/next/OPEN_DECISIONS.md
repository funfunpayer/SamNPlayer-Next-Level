# Open Owner decisions

Found while reviewing the PR #510 documents. Each needs one answer from the
Owner; the recommendation is ours, not decided.

| # | Question | Conflict / context | Recommendation |
|---|---|---|---|
| D1 | **Supporter threshold:** "more than 40,00 €" or "40 € and more"? | Addendum says strictly > 40,00 € — so exactly 40 € gets nothing, which buyers will read as a trick | A fixed **"Supporter Pass 40 €"** purchase tier |
| D2 | **Price of Next** | 49 € one-time is a hypothesis; FunGen shows 75 €/year and 250 € perpetual | Decide after N3; validate with a waiting list (no payment) |
| D3 | **What a pass includes** | Archived docs leave open: one major version vs lifetime | Current major version + security fixes; next major a paid upgrade |
| D4 | **Payment provider** | Stripe often refuses adult-adjacent products | Get written approval from a provider before any code (STU-03) |
| D5 | **Product name** | Docs use "Studio", "Next", "Next Level" for the same thing | Product: **SamNPlayer Next**; "Studio" unused (it was the old Suite tag) |
| D6 | **Where Suite lives after bootstrap** | AI training + golden benchmark are Suite features in the Free repo, but Next owns training too | Freeze Suite in the Free repo at N1; continue it only in Next |
| D7 | **Free scope-down** (less AI, no batch) | EDITIONS.md: "Free stays free"; scope doc: batch leaves Free later | Only after Next batch ships (N3), with a manifest and migration; announce in release notes |
| D8 | **Import timing** | Plan: import after Free release. Alternative: import now, merge Free `main` weekly until N2 | Keep plan (after Free release) — fewer merges, no risk to Free |
| D9 | **Repo was created with a README** | Bootstrap doc wanted an empty repo | No action: the import script merges unrelated histories on a branch, no force push |
| D10 | **DEV licence key in history** | `license/testdata/issuer.ed25519` (private) is in Free history and will be copied by the import. Were any real invite keys issued with it? | Next never trusts it (P7 guard test). If invites were issued, re-issue them with the new key at launch. Rewriting history is not worth it |
