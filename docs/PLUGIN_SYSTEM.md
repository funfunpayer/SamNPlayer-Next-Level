# Plugin system — drop-folder host contract

**Status:** H1 infra kept — drop-folder discover / Install / Open Plugins.  
**Virtual Person product:** **CANCELLED / OUT OF SCOPE** ([#264](https://github.com/funfunpayer/SamNPlayer/pull/264) closed) — not deferred / not “parked for later”.  
**Related:** package [`pluginhost/`](../pluginhost/) · Animation Studio [`funfunpayer/tracken`](https://github.com/funfunpayer/tracken)

---

## End-user load path (infra only)

1. Drop a pack folder into the SamNPlayer **Plugins** folder  
   (`%APPDATA%\SamNPlayer\plugins` on Windows), **or** Settings → Plugins → **Install pack…**
2. Packs must contain `samn-plugin.json` at the root
3. No product Enable / OnFrame host / overlay UI for Virtual Person on tip

Everyday Create / Play / Train are unchanged.

---

## Scope

Generic drop-folder plugin **infrastructure** stays. Virtual Person as a SamNPlayer
product surface does **not** — it is cancelled for this codebase. Animation Studio
and other external work stay outside this repo.

**What this is not**

- Not moving Animation Studio into this repository  
- Not a marketplace  
- Not dynamic Go `.so` loading for end users  
- Not Everyday CSRT changes  
- Not a deferred Virtual Person roadmap inside SamNPlayer  

---

## License

No license gate (Owner 10 Oct 2026: SamNPlayer is freeware with an EULA,
financed by donations; the key system was removed). Plugin packs are free.

---

## Package layout + version handshake

### `samn-plugin.json` (required)

```json
{
  "apiVersion": 1,
  "id": "example_pack",
  "name": "Example Pack",
  "version": "0.1.0",
  "hostMinStage": "H1",
  "description": "Asset pack for SamNPlayer",
  "entry": {
    "kind": "asset_pack",
    "webRoot": "web",
    "assetsRoot": "assets"
  }
}
```

| Field | Rule |
|-------|------|
| `apiVersion` | Host accepts `1` only (`pluginhost.CurrentAPIVersion`) |
| `id` | Any safe id; `virtual_person` reserved (cancelled product id) |
| `entry.kind` | `asset_pack` (H1) |

---

## Ownership split

| Side | Owns |
|------|------|
| **SamNPlayer host** | Plugins folder discovery, `samn-plugin.json` handshake, Settings Install / Open folder |
| **Cancelled product (#264)** | In-process scene bus, Enable host, Give/Titjob, pose overlay — **not shipping** |

---

## Stages

| Stage | Scope |
|-------|--------|
| **H1 infra** (this tip) | Drop-folder discover / Install pack / Open Plugins ([#275](https://github.com/funfunpayer/SamNPlayer/pull/275)); product Enable/tick scrubbed |
| **Cancelled** | Virtual Person product UI / host Enable / overlay — closed with #264 |

Wails App methods (shipped): `PluginsDir`, `ListInstalledPlugins`, `InstallPluginPack`,
`OpenPluginsFolder`.

Packages: `pluginhost/` (manifest + paths + Slot stub).
