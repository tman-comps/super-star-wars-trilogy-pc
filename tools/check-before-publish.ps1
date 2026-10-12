# check-before-publish.ps1
#
# Run this from the root of your repo BEFORE making it public.
# It looks for anything that could be game data, both in your current
# files and in your entire git history.
#
#   powershell -ExecutionPolicy Bypass -File tools\check-before-publish.ps1

$problems = 0
$romExts  = '\.(sfc|smc|swc|fig|bin|rom)$'

Write-Host "`n== 1. ROM files in the repo now ==" -ForegroundColor Cyan
$tracked = git ls-files
$hits = $tracked | Where-Object { $_ -match $romExts }
if ($hits) { $hits | ForEach-Object { Write-Host "  FOUND: $_" -ForegroundColor Red }; $problems++ }
else       { Write-Host "  none" -ForegroundColor Green }

Write-Host "`n== 2. ROM files anywhere in git history ==" -ForegroundColor Cyan
$hist = git log --all --pretty=format: --name-only | Sort-Object -Unique | Where-Object { $_ -match $romExts }
if ($hist) { $hist | ForEach-Object { Write-Host "  FOUND IN HISTORY: $_" -ForegroundColor Red }; $problems++ }
else       { Write-Host "  none" -ForegroundColor Green }

Write-Host "`n== 3. Extracted asset folders tracked ==" -ForegroundColor Cyan
$assets = $tracked | Where-Object { $_ -match '^(assets|extracted|data/rom)/' }
if ($assets) { Write-Host "  FOUND $($assets.Count) files under assets/ or extracted/" -ForegroundColor Red; $problems++ }
else         { Write-Host "  none" -ForegroundColor Green }

Write-Host "`n== 4. Large files (over 500 KB) ever committed ==" -ForegroundColor Cyan
$big = git rev-list --objects --all |
    git cat-file --batch-check='%(objecttype) %(objectsize) %(rest)' |
    Where-Object { $_ -match '^blob (\d+) (.+)$' -and [int64]$Matches[1] -gt 512000 }
if ($big) {
    $big | ForEach-Object { Write-Host "  CHECK: $_" -ForegroundColor Yellow }
    Write-Host "  (Large source files can be fine, but big data tables may be copied ROM data.)" -ForegroundColor Yellow
} else { Write-Host "  none" -ForegroundColor Green }

Write-Host "`n== 5. Source files with huge byte arrays (possible pasted ROM data) ==" -ForegroundColor Cyan
$arrays = $tracked | Where-Object { $_ -match '\.(c|h|cpp|hpp|inc)$' } | ForEach-Object {
    $count = (Select-String -Path $_ -Pattern '0x[0-9A-Fa-f]{2}\s*,' -AllMatches -ErrorAction SilentlyContinue |
              ForEach-Object { $_.Matches.Count } | Measure-Object -Sum).Sum
    if ($count -gt 4096) { "$_ ($count hex bytes)" }
}
if ($arrays) {
    $arrays | ForEach-Object { Write-Host "  CHECK: $_" -ForegroundColor Yellow }
    Write-Host "  (Lookup tables you wrote are fine; graphics/music/level data copied from the ROM are not.)" -ForegroundColor Yellow
} else { Write-Host "  none" -ForegroundColor Green }

Write-Host ""
if ($problems -gt 0) {
    Write-Host "NOT SAFE TO PUBLISH: fix the red items above first." -ForegroundColor Red
    Write-Host "If something is in git HISTORY, the easiest fix is a brand-new repo (see GITHUB_GUIDE.md)." -ForegroundColor Red
    exit 1
} else {
    Write-Host "No ROM data found. Review any yellow items, then you're good." -ForegroundColor Green
}
