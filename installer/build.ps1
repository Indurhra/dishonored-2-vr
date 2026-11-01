param(
 [string]$Version='0.7.0',
 [Parameter(Mandatory=$true)][string]$ToolkitLoader,
 [Parameter(Mandatory=$true)][string]$AfwRuntime,
 [Parameter(Mandatory=$true)][string]$DlssRuntime,
 [Parameter(Mandatory=$true)][string]$DlssLicense,
 [Parameter(Mandatory=$true)][string]$WheelAssets,
 [string]$OutputRoot='',
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
if((Get-FileHash -LiteralPath $AfwRuntime).Hash -ne '48D5849F64AB1B9AB2717247562AFC203CD92900C901510983C745618CC9E5D5'){throw 'AFW runtime differs from the tested release pin'}
if((Get-FileHash -LiteralPath $DlssRuntime).Hash -ne 'BE6E434A94CA32499515EB62CA0E6C274526055D568D0426E4C652DCDFB6EE6E'){throw 'DLSS runtime differs from the tested release pin'}
$builtVersion=Get-Content "$BuildDir/generated/Version.h" -Raw
if($builtVersion -notmatch ('DVR_VERSION_STR "'+[regex]::Escape($Version)+'"')){throw 'Release version does not match the compiled mod'}
$cache=Get-Content "$BuildDir/CMakeCache.txt" -Raw
if($cache -notmatch '(?m)^DVR_NGX_ROOT:PATH=.+$' -or $cache -notmatch '(?m)^DVR_PDAFW_INCLUDE:PATH=.+$'){throw 'Build must include both DLSS and AFW backends'}
if(-not $OutputRoot){$OutputRoot=Join-Path $repo "local\installer-$Version"}
if(-not [IO.Path]::IsPathRooted($OutputRoot)){$OutputRoot=Join-Path $repo $OutputRoot}
$stage=Join-Path $OutputRoot 'stage'
if(Test-Path $stage){throw "Use a new version or move the previous build directory: $stage"}
$payload=Join-Path $stage 'payload'
New-Item -ItemType Directory -Force $payload,"$payload\DVR_psvr2","$payload\DVR_Docs" | Out-Null
foreach($file in 'dinput8.dll','DishonoredVR.dll'){Copy-Item "$BuildDir\bin\$file" $payload}
Copy-Item "$BuildDir\bin\dvr_psvr2_bridge.exe" "$payload\DVR_psvr2"
Copy-Item -LiteralPath $ToolkitLoader -Destination "$payload\DVR_psvr2\psvr2_toolkit_capi_loader.dll"
Copy-Item "$repo\src\Psvr2\PSVR2Toolkit-LICENSE" "$payload\DVR_psvr2\PSVR2Toolkit-LICENSE.txt"
Copy-Item "$repo\package\DishonoredVR.ini" $payload
Copy-Item -LiteralPath $AfwRuntime -Destination "$payload\PDAFWPlugin.dll"
Copy-Item -LiteralPath $DlssRuntime -Destination "$payload\nvngx_dlss.dll"
Copy-Item -LiteralPath $DlssLicense -Destination "$payload\DVR_Docs\NVIDIA-LICENSE.txt"
Copy-Item "$repo\package\AFW-NOTICE.txt","$repo\package\DEPENDENCIES.txt" "$payload\DVR_Docs"
Copy-Item "$repo\docs\PERFORMANCE-REFERENCE.md" "$payload\DVR_Docs\PERFORMANCE.md"
Set-Content "$payload\DVR_steamvr_frame_dispatch.txt" 'Corrected lock ownership and allocation-free SteamVR worker.'
$art=@{lib_wheel=@(29,31,33,36,38,40,42,45,47,49,51,53,55,57,61,63,65,67,69,71,73,75,77,79,81,86,89,119,121,124);quick_access_wheel=@(46,49,52,55,58,64,67,70,73,76,94,113,121)}
foreach($group in $art.Keys){$target="$payload\DVR_assets\wheel\$group";New-Item -ItemType Directory -Force $target | Out-Null;foreach($id in $art[$group]){$name='{0}_id{1:000}.png' -f $group,$id;Copy-Item -LiteralPath (Join-Path "$WheelAssets\$group" $name) -Destination (Join-Path $target $name)}}
Copy-Item "$repo\LICENSE" "$payload\DVR_Docs\LICENSE.txt"
$terms=(Get-Content "$repo\LICENSE" -Raw)+"`r`n`r`nSEPARATE BUNDLED COMPONENT: NVIDIA DLSS. The following terms apply to nvngx_dlss.dll, not to the GPL mod source.`r`n"+(Get-Content -LiteralPath $DlssLicense -Raw)+"`r`n"+(Get-Content "$repo\package\AFW-NOTICE.txt" -Raw)
[IO.File]::WriteAllText("$stage\LICENSE.txt",$terms)
$readme=(Get-Content "$repo\package\README.txt" -Raw).Replace('{VERSION}',$Version).Replace('{REPO_URL}','https://github.com/Indurhra/dishonored-2-vr')
$readme+="`r`nInstaller: use the Play Dishonored 2 VR shortcut for automatic stable updates. Running through Steam directly does not check updates. PSVR2 adaptive triggers require PSVR2 Toolkit: https://github.com/BnuuySolutions/PSVR2Toolkit . Settings and saves are preserved. The Manager supports rollback. Uninstall via Windows Apps. Installation backups remain in the game's .dvr-install folder.`r`n"
[IO.File]::WriteAllText("$payload\DVR_Docs\README.txt",$readme)
$notice="Third-party software in Dishonored 2 VR`r`n"+(Get-Content "$repo\package\DEPENDENCIES.txt" -Raw)+"`r`n"+(Get-Content "$repo\package\AFW-NOTICE.txt" -Raw)
foreach($dep in 'detours','fmt','imgui','openxr-loader','jsoncpp'){
 $installed = ((Get-Content "$BuildDir/CMakeCache.txt" | Where-Object { $_ -like 'VCPKG_INSTALLED_DIR:PATH=*' }) -replace '^VCPKG_INSTALLED_DIR:PATH=','')
 $copyright="$installed\x64-windows-static\share\$dep\copyright"
 if(-not(Test-Path $copyright)){throw "Missing license: $copyright"}
 $notice+="`r`n==== $dep ====`r`n"+(Get-Content $copyright -Raw)
}
[IO.File]::WriteAllText("$payload\DVR_Docs\THIRD-PARTY-NOTICES.txt",$notice)
$entries=@(Get-ChildItem $payload -File -Recurse | ForEach-Object { @{Path=$_.FullName.Substring($payload.Length+1).Replace('\','/');Sha256=(Get-FileHash $_.FullName).Hash.ToLowerInvariant()} })
@{Schema=2;Version=$Version;Files=$entries} | ConvertTo-Json -Depth 5 | Set-Content "$payload\manifest.json" -Encoding utf8NoBOM
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($payload,"$stage\payload.zip")
$csc="$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
& $csc /nologo /target:winexe /platform:x64 /optimize+ "/out:$stage\DishonoredVR-Manager.exe" /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll "$PSScriptRoot\Core.cs" "$PSScriptRoot\Dependencies.cs" "$PSScriptRoot\Updates.cs" "$PSScriptRoot\Manager.cs"
if($LASTEXITCODE){throw 'Manager compilation failed'}
if(-not $InnoCompiler){$InnoCompiler="$repo\local\installer-tools\inno\ISCC.exe"}
& $InnoCompiler "/DStage=$stage" "/DVersion=$Version" "$PSScriptRoot\setup.iss"
if($LASTEXITCODE){throw 'Installer compilation failed'}
$out=Join-Path (Split-Path $stage -Parent) 'output'
Copy-Item "$stage\payload.zip" "$out\DishonoredVR-Payload-$Version.zip"
Get-ChildItem $out -File | ForEach-Object {"$((Get-FileHash $_.FullName).Hash.ToLowerInvariant())  $($_.Name)"} | Set-Content "$out\SHA256SUMS.txt"
Write-Output "UNPUBLISHED installer and update payload: $out"
