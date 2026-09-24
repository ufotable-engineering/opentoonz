# Replaces an in-house portable install with the contents of a release zip.
# Started by OpenToonz right before it quits. Files already in portablestuff
# are user data and are never overwritten; only files missing there are added.
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

Start-Transcript -Path (Join-Path $updateDir 'update.log') -Force | Out-Null
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

  if ($ProcessId) { Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue }

  Reset-Dir $staging
  try {
    Expand-Archive -LiteralPath $Zip -DestinationPath $staging
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
    robocopy $stuff (Join-Path $InstallDir 'portablestuff') /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP | Out-Null
  }
  Remove-Item -LiteralPath $staging -Recurse -Force
  Remove-Item -LiteralPath $Zip -Force
} catch {
  Write-Output $_
} finally {
  if ($Exe) { Start-Process -FilePath $Exe -WorkingDirectory $InstallDir }
  Stop-Transcript | Out-Null
}
