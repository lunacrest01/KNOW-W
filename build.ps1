param([ValidateSet('knob_w', 'reset')][string]$Target = 'knob_w', [switch]$Logging)
$ErrorActionPreference = 'Stop'
$PSScriptRoot = $PSScriptRoot.Replace('\', '/')

# Toolchain locations. Override with environment variables to build on another machine:
#   KNOB_W_CMAKE, KNOB_W_PYTHON, KNOB_W_ZEPHYR_SDK, KNOB_W_ZEPHYR
$cmake = if ($env:KNOB_W_CMAKE) { $env:KNOB_W_CMAKE } else { 'C:/Program Files/CMake/bin/cmake.exe' }
$python = if ($env:KNOB_W_PYTHON) { $env:KNOB_W_PYTHON } else { 'C:/Users/bhavy/zephyrproject/.venv/Scripts/python.exe' }
$sdk = if ($env:KNOB_W_ZEPHYR_SDK) { $env:KNOB_W_ZEPHYR_SDK } else { 'C:/Users/bhavy/zephyr-sdk-0.16.8' }
$zephyr = if ($env:KNOB_W_ZEPHYR) { $env:KNOB_W_ZEPHYR } else { "$PSScriptRoot/zephyr" }

foreach ($tool in @($cmake, $python)) {
    if (-not (Test-Path -LiteralPath $tool)) {
        throw "Toolchain not found: $tool. Set KNOB_W_CMAKE / KNOB_W_PYTHON."
    }
}
if (-not (Test-Path -LiteralPath $sdk)) { throw "Zephyr SDK not found: $sdk. Set KNOB_W_ZEPHYR_SDK." }
if (-not (Test-Path -LiteralPath $zephyr)) { throw "Zephyr tree not found: $zephyr. Set KNOB_W_ZEPHYR." }

$argsList = @('-S', "$PSScriptRoot/zmk/app", '-B', "$PSScriptRoot/build/$Target", '-G', 'Ninja',
    '-DBOARD=promicro_nrf52840/nrf52840/uf2',
    "-DZEPHYR_BASE=$zephyr",
    "-DEXTRA_DTC_OVERLAY_FILE=$PSScriptRoot/config/boards/nosd.overlay",
    "-DPython3_EXECUTABLE=$python",
    "-DZEPHYR_SDK_INSTALL_DIR=$sdk")
if ($Target -eq 'reset') {
    $argsList += @('-DSHIELD=settings_reset', "-DEXTRA_CONF_FILE=$PSScriptRoot/config/reset.conf")
} else {
    $argsList += @('-DSHIELD=knob_w', "-DZMK_CONFIG=$PSScriptRoot/config", "-DDTS_ROOT=$PSScriptRoot/config")
    $extra = if ($Logging) { "$PSScriptRoot/config/diagnostic.conf" } else { "" }
    $argsList += "-DEXTRA_CONF_FILE=$extra"
}
& $cmake @argsList
if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
& $cmake --build "$PSScriptRoot/build/$Target" --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
$name = if ($Target -eq 'reset') { 'KNOB-W-settings-reset.uf2' } else { 'KNOB-W.uf2' }
if ($Logging -and $Target -ne 'reset') { $name='KNOB-W-diagnostic.uf2' }
Copy-Item -LiteralPath "$PSScriptRoot/build/$Target/zephyr/zmk.uf2" -Destination "$PSScriptRoot/$name"
