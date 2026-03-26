#include "casepackagereader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

#include <itkGDCMSeriesFileNames.h>

#include <limits>
#include <vector>

namespace
{
using DicomNamesGeneratorType = itk::GDCMSeriesFileNames;

struct DicomDirectoryMatch
{
    QString directoryPath;
    QStringList files;
    int fileCount = 0;
    int depth = std::numeric_limits<int>::max();
    bool preferredName = false;

    [[nodiscard]] bool isValid() const
    {
        return !directoryPath.isEmpty() && fileCount > 0;
    }
};

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

bool hasPreferredDicomDirectoryName(const QString &directoryPath)
{
    const QString name = QFileInfo(directoryPath).fileName();
    return name.compare(QStringLiteral("dicom"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("dicoms"), Qt::CaseInsensitive) == 0;
}

int directoryDepth(const QDir &rootDir, const QString &directoryPath)
{
    const QString relativePath = QDir::fromNativeSeparators(rootDir.relativeFilePath(directoryPath));
    if (relativePath.isEmpty() || relativePath == QStringLiteral(".")) {
        return 0;
    }

    return relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts).size();
}

QStringList candidateDirectories(const QDir &rootDir)
{
    QStringList results;
    results.append(QDir::toNativeSeparators(rootDir.absolutePath()));

    QDirIterator iterator(rootDir.absolutePath(),
                          QDir::Dirs | QDir::NoDotAndDotDot,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        results.append(QDir::toNativeSeparators(iterator.next()));
    }

    results.removeDuplicates();
    return results;
}

QStringList largestSeriesFilesInDirectory(const QString &directoryPath)
{
    try {
        auto namesGenerator = DicomNamesGeneratorType::New();
        namesGenerator->SetUseSeriesDetails(true);
        namesGenerator->SetDirectory(QDir::toNativeSeparators(directoryPath).toStdString());

        const auto &seriesUIDs = namesGenerator->GetSeriesUIDs();
        QStringList largestSeriesFiles;
        for (const auto &seriesUID : seriesUIDs) {
            const auto &fileNames = namesGenerator->GetFileNames(seriesUID);
            if (static_cast<int>(fileNames.size()) <= largestSeriesFiles.size()) {
                continue;
            }

            QStringList seriesFiles;
            seriesFiles.reserve(static_cast<int>(fileNames.size()));
            for (const std::string &fileName : fileNames) {
                seriesFiles.append(QDir::toNativeSeparators(QString::fromStdString(fileName)));
            }
            largestSeriesFiles = seriesFiles;
        }

        return largestSeriesFiles;
    } catch (const itk::ExceptionObject &) {
        return {};
    } catch (const std::exception &) {
        return {};
    }
}

bool isBetterDicomMatch(const DicomDirectoryMatch &candidate, const DicomDirectoryMatch &currentBest)
{
    if (!currentBest.isValid()) {
        return candidate.isValid();
    }

    if (candidate.fileCount != currentBest.fileCount) {
        return candidate.fileCount > currentBest.fileCount;
    }

    if (candidate.preferredName != currentBest.preferredName) {
        return candidate.preferredName;
    }

    if (candidate.depth != currentBest.depth) {
        return candidate.depth < currentBest.depth;
    }

    return QString::compare(candidate.directoryPath, currentBest.directoryPath, Qt::CaseInsensitive) < 0;
}

DicomDirectoryMatch resolveDicomDirectory(const QDir &rootDir)
{
    DicomDirectoryMatch bestMatch;

    for (const QString &directoryPath : candidateDirectories(rootDir)) {
        const QStringList seriesFiles = largestSeriesFilesInDirectory(directoryPath);
        if (seriesFiles.isEmpty()) {
            continue;
        }

        DicomDirectoryMatch candidate;
        candidate.directoryPath = directoryPath;
        candidate.files = seriesFiles;
        candidate.fileCount = seriesFiles.size();
        candidate.depth = directoryDepth(rootDir, directoryPath);
        candidate.preferredName = hasPreferredDicomDirectoryName(directoryPath);

        if (isBetterDicomMatch(candidate, bestMatch)) {
            bestMatch = candidate;
        }
    }

    return bestMatch;
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
    const DicomDirectoryMatch dicomMatch = resolveDicomDirectory(rootDir);
    const QString modelPath = findChildDirectory(rootDir, {QStringLiteral("model"), QStringLiteral("models")});
    const QString metaPath = findChildDirectory(rootDir, {QStringLiteral("meta")});
    const QString sceneFilePath = QDir(metaPath).filePath(QStringLiteral("scene.json"));

    if (dicomMatch.isValid()) {
        package.dicomPath = QDir::toNativeSeparators(dicomMatch.directoryPath);
        package.dicomFiles = dicomMatch.files;
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
        *errorMessage = QStringLiteral("未在目录中找到可识别的数据。请选择任意包含 DICOM 序列的目录，或包含 dicom/、model/ 子目录的病例目录。");
    }

    return package;
}
