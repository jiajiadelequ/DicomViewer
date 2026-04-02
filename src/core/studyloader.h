#pragma once

#include "src/core/runtime/studyloadresult.h"

class StudyLoader
{
public:
    [[nodiscard]] static StudyLoadResult loadFromDirectory(const QString &rootPath);
};
