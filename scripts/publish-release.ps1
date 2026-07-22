<#
.SYNOPSIS
    Tag HEAD as vX.Y.Z and create a GitHub Release with release/<ver> artifacts.

.DESCRIPTION
    Prerequisites:
      1. Commit (and ideally push) the code you want to ship
      2. Build packages first, e.g.:
           .\build.cmd -CarProject car-4wd -Action release -FwVersion 0.1.0
           .\build.cmd -CarProject car-2wd -Action release -FwVersion 0.1.0
         or from WSL: cd projects && ./build.sh all release 0.1.0
      3. gh auth login  (once)

    Examples:
      .\scripts\publish-release.ps1 -Version 0.1.0
      .\scripts\publish-release.ps1 -Version 0.1.0 -Draft
      .\scripts\publish-release.ps1 -Version 0.1.0 -SkipPush
      .\scripts\publish-release.ps1 -Version 0.1.0 -BuildFirst
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$Version,

    [switch]$Draft,
    [switch]$SkipPush,
    [switch]$SkipTag,
    [switch]$BuildFirst,
    [switch]$AllowDirty,
    [switch]$DryRun,

    [ValidateSet("car-4wd", "car-2wd", "all")]
    [string]$Products = "all",

    [string]$Title = "",
    [string]$Notes = ""
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
Set-Location $ProjectRoot

function Find-Gh {
    $cmd = Get-Command gh -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in @(
            "${env:ProgramFiles}\GitHub CLI\gh.exe",
            "${env:LocalAppData}\Programs\GitHub CLI\gh.exe"
        )) {
        if (Test-Path $p) { return $p }
    }
    throw "gh not found. Install: winget install --id GitHub.cli -e  then open a new shell."
}

function Parse-Semver {
    param([string]$Raw)
    $t = $Raw.Trim()
    if ($t -match '^[vV]?(\d+)\.(\d+)\.(\d+)$') {
        return "$($Matches[1]).$($Matches[2]).$($Matches[3])"
    }
    throw "invalid version '$Raw' — use X.Y.Z or vX.Y.Z"
}

$ver = Parse-Semver $Version
$tag = "v$ver"
$releaseDir = Join-Path $ProjectRoot "release\$ver"

Write-Host "==> publish GitHub Release $tag" -ForegroundColor Cyan
Write-Host "    repo root: $ProjectRoot"
Write-Host "    assets:    $releaseDir"

# --- dirty check ---
$status = & git status --porcelain
if ($status -and -not $AllowDirty) {
    Write-Host "Working tree is dirty. Commit or stash first, or pass -AllowDirty." -ForegroundColor Yellow
    & git status -sb
    throw "refusing to tag a dirty tree (use -AllowDirty to override)"
}

# --- optional build ---
if ($BuildFirst) {
    $list = if ($Products -eq "all") { @("car-4wd", "car-2wd") } else { @($Products) }
    foreach ($p in $list) {
        Write-Host "==> build release $p $ver" -ForegroundColor Cyan
        & (Join-Path $PSScriptRoot "build.ps1") $p -Target standalone -Action release -FwVersion $ver -LogEnable 0
    }
}

if (-not (Test-Path $releaseDir)) {
    throw "missing $releaseDir — run build release first, or pass -BuildFirst"
}

$assets = @(Get-ChildItem -Path $releaseDir -File | Where-Object {
        $_.Extension -in ".elf", ".hex", ".bin"
    })
if ($assets.Count -eq 0) {
    throw "no .elf/.hex/.bin under $releaseDir"
}

Write-Host "Assets ($($assets.Count)):" -ForegroundColor Green
$assets | ForEach-Object { Write-Host "  $($_.Name)" }

# --- existing tag / release ---
$existingTag = & git tag -l $tag
$gh = Find-Gh

if (-not $DryRun) {
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    & $gh auth status 2>&1 | Out-Null
    $authOk = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $oldEap
    if (-not $authOk) {
        throw "gh not authenticated. Run: & '$gh' auth login"
    }
}

$existingRelease = $null
if (-not $DryRun) {
    $oldEap = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $null = & $gh release view $tag 2>&1
    if ($LASTEXITCODE -eq 0) { $existingRelease = $tag }
    $ErrorActionPreference = $oldEap
}

if ($DryRun) {
    Write-Host "[dry-run] would tag $tag @ HEAD=$(git rev-parse --short HEAD)" -ForegroundColor Yellow
    if (-not $SkipPush) { Write-Host "[dry-run] would git push origin $tag" -ForegroundColor Yellow }
    Write-Host "[dry-run] would gh release create $tag with $($assets.Count) assets" -ForegroundColor Yellow
    Write-Host "[dry-run] note: run 'gh auth login' before a real publish" -ForegroundColor DarkGray
    exit 0
}

# --- git tag ---
if (-not $SkipTag) {
    if ($existingTag) {
        $tagCommit = & git rev-list -n 1 $tag
        $head = & git rev-parse HEAD
        if ($tagCommit -ne $head) {
            throw "tag $tag already exists on $tagCommit but HEAD is $head — delete/retag manually or use -SkipTag"
        }
        Write-Host "tag $tag already points at HEAD" -ForegroundColor DarkGray
    } else {
        $msg = "Release $tag"
        Write-Host "==> git tag -a $tag" -ForegroundColor Cyan
        & git tag -a $tag -m $msg
        if ($LASTEXITCODE -ne 0) { throw "git tag failed" }
    }
}

if (-not $SkipPush) {
    Write-Host "==> git push origin $tag" -ForegroundColor Cyan
    & git push origin $tag
    if ($LASTEXITCODE -ne 0) { throw "git push origin $tag failed" }
}

# --- GitHub Release ---
if (-not $Title) { $Title = "TM4C123GH6PM $tag" }

$ghArgs = @("release", "create", $tag, "--title", $Title)
if ($SkipPush) {
    $headSha = & git rev-parse HEAD
    $ghArgs += "--target", $headSha
    Write-Host "    (SkipPush: gh will create remote tag at $headSha)" -ForegroundColor DarkGray
} else {
    $ghArgs += "--verify-tag"
}
if ($Draft) {
    $ghArgs += "--draft"
}
if ($Notes) {
    $ghArgs += "--notes", $Notes
} else {
    $ghArgs += "--generate-notes"
}

foreach ($a in $assets) {
    $ghArgs += $a.FullName
}

if ($existingRelease) {
    Write-Host "==> release $tag exists — uploading assets (clobber)" -ForegroundColor Yellow
    $upArgs = @("release", "upload", $tag, "--clobber")
    foreach ($a in $assets) { $upArgs += $a.FullName }
    & $gh @upArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release upload failed" }
} else {
    Write-Host "==> gh release create $tag" -ForegroundColor Cyan
    & $gh @ghArgs
    if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
}

$viewUrl = & $gh release view $tag --json url -q .url
Write-Host "OK  $viewUrl" -ForegroundColor Green
Write-Host "    tag=$tag ver=$ver assets=$($assets.Count)"
