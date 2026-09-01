# Останавливаться на первой же ошибке (а не молча продолжать)
$ErrorActionPreference = "Stop"

try {

# Определяем базовые пути
$DeployDir = "D:\Projects\StormBrowser\Deploy"
$ReleaseDir = "D:\Projects\StormBrowser\out\build\x64-release"
$ProjectDir = "D:\Projects\StormBrowser"
$ExternalDir = "D:\Projects"
$WindeployQt = "D:\Programs\Qt\6.9.3\msvc2022_64\bin\windeployqt.exe"

# Пути для Inno Setup
# Проверьте этот путь. Обычно Inno Setup устанавливается именно сюда.
$InnoCompiler = "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" 
$IssScript = "D:\Projects\StormBrowser\storm_browser_setup.iss"

Write-Host "Очистка папки Deploy..." -ForegroundColor Cyan
if (Test-Path $DeployDir) {
    Remove-Item -Path "$DeployDir\*" -Recurse -Force
} else {
    New-Item -ItemType Directory -Path $DeployDir | Out-Null
}

Write-Host "Копирование файлов из x64-release..." -ForegroundColor Cyan
$ReleaseFiles = @(
    "QtWebEngineProcess.exe",
    "StormBrowser.exe",
    "swresample-5.dll",
    "swscale-8.dll",
    "torrent-rasterbar.dll",
    "libcrypto-3-x64.dll",
    "libssl-3-x64.dll"
)
foreach ($file in $ReleaseFiles) {
    Copy-Item -Path "$ReleaseDir\$file" -Destination $DeployDir -Force
}

Write-Host "Копирование папок из x64-release..." -ForegroundColor Cyan
$ReleaseFolders = @(
    "system_core",
    "qtwebengine_dictionaries"
)
foreach ($folder in $ReleaseFolders) {
    Copy-Item -Path "$ReleaseDir\$folder" -Destination "$DeployDir\$folder" -Recurse -Force
}

Write-Host "Копирование spellcheck_src..." -ForegroundColor Cyan
Copy-Item -Path "$ProjectDir\spellcheck_src" -Destination "$DeployDir\spellcheck_src" -Recurse -Force

Write-Host "Копирование внешних зависимостей (D:\Projects)..." -ForegroundColor Cyan
$ExternalFiles = @(
    "qwebengine_convert_dict.exe",
    "msvcp140.dll",
    "StormUpdater.exe",
    "VCRUNTIME140.dll",
    "vcruntime140_1.dll"
)
foreach ($file in $ExternalFiles) {
    Copy-Item -Path "$ExternalDir\$file" -Destination $DeployDir -Force
}

Write-Host "Запуск windeployqt..." -ForegroundColor Green
$TargetExe = "$DeployDir\StormBrowser.exe"
& $WindeployQt --release --no-translations --no-system-d3d-compiler $TargetExe

Write-Host "Сборка установщика через Inno Setup..." -ForegroundColor Cyan
if (Test-Path $InnoCompiler) {
    & $InnoCompiler $IssScript
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Установочный файл успешно создан (см. папку Output рядом со скриптом .iss)!" -ForegroundColor Green
    } else {
        Write-Host "Ошибка при создании установщика Inno Setup. Проверьте скрипт .iss." -ForegroundColor Red
    }
} else {
    Write-Host "Не найден компилятор Inno Setup по пути: $InnoCompiler" -ForegroundColor Yellow
    Write-Host "Измените путь `$InnoCompiler в скрипте, если программа установлена в другое место." -ForegroundColor Yellow
}

}
catch {
    Write-Host ""
    Write-Host "ОШИБКА: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "Строка: $($_.InvocationInfo.ScriptLineNumber) — $($_.InvocationInfo.Line.Trim())" -ForegroundColor Red
}
finally {
    Write-Host ""
    Read-Host "Нажмите Enter, чтобы закрыть окно"
}