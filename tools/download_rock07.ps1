# =============================================================================
# download_rock07.ps1 — загрузка ассетов Lab 3 (Poly Haven Rock 07, CC0)
# =============================================================================
#
# Скачивает glTF + bin + три карты (diffuse, normal, displacement),
# затем вызывает gltf_to_obj.py → rock_07.obj + rock_07.mtl с map_disp/map_Bump.
#
# Запуск из корня PCG:
#   powershell -ExecutionPolicy Bypass -File tools/download_rock07.ps1
# =============================================================================

$ErrorActionPreference = "Stop"
$root = Join-Path $PSScriptRoot "..\content\models\rock_07"
New-Item -ItemType Directory -Force -Path $root, (Join-Path $root "textures") | Out-Null
$headers = @{"User-Agent" = "PCG-Lab3/1.0"}
$api = Invoke-RestMethod -Uri "https://api.polyhaven.com/files/rock_07" -Headers $headers

# URL из API Poly Haven (1k разрешение)
$files = @(
    @{ out = "rock_07_1k.gltf"; url = $api.gltf.'1k'.gltf.url },
    @{ out = "rock_07.bin"; url = $api.gltf.'1k'.gltf.include.'rock_07.bin'.url },
    @{ out = "textures\rock_07_diff_1k.jpg"; url = $api.gltf.'1k'.gltf.include.'textures/rock_07_diff_1k.jpg'.url },
    @{ out = "textures\rock_07_nor_dx_1k.jpg"; url = $api.nor_dx.'1k'.jpg.url },
    @{ out = "textures\rock_07_disp_1k.jpg"; url = $api.Displacement.'1k'.jpg.url }
)

foreach ($f in $files) {
    $p = Join-Path $root $f.out
    Write-Host "Download $($f.out)..."
    Invoke-WebRequest -Uri $f.url -OutFile $p -UseBasicParsing -Headers $headers
}

py -3 (Join-Path $PSScriptRoot "gltf_to_obj.py") (Join-Path $root "rock_07_1k.gltf")
Write-Host "Done: $root\rock_07.obj"
