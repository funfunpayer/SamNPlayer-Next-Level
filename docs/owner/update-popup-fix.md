# Update popup (`//`) — fixed

**What you saw:** After saying yes to an update, a popup showed only `//`, and the app might not restart cleanly.

**Cause:** Two issues stacked:

1. Older builds used a browser yes/no dialog; on Windows that dialog’s title can show as `//` instead of a useful message.
2. The Windows “replace exe and restart” helper passed a fragile one-line command through `cmd`. Nested quotes got mangled, so restart could fail after you accepted.

**What we changed:** Updates stay in the app (banner / Settings → Download & restart). Windows now uses a small temp `.cmd` helper so replace+restart is reliable. Release notes can show a short preview. No Patch redesign — that stays a separate track.

**What you should do:** After this lands in a release build, use Settings → Check for updates now (or the startup banner). You should see a version + optional notes, then Download & restart — no empty `//` popup.
