# Prepare Cerberus (Andrew Maximov) from Stuff/PBR models.zip for PCG OBJ loader.
# Run from PCG root: py -3 is enough; this script also unpacks zip if needed.

$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot ".."
$stuff = Join-Path $root "Stuff"
$cerberusDir = Join-Path $stuff "Cerberus_by_Andrew_Maximov"
$zip = Join-Path $stuff "PBR models.zip"

if (-not (Test-Path $cerberusDir)) {
    New-Item -ItemType Directory -Force -Path $cerberusDir | Out-Null
}
if (-not (Test-Path (Join-Path $cerberusDir "Textures"))) {
    if (Test-Path $zip) {
        Expand-Archive -Path $zip -DestinationPath $stuff -Force
    } else {
        throw "Missing $zip"
    }
}

$obj = Join-Path $cerberusDir "Cerberus.obj"
if (-not (Test-Path $obj)) {
    Write-Host "Downloading Cerberus.obj (same mesh as teacher FBX)..."
    Invoke-WebRequest `
        -Uri "https://raw.githubusercontent.com/mrdoob/three.js/r165/examples/models/obj/cerberus/Cerberus.obj" `
        -OutFile $obj `
        -UseBasicParsing
}

py -3 (Join-Path $PSScriptRoot "pack_cerberus_arm.py")
py -3 (Join-Path $PSScriptRoot "prepare_cerberus_obj.py")
Write-Host "OK: Stuff/Cerberus_by_Andrew_Maximov/Cerberus_PBR.obj"
