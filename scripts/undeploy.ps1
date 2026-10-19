# Removes the mod from the game folder (restores vanilla).
param([string]$GameDir = 'C:\Program Files (x86)\Steam\steamapps\common\Dishonored2')
Remove-Item "$GameDir\dinput8.dll", "$GameDir\DishonoredVR.dll", "$GameDir\DishonoredVR.pdb", "$GameDir\DishonoredVR.log" -Force -ErrorAction SilentlyContinue
"Removed."
