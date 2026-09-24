#pragma once
#include <QString>

// Полноценная страница storm://downloads — вся история загрузок сразу (и
// обычные, и торренты, без разделения на вкладки, в отличие от мини-попапа
// DownloadManager). Свободная функция, а не метод PageTemplates — тот же
// паттерн, что уже используется для getBookmarksHtml()/getHelpHtml()
// (см. MainWindow_Tabs.cpp). Данные и действия — через QWebChannel-мост
// "downloadsBridge" (см. DownloadsBridge.h), сама HTML статична.
QString getDownloadsHtml();