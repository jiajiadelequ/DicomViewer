#pragma once

#include "src/model/studypackage.h"

#include <QString>

class CasePackageReader
{
public:
    [[nodiscard]] StudyPackage readFromDirectory(const QString &rootPath, QString *errorMessage = nullptr) const;
};
