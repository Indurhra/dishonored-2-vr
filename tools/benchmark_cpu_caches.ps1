param(
 [Parameter(Mandatory)][string]$GameDir,
 [Parameter(Mandatory)][string]$OutputDir,
 [Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,
 [ValidateRange(0,31)][int]$Mask=0,
 [ValidateRange(5,60)][int]$Seconds=25,
 [switch]$Profile,
 [switch]$Detailed
)
$ErrorActionPreference='Stop'
$GameDir=(Resolve-Path -LiteralPath $GameDir).Path
New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$OutputDir=(Resolve-Path -LiteralPath $OutputDir).Path
$dest=Join-Path $OutputDir "$Tag.jsonl"
if(Test-Path -LiteralPath $dest){throw 'Capture tag already exists'}
$heartbeat=Join-Path $GameDir 'DVR_heartbeat.txt'
function Read-Play {
 $item=Get-Item -LiteralPath $heartbeat
 if(([DateTime]::Now-$item.LastWriteTime).TotalSeconds -gt 5){throw 'Heartbeat is stale'}
 $text=Get-Content -LiteralPath $heartbeat -Raw
 if($text -notmatch '(?m)^xr_running 1' -or $text -notmatch 'screen on 1 kind play'){throw 'Capture requires active VR gameplay, not a menu/loading screen'}
 if($text -notmatch 'afw requested 1 fresh [1-9]'){throw 'AFW has not produced fresh gameplay frames'}
 if($text -notmatch 'gpu6dof mode 1 .*?xf 1/'){throw 'VR hand transforms are inactive; wake the tracked controllers before benchmarking'}
 if($text -notmatch 'input got 1 L .*? valid 1 .*? R .*? valid 1'){throw 'Both controllers must be tracked for a representative CPU capture'}
 return $text
}
function Send-Command([string]$text){
 $path=Join-Path $GameDir 'DVR_cmd_cpu_bench.txt'
 if(Test-Path -LiteralPath $path){throw 'Previous benchmark command is still pending'}
 Set-Content -LiteralPath $path -Value $text
 $clock=[Diagnostics.Stopwatch]::StartNew()
 while(Test-Path -LiteralPath $path){if($clock.Elapsed.TotalSeconds -gt 5){throw 'Game did not consume benchmark command'};Start-Sleep -Milliseconds 100}
}
$before=Read-Play
if($before -notmatch 'cpufast (\d+)'){throw 'This build does not expose CPU test controls'}
$previousMask=[int]$Matches[1]
try {
Send-Command "vr cpufast $Mask"
Start-Sleep -Seconds 5
$ready=Read-Play
if($ready -notmatch "cpufast $Mask(?:\s|$)"){throw 'Requested CPU mode was not verified in heartbeat'}
if($Profile){$stride=1;$detail=0;if($Detailed){$stride=128;$detail=1};Send-Command "vr cpuprofile $Tag $Seconds $stride $detail"}
@{tag=$Tag;mask=$Mask;seconds=$Seconds;profile=[bool]$Profile;detailed=[bool]$Detailed;dll=(Get-FileHash -LiteralPath (Join-Path $GameDir 'DishonoredVR.dll')).Hash;before=$before;ready=$ready} | ConvertTo-Json | Set-Content -LiteralPath "$dest.metadata.json"
$clock=[Diagnostics.Stopwatch]::StartNew()
while($clock.Elapsed.TotalSeconds -lt $Seconds){
 $text=Read-Play
 # A moving scene is not a valid fixed-view A/B comparison. Preserve the data,
 # mark it invalid, and restore the prior mask through the existing finally.
 $pattern='(?m)^aim_org ([-\d.eE+]+) ([-\d.eE+]+) ([-\d.eE+]+) fwd ([-\d.eE+]+) ([-\d.eE+]+) ([-\d.eE+]+)'
 $anchor=[regex]::Match($ready,$pattern);$now=[regex]::Match($text,$pattern)
 if(-not $anchor.Success -or -not $now.Success){throw 'Cannot verify a fixed player view'}
 $positionError=0.;$directionError=0.
 for($i=1;$i -le 6;$i++){
  $delta=[double]::Parse($now.Groups[$i].Value,[Globalization.CultureInfo]::InvariantCulture)-[double]::Parse($anchor.Groups[$i].Value,[Globalization.CultureInfo]::InvariantCulture)
  if($i -le 3){$positionError+=$delta*$delta}else{$directionError+=$delta*$delta}
 }
 $oldPitch=[regex]::Match($ready,'(?m)^cam_pitch ([-\d.eE+]+)')
 $newPitch=[regex]::Match($text,'(?m)^cam_pitch ([-\d.eE+]+)')
 if(-not $oldPitch.Success -or -not $newPitch.Success){throw 'Cannot verify camera pitch'}
 $pitchDelta=[double]::Parse($newPitch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)-[double]::Parse($oldPitch.Groups[1].Value,[Globalization.CultureInfo]::InvariantCulture)
 if($positionError -gt .0225 -or $directionError -gt .0025 -or [Math]::Abs($pitchDelta) -gt .05){
  'INVALID: player moved more than 15 cm or view changed by about 3 degrees' | Set-Content "$dest.invalid.txt"
  throw 'Player/view moved during fixed-view benchmark; result is invalid'
 }
 @{utc=[DateTime]::UtcNow.ToString('o');heartbeat_utc=(Get-Item -LiteralPath $heartbeat).LastWriteTimeUtc.ToString('o');text=$text} | ConvertTo-Json -Compress | Add-Content -LiteralPath $dest
 Start-Sleep -Seconds 1
}
Write-Output "Saved $dest"

} finally {
 # Restore the pre-test mode even when tracking is lost or capture validation fails.
 try { Send-Command "vr cpufast $previousMask" } catch { Write-Warning "Could not restore the CPU test mode; restarting the game restores its default." }
}
