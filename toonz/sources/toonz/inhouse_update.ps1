# Replaces an in-house portable install with the contents of a release zip.
# Started by OpenToonz right before it quits. Only items the release ships are
# replaced, so other files users keep in the install folder stay put. Like the
# upstream installer's default, files shipped in portablestuff are overwritten
# and everything else there, including personal settings, is kept.
# The replaced program files and the overwritten portablestuff files are kept
# in update\previous, and running this script with -Rollback puts them back.
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
$previousProgram = Join-Path $previous 'program'
$stuffBackup = Join-Path $previous 'stuff'
$stuffAdded = Join-Path $previous 'stuff.added'
$rollbackDir = Join-Path $updateDir 'rollback'
$installedStuff = Join-Path $InstallDir 'portablestuff'
# Both markers are also read by OpenToonz on launch
$inProgress = Join-Path $updateDir 'in_progress'
$failed = Join-Path $updateDir 'failed'
$kept = @('portablestuff', 'update')

function Get-ShippedNames($dir) {
  @(Get-ChildItem -LiteralPath $dir -Name -Force | Where-Object { $kept -notcontains $_ })
}

function Move-Named($names, $from, $to) {
  foreach ($name in $names) {
    $item = Join-Path $from $name
    if (Test-Path -LiteralPath $item) { Move-Item -LiteralPath $item -Destination $to }
  }
}

function Remove-Named($names, $dir) {
  foreach ($name in $names) {
    $item = Join-Path $dir $name
    if (Test-Path -LiteralPath $item) { Remove-Item -LiteralPath $item -Recurse -Force }
  }
}

# Every installed file the release ships is saved, which is cheap for
# portablestuff, and files the release adds are listed so they can be removed
function Save-Stuff($src) {
  $added = New-Object System.Collections.Generic.List[string]
  foreach ($file in Get-ChildItem -LiteralPath $src -Recurse -File -Force) {
    $rel = $file.FullName.Substring($src.Length + 1)
    $target = Join-Path $installedStuff $rel
    if (-not (Test-Path -LiteralPath $target)) { $added.Add($rel); continue }
    $saved = Join-Path $stuffBackup $rel
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $saved -Force
  }
  [IO.File]::WriteAllLines($stuffAdded, $added)
}

function Copy-Stuff($src) {
  robocopy $src $installedStuff /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
  # robocopy uses 8 and above for failures
  if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
}

# Puts back what update\previous holds. Without $installed the new program
# files never reached the install folder, so there is nothing to remove first.
function Restore-Previous($names, $installed) {
  if ($installed) { Remove-Named $names $InstallDir }
  Move-Named $names $previousProgram $InstallDir
  if (Test-Path -LiteralPath $stuffAdded) {
    Remove-Named ([IO.File]::ReadAllLines($stuffAdded)) $installedStuff
  }
  if (Test-Path -LiteralPath $stuffBackup) { Copy-Stuff $stuffBackup }
}

function Reset-Dir($dir) {
  if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
  New-Item -ItemType Directory -Path $dir | Out-Null
}

# Windows lets running executables be moved, so another instance would go on
# running the old files against the new ones
function Assert-NotRunning($dir) {
  $prefix = $dir.TrimEnd('\') + '\'
  $running = @(Get-Process | Where-Object {
    # Paths of other users' processes cannot be read
    try { $path = $_.Path } catch { $path = $null }
    $path -and $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
  })
  if ($running.Count -gt 0) {
    $list = ($running | ForEach-Object { "$($_.Name) ($($_.Id))" }) -join ', '
    throw "Close these programs and update again: $list"
  }
}

# Without its own message loop the window turns "Not Responding" during long
# steps, and Windows then offers to kill the updater halfway through
function Show-Status {
  $sync = [hashtable]::Synchronized(@{ Done = $false })
  $runspace = [runspacefactory]::CreateRunspace()
  $runspace.ApartmentState = 'STA'
  $runspace.Open()
  $runspace.SessionStateProxy.SetVariable('sync', $sync)
  $ps = [PowerShell]::Create()
  $ps.Runspace = $runspace
  $null = $ps.AddScript({
    Add-Type -AssemblyName System.Windows.Forms
    $form = New-Object System.Windows.Forms.Form -Property @{
      Text = 'OpenToonz'; Width = 360; Height = 120; StartPosition = 'CenterScreen'
      FormBorderStyle = 'FixedDialog'; ControlBox = $false; TopMost = $true
    }
    $form.Controls.Add((New-Object System.Windows.Forms.Label -Property @{
      Text = 'Updating OpenToonz. It will restart when finished.'
      Dock = 'Fill'; TextAlign = 'MiddleCenter'
    }))
    $timer = New-Object System.Windows.Forms.Timer -Property @{ Interval = 200 }
    $timer.Add_Tick({ if ($sync.Done) { $form.Close() } })
    $timer.Start()
    [System.Windows.Forms.Application]::Run($form)
  })
  @{ Sync = $sync; PowerShell = $ps; Handle = $ps.BeginInvoke() }
}

function Close-Status($status) {
  $status.Sync.Done = $true
  $null = $status.Handle.AsyncWaitHandle.WaitOne(5000)
  $status.PowerShell.Runspace.Dispose()
  $status.PowerShell.Dispose()
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

# Anything that stops the script before the finally block below would leave
# in_progress behind without restarting OpenToonz
try { Start-Transcript -Path (Join-Path $updateDir 'update.log') -Force | Out-Null } catch { }
$status = $null
try {
  if ($Rollback) {
    if (-not (Test-Path -LiteralPath $previous)) { throw 'No previous version to roll back to' }
    Assert-NotRunning $InstallDir
    $names = Get-ShippedNames $previousProgram
    Reset-Dir $rollbackDir
    Move-Named $names $InstallDir $rollbackDir
    Restore-Previous $names $false
    Remove-Item -LiteralPath $previous -Recurse -Force
    Remove-Item -LiteralPath $rollbackDir -Recurse -Force
    return
  }

  $status = Show-Status
  if ($ProcessId) { Wait-Process -Id $ProcessId -Timeout 120 -ErrorAction SilentlyContinue }
  Assert-NotRunning $InstallDir

  Reset-Dir $staging
  try {
    Expand-Zip $Zip $staging
    $exeName = Split-Path $Exe -Leaf
    if (-not (Test-Path -LiteralPath (Join-Path $staging $exeName))) {
      throw "$Zip does not contain $exeName"
    }
  } catch {
    # A broken zip would otherwise be offered again on every launch
    Remove-Item -LiteralPath $Zip -Force
    throw
  }

  $names = Get-ShippedNames $staging
  Reset-Dir $previous
  New-Item -ItemType Directory -Path $previousProgram | Out-Null
  $installing = $false
  try {
    Move-Named $names $InstallDir $previousProgram
    $installing = $true
    Move-Named $names $staging $InstallDir
    $stuff = Join-Path $staging 'portablestuff'
    if (Test-Path -LiteralPath $stuff) {
      Save-Stuff $stuff
      Copy-Stuff $stuff
    }
  } catch {
    $updateError = $_
    # A locked file stops the move or the copy halfway
    try {
      Restore-Previous $names $installing
    } catch {
      Write-Output "Restoring the previous version failed: $_"
    }
    throw $updateError
  }
  Remove-Item -LiteralPath $Zip -Force
} catch {
  Write-Output $_
  [IO.File]::WriteAllText($failed, $_.ToString())
} finally {
  if ($status) { Close-Status $status }
  if (Test-Path -LiteralPath $staging) {
    try { Remove-Item -LiteralPath $staging -Recurse -Force } catch { }
  }
  if (Test-Path -LiteralPath $inProgress) { Remove-Item -LiteralPath $inProgress -Force }
  # Missing when restoring the previous version failed as well
  if ($Exe -and (Test-Path -LiteralPath $Exe)) {
    Start-Process -FilePath $Exe -WorkingDirectory $InstallDir
  }
  try { Stop-Transcript | Out-Null } catch { }
}
