#pragma once

#include <QString>
#include <QStringList>

struct StudyPackage
{
    QString rootPath;
    QString dicomPath;
    QString modelPath;
    QString metaPath;
    QString sceneFilePath;
    QStringList dicomFiles;
    QStringList modelFiles;

    [[nodiscard]] bool isValid() const
    {
        return !rootPath.isEmpty() && (!dicomFiles.isEmpty() || !modelFiles.isEmpty());
    }
};
