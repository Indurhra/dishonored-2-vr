param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,[int]$Mode=2,[int]$Compact=1,[int]$FullscreenScan=1,[int]$Seconds=15)
$g='Q:/SteamLibrary/steamapps/common/Dishonored2'
$csv=Join-Path (Get-Location) "local/early-scene/$Tag.csv"
if(Test-Path $csv){throw 'Capture already exists'}
function Command([string]$Text){Set-Content "$g/DVR_cmd_srgpu.txt" $Text;Start-Sleep -Milliseconds 400}
Command "vr dlsscompact $Compact";Command "vr dlssfullscreenscan $FullscreenScan";Command "vr afw $Mode";Command 'vr stereolimit 300'
Start-Sleep 7
if((Get-Content "$g/DVR_heartbeat.txt" -Raw) -notmatch 'screen on 1 kind play'){throw 'Not verified gameplay'}
Copy-Item "$g/DVR_heartbeat.txt" "local/early-scene/$Tag.before.txt"
& ./local/frame-profile/PresentMon-2.6.0-x64.exe --process_name Dishonored2.exe --timed $Seconds --terminate_after_timed --no_console_stats --no_track_input --session_name DVRSceneProfile --output_file $csv --v2_metrics > "local/early-scene/$Tag.console.txt" 2>&1
Copy-Item "$g/DVR_heartbeat.txt" "local/early-scene/$Tag.after.txt"
Command 'vr dlssstatus'
@{mode=$Mode;compact=$Compact;fullscreen_scan=$FullscreenScan;dll_sha256=(Get-FileHash "$g/DishonoredVR.dll").Hash;status=(Get-Content "$g/DVR_dlssstatus.txt" -Raw)} | ConvertTo-Json | Set-Content "local/early-scene/$Tag.metadata.json"
Write-Output "Saved $csv"
