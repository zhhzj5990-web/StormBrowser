# Указываем минимально необходимую версию CMake.
cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0167 NEW)

# Объявляем проект и сразу указываем язык (C++).
project(StormBrowser VERSION 1.0.0 LANGUAGES CXX)

# --- Настройки языка C++ ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON) # Не позволяет использовать более старый стандарт
set(CMAKE_CXX_EXTENSIONS OFF)      # Отключаем нестандартные расширения компилятора

# --- Настройки для работы с Qt6 ---
# Включаем автоматическую обработку файлов Qt (MOC, UIC, RCC)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Отключаем поиск Vulkan, чтобы Qt не требовал Vulkan SDK при конфигурации
set(CMAKE_DISABLE_FIND_PACKAGE_Vulkan ON)
set(CMAKE_DISABLE_FIND_PACKAGE_WrapVulkanHeaders ON)

# Ищем необходимые компоненты Qt6.
find_package(Qt6 REQUIRED COMPONENTS 
    Core
    Gui
    Widgets
    WebEngineWidgets
    Sql
    Network
    Multimedia
    PrintSupport
)

# Ищем OpenSSL до подключения торрент-библиотеки
find_package(OpenSSL REQUIRED)

# Ищем библиотеки для торрентов
find_package(LibtorrentRasterbar CONFIG REQUIRED)

# --- Сборка исполняемого файла ---
# Сначала объявляем цель (исполняемый файл), а ЗАТЕМ линкуем к ней библиотеки
add_executable(StormBrowser WIN32
    main.cpp
    app_icon.rc
    resources.qrc

    # Основные окна и менеджеры
    MainWindow.cpp
    MainWindow.h
    MainWindow_UiHelpers.h
    MainWindow_Navigation.cpp
    MainWindow_Tabs.cpp
    MainWindow_Bookmarks.cpp
    MainWindow_Passwords.cpp
    MainWindow_Settings.cpp
    MainWindow_View.cpp
    DatabaseManager.cpp
    DatabaseManager.h
    ThemeManager.cpp
    ThemeManager.h
    ScreenshotEditor.cpp
    ScreenshotEditor.h
    DownloadManager.cpp
    DownloadManager.h
    DownloadsBridge.cpp
    DownloadsBridge.h
    DownloadsPageHtml.h
    DownloadsPageHtml.cpp
    StormCloud.cpp
    StormCloud.h
    StormWebPage.cpp
    StormWebPage.h
    HelpPageHtml.h
    UpdateManager.cpp
    UpdateManager.h
    SingleInstanceGuard.cpp
    SingleInstanceGuard.h
    Logger.cpp
    Logger.h
    ProxyManager.cpp       
    ProxyManager.h
    GameModeManager.cpp
    GameModeManager.h
    AdblockManager.cpp
    AdblockManager.h
    ShieldInterceptor.cpp
    ShieldInterceptor.h
    CertificateManager.h
    CertificateManager.cpp
    HomeAIBridge.cpp
    HomeAIBridge.h
    HomeBridge.cpp
    HomeBridge.h
    BrowserWebView.h
    BrowserWebView.cpp
    AnalyticsClient.h
    AnalyticsClient.cpp
    TabSpinner.h
    TabSpinner.cpp
    CustomMenuPanel.h
    CustomMenuPanel.cpp 
    WebPageAgent.h
    WebPageAgent.cpp

    # Элементы интерфейса
    BrowserTopBar.cpp
    BrowserTopBar.h
    BookmarksBar.cpp
    BookmarksBar.h
    BookmarksPageHtml.h
    BookmarksReorderHelper.h
    BookmarksBridge.cpp
    BookmarksBridge.h
    Sidebar.cpp
    Sidebar.h
    MenuBuilder.cpp
    MenuBuilder.h
    PasswordManager.cpp
    PasswordManager.h
    PasswordSuggestBridge.h
    PasswordCaptureBridge.cpp
    PasswordCaptureBridge.h
    FramelessDialog.cpp
    FramelessDialog.h
    CryptoManager.cpp
    CryptoManager.h
    StormTabBar.h
    StormTabBar.cpp
       
    # Виджеты боковой панели
    AIAssistantWidget.cpp
    AIAssistantWidget.h
    NotesWidget.cpp
    NotesWidget.h
    TodoWidget.cpp
    TodoWidget.h
    PomodoroWidget.cpp
    PomodoroWidget.h
    NewsWidget.cpp
    NewsWidget.h
    GamesWidget.cpp
    GamesWidget.h
    TranslatorWidget.cpp
    TranslatorWidget.h
    VoiceChatWidget.cpp
    VoiceChatWidget.h
    WeatherWidget.cpp
    WeatherWidget.h
    FreeGamesWidget.cpp
    FreeGamesWidget.h
    EducationWidget.cpp
    EducationWidget.h
    ArcadeWidget.cpp
    ArcadeWidget.h
    ReaderWidget.cpp
    ReaderWidget.h
    ResearchWidget.cpp
    ResearchWidget.h
    ResearchManager.cpp
    ResearchManager.h
    AiClient.cpp
    AiClient.h
    TalkWidget.cpp
    TalkWidget.h
    SettingsBridge.h
    SettingsBridge.cpp
    StormCloudBridge.h
    StormCloudBridge.cpp
    FernetCrypto.h
    FernetCrypto.cpp
    PageTemplates.h
    PageTemplates.cpp
    PageTemplates_Internal.h
    PageTemplates_Home.cpp
    PageTemplates_Cloud.cpp
    PageTemplates_Cloud2.cpp
    PageTemplates_Talk.cpp
    PageTemplates_Talk2.cpp
    TalkBridge.h
    TalkBridge.cpp
    TalkPipOverlay.h
    TalkPipOverlay.cpp
    TodoTask.h
    TodoTask.cpp
    TaskItemWidget.h
    TaskItemWidget.cpp
    SettingsPageHtml.h
    SettingsPageHtml.cpp

    # SMM Auto-Publisher
    SmmTypes.h
    PostQueueManager.h
    PostQueueManager.cpp
    PlatformToggleButton.h
    PlatformToggleButton.cpp
    QueueItemWidget.h
    QueueItemWidget.cpp
    SmmAutoPublisherWidget.h
    SmmAutoPublisherWidget.cpp
    SmmPublishController.h
    SmmPublishController.cpp
    AiAgentTaskRunner.h
    AiAgentTaskRunner.cpp
)

if(MSVC)
    target_compile_options(StormBrowser PRIVATE /wd26813)
endif()

# --- Связывание библиотек --
target_link_libraries(StormBrowser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::WebEngineWidgets
    Qt6::Sql
    Qt6::Network
    Qt6::Multimedia
    Qt6::PrintSupport
    LibtorrentRasterbar::torrent-rasterbar
    OpenSSL::SSL
    OpenSSL::Crypto    
)

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/resources 
     DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/system_core"
    "$<TARGET_FILE_DIR:StormBrowser>/system_core"
)

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/qtwebengine_dictionaries"
    "$<TARGET_FILE_DIR:StormBrowser>/qtwebengine_dictionaries"
)

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "D:/Programs/Qt/6.9.3/msvc2022_64/resources"
    "$<TARGET_FILE_DIR:StormBrowser>"
    COMMENT "Копирование pak-ресурсов WebEngine..."
)

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "D:/Programs/Qt/6.9.3/msvc2022_64/translations/qtwebengine_locales"
    "$<TARGET_FILE_DIR:StormBrowser>/locales"
    COMMENT "Копирование локалей Chromium..."
)

find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "D:/Programs/Qt/6.9.3/msvc2022_64/bin")

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${WINDEPLOYQT_EXECUTABLE} 
            --no-translations 
            --no-system-d3d-compiler 
            "$<TARGET_FILE:StormBrowser>"
    COMMENT "Running windeployqt to copy Qt WebEngine dependencies..."
)

add_custom_command(TARGET StormBrowser POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "D:/Programs/Qt/6.9.3/msvc2022_64/bin/libcrypto-3-x64.dll"
    "$<TARGET_FILE_DIR:StormBrowser>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "D:/Programs/Qt/6.9.3/msvc2022_64/bin/libssl-3-x64.dll"
    "$<TARGET_FILE_DIR:StormBrowser>"
    COMMENT "Копирование библиотек OpenSSL..."
)