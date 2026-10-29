param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,[int]$Mode=0,[int]$Seconds=30,[int]$Compact=1,[int]$FullscreenScan=1,[int]$Movement=0,[string]$Profile='')
$g='Q:/SteamLibrary/steamapps/common/Dishonored2'
function Command([string]$Text){Set-Content -LiteralPath "$g/DVR_cmd_srbench.txt" -Value $Text;Start-Sleep -Milliseconds 400}
Command "vr dlsscompact $Compact"
Command "vr dlssfullscreenscan $FullscreenScan"
& "$PSScriptRoot/benchmark_frame_profile.ps1" -Tag $Tag -Mode $Mode -Seconds $Seconds -Movement $Movement -Profile $Profile -DirectColor 0 -Identity 0
Command 'vr dlssstatus'
$status=Get-Content "$g/DVR_dlssstatus.txt" -Raw -ErrorAction Stop
$status | Set-Content "local/frame-profile/$Tag.dlss.txt"
@{compact=$Compact;fullscreen_scan=$FullscreenScan;mode=$Mode;status=$status;dll_sha256=(Get-FileHash "$g/DishonoredVR.dll").Hash} | ConvertTo-Json | Set-Content "local/frame-profile/$Tag.sr.json"
if($Mode -gt 0 -and ($status -notmatch 'dlsslive enabled 1' -or $status -notmatch ('quality '+($Mode-1)+' '))){throw "Requested DLSS mode was not active: $status"}
