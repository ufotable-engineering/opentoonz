# Replaces an in-house portable install with the contents of a release zip.
# Started by OpenToonz right before it quits. Like the upstream installer's
# default, files shipped in portablestuff are overwritten and everything else
# there, including personal settings, is kept.
# The replaced program files are kept in update\previous, and running this
# script with -Rollback swaps them back.
param(
  [Parameter(Mandatory = $true)][string]$InstallDir,
  [string]$Zip,
  [int]$ProcessId,
  [string]$Exe,
  [switch]$Rollback
)

$ErrorActionPreference = 'Stop'
$updateDir = Join-Path $InstallDir 'update'
$staging = Join-Path $updateDir 'staging'
$previous = Join-Path $updateDir 'previous'
# Both markers are also read by OpenToonz on launch
$inProgress = Join-Path $updateDir 'in_progress'
$failed = Join-Path $updateDir 'failed'
$kept = @('portablestuff', 'update')

function Get-ProgramItems($dir) {
  Get-ChildItem -LiteralPath $dir -Force | Where-Object { $kept -notcontains $_.Name }
}

function Move-ProgramItems($from, $to) {
  Get-ProgramItems $from | Move-Item -Destination $to
}

function Reset-Dir($dir) {
  if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
  New-Item -ItemType Directory -Path $dir | Out-Null
}

function Show-Status {
  Add-Type -AssemblyName System.Windows.Forms
  $form = New-Object System.Windows.Forms.Form -Property @{
    Text = 'OpenToonz'; Width = 360; Height = 120; StartPosition = 'CenterScreen'
    FormBorderStyle = 'FixedDialog'; ControlBox = $false; TopMost = $true
  }
  $form.Controls.Add((New-Object System.Windows.Forms.Label -Property @{
    Text = 'Updating OpenToonz. It will restart when finished.'
    Dock = 'Fill'; TextAlign = 'MiddleCenter'
  }))
  $form.Show()
  $form.Refresh()
  $form
}

function Expand-Zip($zip, $dest) {
  # bsdtar ships with Windows 10 1803+ and is far faster than Expand-Archive
  if (Get-Command tar.exe -ErrorAction SilentlyContinue) {
    tar.exe -xf $zip -C $dest
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }
  } else {
    Expand-Archive -LiteralPath $zip -DestinationPath $dest
  }
}

Start-Transcript -Path (Join-Path $updateDir 'update.log') -Force | Out-Null
$status = $null
try {
  if ($Rollback) {
    if (-not (Test-Path -LiteralPath $previous)) { throw 'No previous version to roll back to' }
    Reset-Dir $staging
    Move-ProgramItems $InstallDir $staging
    Move-ProgramItems $previous $InstallDir
    Remove-Item -LiteralPath $previous -Recurse -Force
    Rename-Item -LiteralPath $staging -NewName 'previous'
    return
  }

  $status = Show-Status
  if ($ProcessId) { Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue }

  Reset-Dir $staging
  try {
    Expand-Zip $Zip $staging
    if (-not (Test-Path -LiteralPath (Join-Path $staging (Split-Path $Exe -Leaf)))) {
      throw "$Zip does not contain $(Split-Path $Exe -Leaf)"
    }
  } catch {
    # A broken zip would otherwise be offered again on every launch
    Remove-Item -LiteralPath $Zip -Force
    throw
  }

  Reset-Dir $previous
  $installing = $false
  try {
    Move-ProgramItems $InstallDir $previous
    $installing = $true
    Move-ProgramItems $staging $InstallDir
  } catch {
    # A locked file (e.g. another running instance) stops the move halfway
    if ($installing) { Get-ProgramItems $InstallDir | Remove-Item -Recurse -Force }
    Move-ProgramItems $previous $InstallDir
    throw
  }

  $stuff = Join-Path $staging 'portablestuff'
  if (Test-Path -LiteralPath $stuff) {
    robocopy $stuff (Join-Path $InstallDir 'portablestuff') /E /NFL /NDL /NJH /NJS /NP | Out-Null
    # robocopy uses 8 and above for failures
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
  }
  Remove-Item -LiteralPath $staging -Recurse -Force
  Remove-Item -LiteralPath $Zip -Force
} catch {
  Write-Output $_
  Set-Content -LiteralPath $failed -Value $_.ToString()
} finally {
  if ($status) { $status.Close() }
  if (Test-Path -LiteralPath $inProgress) { Remove-Item -LiteralPath $inProgress -Force }
  if ($Exe) { Start-Process -FilePath $Exe -WorkingDirectory $InstallDir }
  Stop-Transcript | Out-Null
}
