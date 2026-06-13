# =============================================================================
# download_rock07.ps1 — загрузка ассетов Lab 3/8 (Poly Haven Rock 07, CC0)
# =============================================================================
#
# Скачивает glTF + bin + карты (diffuse, normal, displacement, ARM для PBR),
# затем вызывает gltf_to_obj.py → rock_07.obj + rock_07.mtl с map_disp/map_Bump/map_ARM.
#
# Запуск из корня PCG:
#   powershell -ExecutionPolicy Bypass -File tools/download_rock07.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot "..\content\models\rock_07"
New-Item -ItemType Directory -Force -Path $root, (Join-Path $root "textures") | Out-Null
$headers = @{"User-Agent" = "PCG-Lab3/1.0"}
$api = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/rock_07" -Headers $headers

function Download-File($outRel, $url) {
    if (-not $url) { return $false }
    $p = Join-Path $root $outRel
    Write-Host "Download $outRel..."
    Invoke-WebRequest -Uri $url -OutFile $p -UseBasicParsing -Headers $headers
    return $true
}

$required = @(
    @{ out = "rock_07_1k.gltf"; url = $api.gltf.'1k'.gltf.url },
    @{ out = "rock_07.bin"; url = $api.gltf.'1k'.gltf.include.'rock_07.bin'.url },
    @{ out = "textures\rock_07_diff_1k.jpg"; url = $api.gltf.'1k'.gltf.include.'textures/rock_07_diff_1k.jpg'.url },
    @{ out = "textures\rock_07_nor_dx_1k.jpg"; url = $api.nor_dx.'1k'.jpg.url },
    @{ out = "textures\rock_07_disp_1k.jpg"; url = $api.Displacement.'1k'.jpg.url }
)

foreach ($f in $required) {
    if (-not (Download-File $f.out $f.url)) {
        throw "Missing required asset URL for $($f.out)"
    }
}

$armUrl = $null
if ($api.arm.'1k'.jpg.url) {
    $armUrl = $api.arm.'1k'.jpg.url
}
elseif ($api.rough.'1k'.jpg.url) {
    Write-Host "ARM not found, using roughness map as fallback (G channel duplicated in shader path)."
    $armUrl = $api.rough.'1k'.jpg.url
}

if (-not (Download-File "textures\rock_07_arm_1k.jpg" $armUrl)) {
    Write-Warning "No ARM/rough map available - rock will use Ns fallback for PBR."
}

py -3 (Join-Path $PSScriptRoot "gltf_to_obj.py") (Join-Path $root "rock_07_1k.gltf")
Write-Host ("Done: " + (Join-Path $root "rock_07.obj"))
