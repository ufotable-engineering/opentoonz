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
# Both markers are also read by OpenToonz on launch
$inProgress = Join-Path $updateDir 'in_progress'
$failed = Join-Path $updateDir 'failed'
# Kept inside previous so that they never outlive the program files they belong to
$stuffBackup = Join-Path $previous 'portablestuff'
$stuffAdded = Join-Path $previous 'portablestuff.added'
$installedStuff = Join-Path $InstallDir 'portablestuff'
$kept = @('portablestuff', 'portablestuff.added', 'update')

function Get-ShippedNames($dir) {
  @(Get-ChildItem -LiteralPath $dir -Force | Where-Object { $kept -notcontains $_.Name } | ForEach-Object { $_.Name })
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

# robocopy overwrites only files whose size or time differ, so those are the
# ones saved. Files the release adds are listed so they can be removed again.
function Save-Stuff($src) {
  $added = @()
  foreach ($file in Get-ChildItem -LiteralPath $src -Recurse -File -Force) {
    $rel = $file.FullName.Substring($src.Length + 1)
    $target = Join-Path $installedStuff $rel
    if (-not (Test-Path -LiteralPath $target)) { $added += $rel; continue }
    $old = Get-Item -LiteralPath $target -Force
    if ($old.Length -eq $file.Length -and $old.LastWriteTimeUtc -eq $file.LastWriteTimeUtc) { continue }
    $saved = Join-Path $stuffBackup $rel
    New-Item -ItemType Directory -Path (Split-Path $saved) -Force | Out-Null
    Copy-Item -LiteralPath $target -Destination $saved -Force
  }
  [IO.File]::WriteAllLines($stuffAdded, [string[]]$added)
}

function Restore-Stuff {
  if (Test-Path -LiteralPath $stuffAdded) {
    foreach ($rel in [IO.File]::ReadAllLines($stuffAdded)) {
      $item = Join-Path $installedStuff $rel
      if (Test-Path -LiteralPath $item) { Remove-Item -LiteralPath $item -Force }
    }
  }
  if (Test-Path -LiteralPath $stuffBackup) { Copy-Stuff $stuffBackup }
}

function Copy-Stuff($src) {
  robocopy $src $installedStuff /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
  # robocopy uses 8 and above for failures
  if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
}

# Undoes a partial update. Without $installed only the old program files are
# moved back, as the new ones never reached the install folder.
function Restore-Previous($names, $installed) {
  try {
    if ($installed) { Remove-Named $names $InstallDir }
    Move-Named $names $previous $InstallDir
    Restore-Stuff
  } catch {
    Write-Output "Restoring the previous version failed: $_"
  }
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
    $names = Get-ShippedNames $previous
    Reset-Dir $staging
    Move-Named $names $InstallDir $staging
    Move-Named $names $previous $InstallDir
    Restore-Stuff
    Remove-Item -LiteralPath $previous -Recurse -Force
    Remove-Item -LiteralPath $staging -Recurse -Force
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
  $installing = $false
  try {
    Move-Named $names $InstallDir $previous
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
    Restore-Previous $names $installing
    throw $updateError
  }
  Remove-Item -LiteralPath $Zip -Force
} catch {
  Write-Output $_
  [IO.File]::WriteAllText($failed, $_.ToString())
} finally {
  if ($status) { Close-Status $status }
  # A failed rollback leaves the current program files in staging
  if (-not $Rollback -and (Test-Path -LiteralPath $staging)) {
    try { Remove-Item -LiteralPath $staging -Recurse -Force } catch { }
  }
  if (Test-Path -LiteralPath $inProgress) { Remove-Item -LiteralPath $inProgress -Force }
  # Missing when restoring the previous version failed as well
  if ($Exe -and (Test-Path -LiteralPath $Exe)) {
    Start-Process -FilePath $Exe -WorkingDirectory $InstallDir
  }
  try { Stop-Transcript | Out-Null } catch { }
}
