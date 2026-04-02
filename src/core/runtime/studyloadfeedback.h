#pragma once

#include <QString>

#include <functional>

struct StudyLoadFeedback
{
    std::function<void(const QString &message, int percent)> reportProgress;
    std::function<bool()> isCancelled;
};
