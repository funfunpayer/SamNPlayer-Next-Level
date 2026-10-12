#Requires -Version 5.1
<#
.SYNOPSIS
  Windows 11 setup for SamNPlayer local AI: Ollama, Colibri, teachers, RF-DETR train venv.

.DESCRIPTION
  Idempotent Owner-PC installer. Everyday Create stays on Go CSRT (portable
  with -tags opencv, soft-ignore PerScene after #339). This script only
  prepares optional AI helpers:

    - Python 3 (python.org / winget) + ffmpeg
    - Ollama + recommended vision models (via ai_setup.py)
    - Colibri prebuilt (coli serve) — models NOT downloaded (too large)
    - opencv-contrib-python on the app Python (bootstrap / PreferPython)
    - ai_setup.py: teachers + train (separate ai-train-venv) + models

  Docs: LOCAL_MODEL_SETUP.md, COLIBRI_SETUP.md, VLM_MODELS.md, WINDOWS_OPENCV.md

.PARAMETER RepoRoot
  Path to a SamNPlayer checkout (must contain generator/ai_setup.py).
  If omitted, searches common locations and the script's parent tree.

.PARAMETER SkipOllama
  Do not install Ollama or pull vision models.

.PARAMETER SkipColibri
  Do not download the Colibri Windows zip.

.PARAMETER SkipTrain
  Do not create the RF-DETR / PyTorch train venv.

.PARAMETER SkipTeachers
  Do not pip-install onnxruntime / NudeNet on the app Python.

.PARAMETER SkipOpenCvContrib
  Do not install opencv-contrib-python (Go CSRT is primary; only needed for
  Python bootstrap / PreferPython / rare Advanced paths).

.PARAMETER ColibriDir
  Where to unpack Colibri. Default: %LOCALAPPDATA%\SamNPlayer\colibri

.PARAMETER ColibriPort
  Port for prose helpers (SamNPlayer Settings default). Default: 8080.
  Vision teacher preset in contact_points.py uses 8000 — start coli on the
  port you configure.

.PARAMETER Yes
  Run installs without prompting (winget / pip / ai_setup --yes).

.PARAMETER CheckOnly
  Only print status (nvidia-smi, python, ollama, colibri, ai_setup check).

.EXAMPLE
  .\setup-ai-windows11.ps1 -RepoRoot D:\src\SamNPlayer -Yes

.EXAMPLE
  .\setup-ai-windows11.ps1 -CheckOnly
#>
[CmdletBinding()]
param(
    [string]$RepoRoot = "",
    [switch]$SkipOllama,
    [switch]$SkipColibri,
    [switch]$SkipTrain,
    [switch]$SkipTeachers,
    [switch]$SkipOpenCvContrib,
    [string]$ColibriDir = "",
    [int]$ColibriPort = 8080,
    [switch]$Yes,
    [switch]$CheckOnly
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

function Write-Step([string]$msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Write-Ok([string]$msg)   { Write-Host "  OK  $msg" -ForegroundColor Green }
function Write-Warn([string]$msg) { Write-Host "  !!  $msg" -ForegroundColor Yellow }
function Write-Fail([string]$msg) { Write-Host "  XX  $msg" -ForegroundColor Red }

function Test-IsAdmin {
    $id = [Security.Principal.WindowsIdentity]::GetCurrent()
    $p = New-Object Security.Principal.WindowsPrincipal($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Refresh-Path {
    $machine = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $user = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machine;$user"
}

function Find-Command([string]$name) {
    Refresh-Path
    return (Get-Command $name -ErrorAction SilentlyContinue)
}

function Ensure-Winget {
    if (Find-Command "winget") { return $true }
    Write-Fail "winget not found. Install 'App Installer' from the Microsoft Store, then re-run."
    return $false
}

function Winget-Install([string]$id, [string]$name) {
    if (-not (Ensure-Winget)) { return $false }
    Write-Step "Install $name via winget ($id)"
    $wingetArgs = @(
        "install", "--id", $id, "-e", "--accept-package-agreements",
        "--accept-source-agreements"
    )
    if ($Yes) { $wingetArgs += "--disable-interactivity" }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & winget @wingetArgs
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    Refresh-Path
    # 0 = ok, -1978335189 / other = already installed (winget varies by version)
    if ($code -eq 0) { return $true }
    Write-Warn "winget exit $code for $id (may already be installed — continuing)"
    return $true
}

function Find-Python {
    # Prefer real python.org installs over the WindowsApps stub.
    $candidates = @()
    foreach ($c in @("py", "python", "python3")) {
        $cmd = Find-Command $c
        if ($cmd) { $candidates += $cmd.Source }
    }
    $local = Join-Path $env:LOCALAPPDATA "Programs\Python"
    if (Test-Path $local) {
        Get-ChildItem $local -Directory -ErrorAction SilentlyContinue |
            ForEach-Object {
                $exe = Join-Path $_.FullName "python.exe"
                if (Test-Path $exe) { $candidates += $exe }
            }
    }
    foreach ($exe in $candidates) {
        if ($exe -match "WindowsApps") { continue }
        try {
            $ver = & $exe -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')" 2>$null
            if ($LASTEXITCODE -eq 0 -and $ver) {
                $maj, $min = $ver.Trim().Split(".")
                if ([int]$maj -ge 3 -and [int]$min -ge 10) {
                    return @{ Exe = $exe; Version = $ver.Trim() }
                }
            }
        } catch { }
    }
    # Last resort: py -3
    $py = Find-Command "py"
    if ($py) {
        try {
            $exe = (& py -3 -c "import sys; print(sys.executable)" 2>$null).Trim()
            if ($exe -and (Test-Path $exe) -and ($exe -notmatch "WindowsApps")) {
                $ver = (& $exe -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')").Trim()
                return @{ Exe = $exe; Version = $ver }
            }
        } catch { }
    }
    return $null
}

function Find-RepoRoot([string]$hint) {
    if ($hint -and (Test-Path (Join-Path $hint "generator\ai_setup.py"))) {
        return (Resolve-Path $hint).Path
    }
    $here = $PSScriptRoot
    $walk = @(
        $here,
        (Split-Path $here -Parent),
        (Join-Path (Split-Path $here -Parent) ".."),
        (Join-Path $env:USERPROFILE "src\SamNPlayer"),
        (Join-Path $env:USERPROFILE "SamNPlayer"),
        "D:\src\SamNPlayer",
        "C:\src\SamNPlayer"
    )
    # If script lives under repo/scripts/
    $cand = Join-Path (Split-Path $here -Parent) "generator\ai_setup.py"
    if (Test-Path $cand) { return (Split-Path $here -Parent) }

    foreach ($p in $walk) {
        if (-not $p) { continue }
        try {
            $full = (Resolve-Path $p -ErrorAction SilentlyContinue).Path
            if ($full -and (Test-Path (Join-Path $full "generator\ai_setup.py"))) {
                return $full
            }
        } catch { }
    }
    return $null
}

function Get-GpuInfo {
    $gpus = @()
    $smi = Find-Command "nvidia-smi"
    if (-not $smi) { return $gpus }
    try {
        $out = & nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv,noheader,nounits 2>$null
        foreach ($line in ($out -split "`n")) {
            $parts = $line.Split(",") | ForEach-Object { $_.Trim() }
            if ($parts.Count -ge 3) {
                $gpus += @{
                    Name = $parts[0]
                    VramGb = [math]::Round([double]$parts[1] / 1024, 1)
                    Driver = $parts[2]
                }
            }
        }
    } catch { }
    return $gpus
}

function Install-Colibri([string]$dest) {
    Write-Step "Colibri prebuilt → $dest"
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    $marker = Join-Path $dest "colibri.exe"
    if (Test-Path $marker) {
        Write-Ok "Already present: $marker"
        return $true
    }
    $api = "https://api.github.com/repos/JustVugg/colibri/releases/latest"
    Write-Host "  Fetching latest release metadata…"
    $rel = Invoke-RestMethod -Uri $api -Headers @{ "User-Agent" = "SamNPlayer-setup" }
    $asset = $rel.assets | Where-Object { $_.name -match "windows-x86_64\.zip$" } | Select-Object -First 1
    if (-not $asset) {
        Write-Fail "No windows-x86_64.zip on latest Colibri release."
        Write-Warn "Manual: https://github.com/JustVugg/colibri/releases"
        return $false
    }
    $zip = Join-Path $env:TEMP $asset.name
    Write-Host "  Downloading $($asset.name)…"
    Invoke-WebRequest -Uri $asset.browser_download_url -OutFile $zip -UseBasicParsing
    Expand-Archive -Path $zip -DestinationPath $dest -Force
    # Zip may nest one folder
    if (-not (Test-Path $marker)) {
        $nested = Get-ChildItem $dest -Recurse -Filter "colibri.exe" -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($nested) {
            $srcDir = $nested.Directory.FullName
            Get-ChildItem $srcDir | ForEach-Object {
                Copy-Item $_.FullName -Destination $dest -Recurse -Force
            }
        }
    }
    if (Test-Path $marker) {
        Write-Ok "Colibri engine at $marker"
        Write-Warn "Models are NOT downloaded (hundreds of GB). Set COLI_MODEL to a converted snapshot, then:"
        Write-Host "    cd `"$dest`""
        Write-Host "    `$env:COLI_MODEL = 'D:\path\to\model'"
        Write-Host "    py .\coli serve --host 127.0.0.1 --port $ColibriPort"
        Write-Host "  SamNPlayer Settings AI URL empty = http://127.0.0.1:8080 (prose)."
        Write-Host "  Vision teacher preset uses http://127.0.0.1:8000 — match the port you start."
        return $true
    }
    Write-Fail "colibri.exe missing after unpack"
    return $false
}

function Install-OpenCvContrib([string]$pythonExe) {
    Write-Step "opencv-contrib-python (Python path only — Go CSRT is primary after #339)"
    & $pythonExe -m pip install --upgrade "opencv-contrib-python>=4.8"
    if ($LASTEXITCODE -ne 0) { Write-Fail "pip opencv-contrib-python failed"; return $false }
    # Drop plain opencv that breaks CSRT if something else pulled it earlier
    & $pythonExe -m pip uninstall -y opencv-python opencv-python-headless 2>$null | Out-Null
    & $pythonExe -m pip install --upgrade --force-reinstall --no-deps "opencv-contrib-python>=4.8" | Out-Null
    $check = & $pythonExe -c @"
import cv2
ok = hasattr(cv2, 'TrackerCSRT_create') or hasattr(getattr(cv2, 'legacy', None), 'TrackerCSRT_create')
print('CSRT' if ok else 'NO_CSRT', getattr(cv2, '__version__', '?'))
"@
    if ($check -match "^CSRT") {
        Write-Ok "OpenCV contrib with CSRT: $check"
        return $true
    }
    Write-Warn "OpenCV installed but CSRT API missing ($check). OpenCV 5 may fall back to KCF (#95). Everyday Go CSRT portable does not need this."
    return $true
}

function Invoke-AiSetup([string]$pythonExe, [string]$repo, [string]$profile) {
    $script = Join-Path $repo "generator\ai_setup.py"
    Write-Step "ai_setup.py install $profile"
    # Caller already confirmed (-Yes or interactive Continue); always execute.
    $aiArgs = @($script, "install", $profile, "--yes")
    & $pythonExe @aiArgs
    return ($LASTEXITCODE -eq 0)
}

function Show-Check([string]$pythonExe, [string]$repo) {
    Write-Step "Status check"
    $gpus = Get-GpuInfo
    if ($gpus.Count -eq 0) {
        Write-Warn "No NVIDIA GPU via nvidia-smi (CPU training is very slow; onnxruntime-gpu skipped)."
    } else {
        foreach ($g in $gpus) {
            Write-Ok "GPU: $($g.Name) $($g.VramGb) GB (driver $($g.Driver))"
        }
    }
    if ($pythonExe) {
        Write-Ok "Python: $pythonExe"
    } else {
        Write-Fail "Python 3.10+ not found"
    }
    $ollama = Find-Command "ollama"
    if ($ollama) { Write-Ok "ollama: $($ollama.Source)" } else { Write-Warn "ollama not on PATH" }
    $ffmpeg = Find-Command "ffmpeg"
    if ($ffmpeg) { Write-Ok "ffmpeg: $($ffmpeg.Source)" } else { Write-Warn "ffmpeg not on PATH" }
    $coliDir = if ($ColibriDir) { $ColibriDir } else { Join-Path $env:LOCALAPPDATA "SamNPlayer\colibri" }
    if (Test-Path (Join-Path $coliDir "colibri.exe")) {
        Write-Ok "Colibri: $coliDir\colibri.exe"
    } else {
        Write-Warn "Colibri not installed at $coliDir"
    }
    $trainVenv = Join-Path $env:LOCALAPPDATA "SamNPlayer\ai-train-venv\Scripts\python.exe"
    if (Test-Path $trainVenv) {
        Write-Ok "Train venv: $trainVenv"
    } else {
        Write-Warn "Train venv missing (ai_setup install train)"
    }
    if ($pythonExe -and $repo) {
        Write-Host ""
        & $pythonExe (Join-Path $repo "generator\ai_setup.py") "check"
    }
}

# --- main ------------------------------------------------------------------

Write-Host "SamNPlayer Windows 11 AI setup" -ForegroundColor White
Write-Host "Everyday Create = Go CSRT (portable). This installs optional teachers / train / LLM." -ForegroundColor DarkGray

if (-not $ColibriDir) {
    $ColibriDir = Join-Path $env:LOCALAPPDATA "SamNPlayer\colibri"
}

$repo = Find-RepoRoot $RepoRoot
$pyInfo = Find-Python

if ($CheckOnly) {
    Show-Check $(if ($pyInfo) { $pyInfo.Exe } else { $null }) $repo
    exit 0
}

if (-not $Yes) {
    Write-Host ""
    Write-Host "Will install (skippable via switches): Python, ffmpeg, Ollama, Colibri zip,"
    Write-Host "opencv-contrib (optional Python path), ai_setup teachers + train + models."
    Write-Host "Disk: plan 50+ GB free (Ollama VLMs); Colibri model files are separate (100s GB)."
    $ans = Read-Host "Continue? [y/N]"
    if ($ans -notmatch '^[Yy]') { Write-Host "Aborted."; exit 1 }
    $Yes = $true  # subsequent steps non-interactive
}

# 1) Python
Write-Step "Python 3.10+"
$pyInfo = Find-Python
if (-not $pyInfo) {
    if (-not (Winget-Install "Python.Python.3.12" "Python 3.12")) {
        Write-Fail "Install Python from https://www.python.org/downloads/ (check 'Add to PATH')"
        exit 1
    }
    Refresh-Path
    $pyInfo = Find-Python
    if (-not $pyInfo) {
        Write-Fail "Python still not found. Open a new terminal or reboot PATH, then re-run."
        exit 1
    }
}
Write-Ok "Using Python $($pyInfo.Version) at $($pyInfo.Exe)"
& $pyInfo.Exe -m pip install --upgrade pip setuptools wheel | Out-Null

# 2) ffmpeg
Write-Step "ffmpeg"
if (-not (Find-Command "ffmpeg")) {
    Winget-Install "Gyan.FFmpeg" "ffmpeg" | Out-Null
    Refresh-Path
    if (-not (Find-Command "ffmpeg")) {
        Winget-Install "ffmpeg" "ffmpeg" | Out-Null
        Refresh-Path
    }
}
if (Find-Command "ffmpeg") { Write-Ok "ffmpeg on PATH" } else { Write-Warn "ffmpeg still missing — portable SamNPlayer zip often ships its own" }

# 3) NVIDIA note
$gpus = Get-GpuInfo
Write-Step "GPU"
if ($gpus.Count -eq 0) {
    Write-Warn "nvidia-smi not found. Install NVIDIA Game Ready / Studio driver for CUDA training & onnxruntime-gpu."
} else {
    foreach ($g in $gpus) { Write-Ok "$($g.Name) — $($g.VramGb) GB VRAM" }
    Write-Host "  Tip: PyTorch train venv uses CUDA 12.8 wheels (RTX 20xx–50xx). Driver should be recent."
}

# 4) Ollama
if (-not $SkipOllama) {
    Write-Step "Ollama"
    if (-not (Find-Command "ollama")) {
        Winget-Install "Ollama.Ollama" "Ollama" | Out-Null
        Refresh-Path
        if (-not (Find-Command "ollama")) {
            Write-Warn "winget Ollama failed — install from https://ollama.com/download then re-run models step."
        }
    } else {
        Write-Ok "ollama already on PATH"
    }
    # Start server if CLI exists but API down
    if (Find-Command "ollama") {
        try {
            $null = Invoke-WebRequest -Uri "http://127.0.0.1:11434/api/tags" -UseBasicParsing -TimeoutSec 2
            Write-Ok "Ollama API up"
        } catch {
            Write-Host "  Starting Ollama service…"
            Start-Process "ollama" -ArgumentList "serve" -WindowStyle Hidden -ErrorAction SilentlyContinue
            Start-Sleep -Seconds 3
        }
    }
}

# 5) Colibri
if (-not $SkipColibri) {
    Install-Colibri $ColibriDir | Out-Null
}

# 6) Repo + opencv-contrib + ai_setup
if (-not $repo) {
    Write-Fail "SamNPlayer checkout not found (need generator/ai_setup.py)."
    Write-Warn "Pass -RepoRoot D:\path\to\SamNPlayer or clone funfunpayer/SamNPlayer first."
    Write-Host "  Remaining system tools are installed; run again after clone."
    Show-Check $pyInfo.Exe $null
    exit 2
}
Write-Ok "Repo: $repo"

if (-not $SkipOpenCvContrib) {
    Install-OpenCvContrib $pyInfo.Exe | Out-Null
}

if (-not $SkipTeachers) {
    if (-not (Invoke-AiSetup $pyInfo.Exe $repo "teachers")) {
        Write-Fail "ai_setup teachers failed"
        exit 1
    }
}

if (-not $SkipTrain) {
    if (-not (Invoke-AiSetup $pyInfo.Exe $repo "train")) {
        Write-Fail "ai_setup train failed"
        exit 1
    }
}

if (-not $SkipOllama) {
    if (-not (Invoke-AiSetup $pyInfo.Exe $repo "models")) {
        Write-Warn "ai_setup models had issues (ollama not running / no VRAM match) — pull manually later."
    }
}

Write-Step "Done — verify"
Show-Check $pyInfo.Exe $repo

Write-Host ""
Write-Host "Next:" -ForegroundColor Cyan
Write-Host "  1. Everyday Create: portable zip log should say Go-native CSRT (no pip needed)."
Write-Host "  2. Teachers: Create → Advanced → Rhythm-robust → Generate contact points."
Write-Host "  3. Train RF-DETR:"
Write-Host "       %LOCALAPPDATA%\SamNPlayer\ai-train-venv\Scripts\python.exe generator\contact_detector.py train --dataset DS --out model.onnx --device cuda"
Write-Host "  4. Colibri prose: coli serve on :$ColibriPort → Settings → Test AI server."
Write-Host "  5. Re-check anytime:  .\setup-ai-windows11.ps1 -CheckOnly -RepoRoot `"$repo`""
Write-Host "     or:  $($pyInfo.Exe) `"$repo\generator\ai_setup.py`" check"
