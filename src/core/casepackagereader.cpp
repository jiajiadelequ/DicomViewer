#include "casepackagereader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

namespace
{
QString findChildDirectory(const QDir &rootDir, const QStringList &candidates)
{
    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &candidate : candidates) {
        for (const QFileInfo &entry : entries) {
            if (entry.fileName().compare(candidate, Qt::CaseInsensitive) == 0) {
                return entry.absoluteFilePath();
            }
        }
    }

    return QString();
}

QStringList collectFiles(const QString &directoryPath, const QStringList &filters)
{
    QStringList results;
    QDirIterator iterator(directoryPath, filters, QDir::Files, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        results.append(QDir::toNativeSeparators(iterator.next()));
    }
    results.sort(Qt::CaseInsensitive);
    return results;
}
}

StudyPackage CasePackageReader::readFromDirectory(const QString &rootPath, QString *errorMessage) const
{
    StudyPackage package;

    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("病例目录不存在: %1").arg(QDir::toNativeSeparators(rootPath));
        }
        return package;
    }

    package.rootPath = QDir::toNativeSeparators(rootInfo.absoluteFilePath());

    const QDir rootDir(rootInfo.absoluteFilePath());
    const QString dicomPath = findChildDirectory(rootDir, {QStringLiteral("dicom")});
    const QString modelPath = findChildDirectory(rootDir, {QStringLiteral("model"), QStringLiteral("models")});
    const QString metaPath = findChildDirectory(rootDir, {QStringLiteral("meta")});
    const QString sceneFilePath = QDir(metaPath).filePath(QStringLiteral("scene.json"));

    if (QFileInfo::exists(dicomPath) && QFileInfo(dicomPath).isDir()) {
        package.dicomPath = QDir::toNativeSeparators(dicomPath);
        package.dicomFiles = collectFiles(dicomPath, {QStringLiteral("*.dcm"), QStringLiteral("*.dicom"), QStringLiteral("*")});
    }

    if (QFileInfo::exists(modelPath) && QFileInfo(modelPath).isDir()) {
        package.modelPath = QDir::toNativeSeparators(modelPath);
        package.modelFiles = collectFiles(modelPath, {
            QStringLiteral("*.stl"),
            QStringLiteral("*.obj"),
            QStringLiteral("*.ply"),
            QStringLiteral("*.vtp"),
            QStringLiteral("*.vtk")
        });
    }

    if (QFileInfo::exists(metaPath) && QFileInfo(metaPath).isDir()) {
        package.metaPath = QDir::toNativeSeparators(metaPath);
    }

    if (QFileInfo::exists(sceneFilePath) && QFileInfo(sceneFilePath).isFile()) {
        package.sceneFilePath = QDir::toNativeSeparators(sceneFilePath);
    }

    if (!package.isValid() && errorMessage != nullptr) {
        *errorMessage = QStringLiteral("未在目录中找到可识别的数据。期望存在 dicom/ 或 model/ 子目录。");
    }

    return package;
}
