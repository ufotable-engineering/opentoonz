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
    $exeName = Split-Path $Exe -Leaf
    if (-not (Test-Path -LiteralPath (Join-Path $staging $exeName))) {
      throw "$Zip does not contain $exeName"
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
    $moveError = $_
    # A locked file (e.g. another running instance) stops the move halfway
    try {
      if ($installing) { Get-ProgramItems $InstallDir | Remove-Item -Recurse -Force }
      Move-ProgramItems $previous $InstallDir
    } catch {
      Write-Output "Restoring the previous version failed: $_"
    }
    throw $moveError
  }

  $stuff = Join-Path $staging 'portablestuff'
  if (Test-Path -LiteralPath $stuff) {
    robocopy $stuff (Join-Path $InstallDir 'portablestuff') /E /R:2 /W:1 /NFL /NDL /NJH /NJS /NP | Out-Null
    # robocopy uses 8 and above for failures
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
  }
  Remove-Item -LiteralPath $staging -Recurse -Force
  Remove-Item -LiteralPath $Zip -Force
} catch {
  Write-Output $_
  Set-Content -LiteralPath $failed -Value $_.ToString()
} finally {
  if ($status) { Close-Status $status }
  if (Test-Path -LiteralPath $inProgress) { Remove-Item -LiteralPath $inProgress -Force }
  # Missing when restoring the previous version failed as well
  if ($Exe -and (Test-Path -LiteralPath $Exe)) {
    Start-Process -FilePath $Exe -WorkingDirectory $InstallDir
  }
  try { Stop-Transcript | Out-Null } catch { }
}
