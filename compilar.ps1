# Compila usando a instalacao local do ESP-IDF 6.1.
$ErrorActionPreference = 'Stop'
$env:IDF_PATH = 'C:\esp\v6.1\esp-idf'
$env:IDF_TOOLS_PATH = 'C:\Espressif\tools'
$env:IDF_PYTHON_ENV_PATH = 'C:\Espressif\tools\python\v6.1\venv'
$env:ESP_IDF_VERSION = '6.1'
$env:PYTHONUTF8 = '1'
$env:ESP_ROM_ELF_DIR = 'C:\Espressif\tools\tools\esp-rom-elfs\20241011'
$caminhoAnterior = $env:Path
$env:Path = 'C:\Espressif\tools\tools\cmake\4.0.3\bin;C:\Espressif\tools\tools\ninja\1.12.1;C:\Espressif\tools\tools\xtensa-esp-elf\esp-15.2.0_20251204\xtensa-esp-elf\bin;' + $env:Path
Push-Location (Join-Path $PSScriptRoot 'firmware')
try {
    & "$env:IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" build
    if ($LASTEXITCODE -ne 0) { throw "A compilacao falhou (codigo $LASTEXITCODE)." }
} finally {
    Pop-Location
    $env:Path = $caminhoAnterior
}