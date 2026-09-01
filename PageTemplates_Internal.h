#pragma once
// ==========================================================================
// PageTemplates_Internal.h
// Огромные HTML/CSS/JS-шаблоны Storm Cloud и Storm Talk физически
// разбиты на несколько .cpp-файлов, чтобы ни один файл не раздувался
// на тысячи строк. Разбиение чисто механическое — по границам строк
// текста ВНУТРИ исходного raw-string литерала, само содержимое
// шаблонов не менялось ни на символ.
// getStormCloudHtml()/getTalkHtml() просто склеивают куски оператором
// '+' перед тем как (для Cloud) применить цепочку .replace(...).
// ==========================================================================

#include <QString>

QString buildStormCloudHtmlPart1();
QString buildStormCloudHtmlPart2();

QString buildTalkHtmlPart1();
QString buildTalkHtmlPart2();
