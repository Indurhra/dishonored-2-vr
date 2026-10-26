param(
 [string]$Version='0.5.1',
 [Parameter(Mandatory=$true)][string]$ToolkitLoader,
 [string]$BuildDir='build',
 [string]$InnoCompiler='',
 [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$versionParsed=[version]$Version
if($Version -ne $versionParsed.ToString()){throw 'Use a numeric release version, such as 0.5.1'}
if(-not [IO.Path]::IsPathRooted($BuildDir)){$BuildDir=Join-Path $repo $BuildDir}
if(-not $SkipBuild){& cmake --build $BuildDir --config Release --target DishonoredVR dinput8 dvr_psvr2_bridge -j 4; if($LASTEXITCODE){throw 'Mod build failed'}}
$stage=Join-Path $repo "local\installer-$Version\stage"
if(Test-Path $stage){throw "Use a new version or move the previous build directory: $stage"}
$payload=Join-Path $stage 'payload'
New-Item -ItemType Directory -Force $payload,"$payload\DVR_psvr2","$payload\DVR_Docs" | Out-Null
foreach($file in 'dinput8.dll','DishonoredVR.dll'){Copy-Item "$BuildDir\bin\$file" $payload}
Copy-Item "$BuildDir\bin\dvr_psvr2_bridge.exe" "$payload\DVR_psvr2"
Copy-Item -LiteralPath $ToolkitLoader -Destination "$payload\DVR_psvr2\psvr2_toolkit_capi_loader.dll"
Copy-Item "$repo\src\Psvr2\PSVR2Toolkit-LICENSE" "$payload\DVR_psvr2\PSVR2Toolkit-LICENSE.txt"
Copy-Item "$repo\package\DishonoredVR.ini" $payload
Copy-Item "$repo\LICENSE" "$payload\DVR_Docs\LICENSE.txt"
Copy-Item "$repo\LICENSE" "$stage\LICENSE.txt"
$readme=(Get-Content "$repo\package\README.txt" -Raw).Replace('{VERSION}',$Version).Replace('{REPO_URL}','https://github.com/Indurhra')
$readme+="`r`nInstaller: use the Play Dishonored 2 VR shortcut for automatic stable updates. Running through Steam directly does not check updates. PSVR2 adaptive triggers require PSVR2 Toolkit: https://github.com/BnuuySolutions/PSVR2Toolkit . Settings and saves are preserved. The Manager supports rollback. Uninstall via Windows Apps. Installation backups remain in the game's .dvr-install folder.`r`n"
[IO.File]::WriteAllText("$payload\DVR_Docs\README.txt",$readme)
$notice="Third-party software in Dishonored 2 VR`r`n"
foreach($dep in 'detours','fmt','imgui','openxr-loader','jsoncpp'){
 $copyright="$repo\build\vcpkg_installed\x64-windows-static\share\$dep\copyright"
 if(-not(Test-Path $copyright)){throw "Missing license: $copyright"}
 $notice+="`r`n==== $dep ====`r`n"+(Get-Content $copyright -Raw)
}
[IO.File]::WriteAllText("$payload\DVR_Docs\THIRD-PARTY-NOTICES.txt",$notice)
$entries=@(Get-ChildItem $payload -File -Recurse | ForEach-Object { @{Path=$_.FullName.Substring($payload.Length+1).Replace('\','/');Sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()} })
@{Schema=1;Version=$Version;Files=$entries} | ConvertTo-Json -Depth 5 | Set-Content "$payload\manifest.json" -Encoding utf8NoBOM
Compress-Archive -Path "$payload\*" -DestinationPath "$stage\payload.zip"
$csc="$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
& $csc /nologo /target:winexe /platform:x64 /optimize+ "/out:$stage\DishonoredVR-Manager.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll "$PSScriptRoot\Core.cs" "$PSScriptRoot\Updates.cs" "$PSScriptRoot\Manager.cs"
if($LASTEXITCODE){throw 'Manager compilation failed'}
if(-not $InnoCompiler){$InnoCompiler="$repo\local\installer-tools\inno\ISCC.exe"}
& $InnoCompiler "/DStage=$stage" "/DVersion=$Version" "$PSScriptRoot\setup.iss"
if($LASTEXITCODE){throw 'Installer compilation failed'}
$out=Join-Path (Split-Path $stage -Parent) 'output'
Copy-Item "$stage\payload.zip" "$out\DishonoredVR-Payload-$Version.zip"
Get-ChildItem $out -File | ForEach-Object {"$((Get-FileHash $_.FullName).Hash.ToLowerInvariant())  $($_.Name)"} | Set-Content "$out\SHA256SUMS.txt"
Write-Output "UNPUBLISHED installer and update payload: $out"
