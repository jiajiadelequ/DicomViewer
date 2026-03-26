#pragma once

#include "src/model/studyloadresult.h"

class StudyLoader
{
public:
    [[nodiscard]] static StudyLoadResult loadFromDirectory(const QString &rootPath);
};
