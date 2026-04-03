#pragma once

#include <QString>
#include <QStringList>

struct StudyPackage
{
    QString rootPath;
    QString dicomPath;
    QString niftiFilePath;
    QString modelPath;
    QString metaPath;
    QString sceneFilePath;
    QStringList dicomFiles;
    QStringList modelFiles;

    [[nodiscard]] bool hasDicomVolume() const
    {
        return !dicomFiles.isEmpty();
    }

    [[nodiscard]] bool hasNiftiVolume() const
    {
        return !niftiFilePath.isEmpty();
    }

    [[nodiscard]] bool hasVolumeData() const
    {
        return hasDicomVolume() || hasNiftiVolume();
    }

    [[nodiscard]] bool isValid() const
    {
        return !rootPath.isEmpty() && (hasVolumeData() || !modelFiles.isEmpty());
    }
};
