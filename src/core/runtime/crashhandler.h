#pragma once

#include <QString>

namespace CrashHandler
{
void initialize(const QString &applicationName);
QString logDirectoryPath();
QString dumpDirectoryPath();
}
