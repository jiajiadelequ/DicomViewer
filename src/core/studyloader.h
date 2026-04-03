#pragma once

#include "src/core/runtime/studyloadfeedback.h"
#include "src/core/runtime/studyloadresult.h"

class StudyLoader
{
public:
    [[nodiscard]] static StudyLoadResult loadFromDirectory(const QString &rootPath,
                                                           const StudyLoadFeedback &feedback = StudyLoadFeedback {});
    [[nodiscard]] static StudyLoadResult loadFromFile(const QString &filePath,
                                                      const StudyLoadFeedback &feedback = StudyLoadFeedback {});
};
