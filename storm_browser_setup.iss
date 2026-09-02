#define MyAppVersion "1.2.4"

[Setup]
AppId={{8A2B3C4D-5E6F-4C4C-8A8A-1A2B3C4D5E6F}
AppName=Storm Browser
AppVersion={#MyAppVersion}
AppPublisher=Shtorm Software

CloseApplications=force
RestartApplications=no

; Устанавливаем браузер в локальную папку пользователя (AppData\Local)
DefaultDirName={localappdata}\Storm Software\Storm Browser
DefaultGroupName=Storm Browser
AllowNoIcons=yes

; Снижаем требования к правам. UAC (щит администратора) больше не появится!
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64
OutputBaseFilename=StormBrowser_Setup

SetupIconFile=D:\Projects\StormBrowser\resources\storm_browser.ico
LicenseFile=D:\Projects\StormBrowser\resources\eula.txt
WizardImageFile=D:\Projects\StormBrowser\resources\installer_large.bmp
WizardSmallImageFile=D:\Projects\StormBrowser\resources\installer_small.bmp

Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ShowLanguageDialog=yes

[Languages]
Name: "russian"; MessagesFile: "compiler:Languages\Russian.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Dirs]
; Явно гарантируем создание папки для исходников словарей при установке
Name: "{app}\spellcheck_src"

[Files]
Source: "D:\Projects\StormBrowser\Deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "D:\Projects\StormBrowser\resources\dev\vcpkg\installed\x64-windows\bin\torrent-rasterbar.dll"; DestDir: "{app}"; Flags: ignoreversion

; Официальный VC++ 2015-2022 x64 Redistributable — скачать заранее с
; https://aka.ms/vs/17/release/vc_redist.x64.exe и положить рядом со скриптом
; установки (или поправить путь ниже). Без него на "чистых"/непропатченных
; слабых ПК StormBrowser.exe может вообще не запускаться (ошибка вида
; "не найден VCRUNTIME140.dll" / "0xc000007b") — визуально это выглядит
; точно так же, как "браузер не открывается".
Source: "D:\Projects\StormBrowser\vc_redist.x64.exe"; DestDir: "{tmp}"; Flags: ignoreversion

[Icons]
; Ярлыки
Name: "{group}\Storm Browser"; Filename: "{app}\StormBrowser.exe"; IconFilename: "{app}\StormBrowser.exe"
Name: "{autodesktop}\Storm Browser"; Filename: "{app}\StormBrowser.exe"; IconFilename: "{app}\StormBrowser.exe"; Tasks: desktopicon

[Registry]
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; \
  ValueType: string; ValueName: "StormBrowserUpdater"; \
  ValueData: """{app}\StormUpdater.exe"""; Flags: uninsdeletevalue

; Пишем версию туда же, где её ищет StormUpdater.exe
Root: HKCU; Subkey: "Software\Shtorm Software\StormBrowser\browser"; \
  ValueType: string; ValueName: "installed_version"; \
  ValueData: "{#MyAppVersion}"; Flags: uninsdeletevalue

[Run]
; Ставим Visual C++ Redistributable ДО первого запуска браузера, и только
; если он ещё не установлен (проверка — VCRedistNeedsInstall в [Code]).
; waituntilterminated — ждём завершения, т.к. StormBrowser.exe линкован
; против этих же рантайм-DLL и без них просто не стартует.
Filename: "{tmp}\vc_redist.x64.exe"; Parameters: "/install /quiet /norestart"; \
  StatusMsg: "Устанавливаем компоненты Visual C++ (нужны для работы браузера)..."; \
  Check: VCRedistNeedsInstall; Flags: waituntilterminated

Filename: "{app}\StormBrowser.exe"; Description: "{cm:LaunchProgram,Storm Browser}"; Flags: nowait postinstall

; А вот здесь skipifsilent НЕ нужен
Filename: "{app}\StormUpdater.exe"; Flags: nowait runhidden

[UninstallDelete]
; Очистка папок Qt (которые создает Chromium) при удалении
Type: filesandordirs; Name: "{app}\cache"
Type: filesandordirs; Name: "{app}\GPUCache"
Type: filesandordirs; Name: "{app}\logs"
; Удаляем папку исходников словаря при деинсталляции
Type: filesandordirs; Name: "{app}\spellcheck_src"
Type: dirifempty; Name: "{app}"

[Code]
var
  g_IsUpgrade: Boolean;

// Проверяет ключ реестра, куда официальный VC++ 2015-2022 (14.x) x64
// Redistributable пишет "Installed"=1 после успешной установки. Если ключа
// нет вообще или значение не 1 — редистрибутив точно не стоит (типичная
// ситуация на старых/непропатченных ПК, которые никогда не ставили
// современные Visual Studio рантаймы).
function VCRedistNeedsInstall(): Boolean;
var
  installed: Cardinal;
begin
  // Это тихое фоновое авто-обновление уже установленного и рабочего
  // браузера (см. UpdateManager::cleanShutdownAndRunInstaller — он
  // запускает этот же инсталлятор с /VERYSILENT). Раз StormBrowser.exe
  // уже стоял и успешно запускался — CRT у него 100% уже есть. Пропускаем
  // проверку целиком, чтобы даже теоретически не словить внезапный
  // UAC-запрос на установку vc_redist.x64.exe посреди фонового апдейта.
  if g_IsUpgrade then begin
    Result := False;
    exit;
  end;

  if RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\X64', 'Installed', installed) then
    Result := (installed <> 1)
  else
    Result := True;
end;

function InitializeSetup(): Boolean;
var
  ErrorCode: Integer;
begin
  // Определяем ДО копирования новых файлов: если exe по пути {app} уже
  // существует — это обновление поверх рабочей установки, а не первая
  // установка "в чистое поле".
  g_IsUpgrade := FileExists(ExpandConstant('{app}\StormBrowser.exe'));

  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM StormBrowser.exe', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM QtWebEngineProcess.exe', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM xray.exe', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM StormUpdater.exe', '', SW_HIDE, ewWaitUntilTerminated, ErrorCode);

  Result := True;
end;