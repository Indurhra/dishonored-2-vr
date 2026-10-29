param([Parameter(Mandatory)][ValidatePattern('^[A-Za-z0-9_-]+$')][string]$Tag,[int]$Mode=0,[int]$Seconds=25,[string]$Profile='',[int]$Movement=0,[int]$DirectColor=-1,[int]$Identity=-1)
$g='Q:/SteamLibrary/steamapps/common/Dishonored2'
if(Test-Path "local/frame-profile/$Tag.jsonl"){throw "Benchmark tag already exists: $Tag"}
function Command([string]$Text){Set-Content -LiteralPath "$g/DVR_cmd_bench.txt" -Value $Text;Start-Sleep -Milliseconds 300}
$v=@('0')*62;$v[55]='240';if($Movement){$v[50]='8';$v[51]='5'}
Set-Content "$g/DVR_MockInput.next" ($v -join ' ');Move-Item "$g/DVR_MockInput.next" "$g/DVR_MockInput.txt" -Force
if($Identity -ge 0){Command "vr afwidentity $Identity"};if($DirectColor -ge 0){Command "vr dlssdirectcolor $DirectColor"};Command "vr afw $Mode";Command 'vr stereolimit 300';Start-Sleep -Seconds 7
if((Get-Content "$g/DVR_heartbeat.txt" -Raw) -notmatch 'screen on 1 kind play'){throw "Not verified gameplay; load the matching save first"}
if($Profile){Command ($Profile.Replace('{tag}',$Tag))}
$path=Join-Path (Get-Location) "local/frame-profile/$Tag.jsonl"
@{tag=$Tag;mode=$Mode;direct_color=$DirectColor;identity=$Identity;profile=$Profile;movement=$Movement;dll_sha256=(Get-FileHash "$g/DishonoredVR.dll").Hash;toolkit_requested=(Test-Path "$g/DVR_toolkit.txt");toolkit_process_config=(Get-Content "$g/DVR_toolkit_config.txt" -Raw -ErrorAction SilentlyContinue);runtime_manifest=(Get-Content "$g/DVR_usemock.txt" -Raw)}|ConvertTo-Json|Set-Content "$path.metadata.json"
$clock=[Diagnostics.Stopwatch]::StartNew()
while($clock.Elapsed.TotalSeconds -lt $Seconds){
 $p=Get-Process Dishonored2 -ErrorAction Stop
 $r=@{utc=[DateTime]::UtcNow.ToString('o');text=(Get-Content "$g/DVR_heartbeat.txt" -Raw -ErrorAction SilentlyContinue);heartbeat_utc=(Get-Item "$g/DVR_heartbeat.txt").LastWriteTimeUtc.ToString('o');private_memory=$p.PrivateMemorySize64;working_set=$p.WorkingSet64;mode=$Mode;movement=$Movement}
 $r|ConvertTo-Json -Compress|Add-Content -LiteralPath $path
 Start-Sleep -Seconds 1
}
Write-Output "Saved $path"
