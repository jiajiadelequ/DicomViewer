#include "casepackagereader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStringList>

#include <gdcmException.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>

#include <algorithm>
#include <limits>
#include <vector>

namespace
{
using DicomImageIOType = itk::GDCMImageIO;
using DicomNamesGeneratorType = itk::GDCMSeriesFileNames;

bool isCancelled(const StudyLoadFeedback *feedback)
{
    return feedback != nullptr && feedback->isCancelled && feedback->isCancelled();
}

void reportProgress(const StudyLoadFeedback *feedback, const QString &message, int percent)
{
    if (feedback != nullptr && feedback->reportProgress) {
        feedback->reportProgress(message, percent);
    }
}

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

struct NiftiFileMatch
{
    QString filePath;
    int depth = std::numeric_limits<int>::max();
    bool preferredDirectory = false;

    [[nodiscard]] bool isValid() const
    {
        return !filePath.isEmpty();
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

bool isLikelyDicomByName(const QFileInfo &fileInfo)
{
    const QString suffix = fileInfo.suffix().toLower();
    return suffix.isEmpty()
        || suffix == QStringLiteral("dcm")
        || suffix == QStringLiteral("dicom")
        || suffix == QStringLiteral("ima");
}

bool isKnownNonDicomFile(const QFileInfo &fileInfo)
{
    static const QStringList suffixes {
        QStringLiteral("json"),
        QStringLiteral("nii"),
        QStringLiteral("obj"),
        QStringLiteral("stl"),
        QStringLiteral("ply"),
        QStringLiteral("vtp"),
        QStringLiteral("vtk"),
        QStringLiteral("txt"),
        QStringLiteral("csv"),
        QStringLiteral("xml"),
        QStringLiteral("jpg"),
        QStringLiteral("jpeg"),
        QStringLiteral("png"),
        QStringLiteral("bmp"),
        QStringLiteral("gif"),
        QStringLiteral("log"),
        QStringLiteral("ini"),
        QStringLiteral("zip"),
        QStringLiteral("7z"),
        QStringLiteral("rar")
    };
    const QString suffix = fileInfo.suffix().toLower();
    return !suffix.isEmpty() && suffixes.contains(suffix);
}

bool canReadAsDicom(DicomImageIOType *imageIO, const QFileInfo &fileInfo)
{
    if (imageIO == nullptr || !fileInfo.exists() || !fileInfo.isFile()) {
        return false;
    }

    try {
        const std::string nativeFilePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath()).toStdString();
        return imageIO->CanReadFile(nativeFilePath.c_str());
    } catch (const itk::ExceptionObject &) {
        return false;
    } catch (const gdcm::Exception &) {
        return false;
    } catch (const std::exception &) {
        return false;
    }
}

bool shouldSkipCandidateDirectory(const QString &directoryName)
{
    static const QStringList skippedNames {
        QStringLiteral("model"),
        QStringLiteral("models"),
        QStringLiteral("meta"),
        QStringLiteral("__macosx")
    };
    return skippedNames.contains(directoryName, Qt::CaseInsensitive);
}

bool directoryLooksLikeDicom(DicomImageIOType *imageIO, const QString &directoryPath)
{
    const QDir directory(directoryPath);
    const QFileInfoList files = directory.entryInfoList(QDir::Files | QDir::Readable | QDir::NoSymLinks,
                                                        QDir::Name | QDir::IgnoreCase);
    if (files.isEmpty()) {
        return false;
    }

    int likelyProbeCount = 0;
    for (const QFileInfo &fileInfo : files) {
        if (!isLikelyDicomByName(fileInfo)) {
            continue;
        }

        ++likelyProbeCount;
        if (canReadAsDicom(imageIO, fileInfo)) {
            return true;
        }

        if (likelyProbeCount >= 8) {
            return false;
        }
    }

    if (likelyProbeCount > 0) {
        return false;
    }

    int fallbackProbeCount = 0;
    for (const QFileInfo &fileInfo : files) {
        if (isKnownNonDicomFile(fileInfo)) {
            continue;
        }

        ++fallbackProbeCount;
        if (canReadAsDicom(imageIO, fileInfo)) {
            return true;
        }

        if (fallbackProbeCount >= 4) {
            break;
        }
    }

    return false;
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

bool hasPreferredNiftiDirectoryName(const QString &directoryPath)
{
    const QString name = QFileInfo(directoryPath).fileName();
    return name.compare(QStringLiteral("nifti"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("nii"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("image"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("images"), Qt::CaseInsensitive) == 0;
}

QStringList candidateDirectories(const QDir &rootDir)
{
    QStringList results;
    const QString rootPath = QDir::toNativeSeparators(rootDir.absolutePath());
    results.append(rootPath);

    QStringList pendingDirectories { rootPath };
    while (!pendingDirectories.isEmpty()) {
        const QString currentPath = pendingDirectories.takeLast();
        const QDir currentDir(currentPath);
        const QFileInfoList childDirectories = currentDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::NoSymLinks,
                                                                        QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &childInfo : childDirectories) {
            if (shouldSkipCandidateDirectory(childInfo.fileName())) {
                continue;
            }

            const QString childPath = QDir::toNativeSeparators(childInfo.absoluteFilePath());
            results.append(childPath);
            pendingDirectories.append(childPath);
        }
    }

    results.removeDuplicates();
    return results;
}

bool sameDirectoryPath(const QString &lhs, const QString &rhs)
{
    return QString::compare(QDir::fromNativeSeparators(lhs),
                            QDir::fromNativeSeparators(rhs),
                            Qt::CaseInsensitive) == 0;
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
    } catch (const gdcm::Exception &) {
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

bool isBetterNiftiMatch(const NiftiFileMatch &candidate, const NiftiFileMatch &currentBest)
{
    if (!currentBest.isValid()) {
        return candidate.isValid();
    }

    if (candidate.preferredDirectory != currentBest.preferredDirectory) {
        return candidate.preferredDirectory;
    }

    if (candidate.depth != currentBest.depth) {
        return candidate.depth < currentBest.depth;
    }

    return QString::compare(candidate.filePath, currentBest.filePath, Qt::CaseInsensitive) < 0;
}

bool isLikelyNiftiFile(const QFileInfo &fileInfo)
{
    const QString fileName = fileInfo.fileName().toLower();
    return fileName.endsWith(QStringLiteral(".nii"))
        || fileName.endsWith(QStringLiteral(".nii.gz"));
}

DicomDirectoryMatch inspectDicomDirectory(const QDir &rootDir,
                                         const QString &directoryPath,
                                         DicomImageIOType *probeImageIO,
                                         const StudyLoadFeedback *feedback)
{
    DicomDirectoryMatch candidate;
    if (directoryPath.isEmpty()
        || probeImageIO == nullptr
        || isCancelled(feedback)
        || !directoryLooksLikeDicom(probeImageIO, directoryPath)) {
        return candidate;
    }

    const QStringList seriesFiles = largestSeriesFilesInDirectory(directoryPath);
    if (seriesFiles.isEmpty()) {
        return candidate;
    }

    candidate.directoryPath = directoryPath;
    candidate.files = seriesFiles;
    candidate.fileCount = seriesFiles.size();
    candidate.depth = directoryDepth(rootDir, directoryPath);
    candidate.preferredName = hasPreferredDicomDirectoryName(directoryPath);
    return candidate;
}

DicomDirectoryMatch resolveDicomDirectory(const QDir &rootDir, const StudyLoadFeedback *feedback)
{
    DicomDirectoryMatch bestMatch;
    const QString rootPath = QDir::toNativeSeparators(rootDir.absolutePath());
    const QString preferredPath = findChildDirectory(rootDir, {QStringLiteral("dicom"), QStringLiteral("dicoms")});
    auto probeImageIO = DicomImageIOType::New();

    reportProgress(feedback, QStringLiteral("正在扫描病例目录中的 DICOM 序列..."), 10);

    if (!preferredPath.isEmpty()) {
        const DicomDirectoryMatch preferredMatch = inspectDicomDirectory(rootDir, preferredPath, probeImageIO, feedback);
        if (preferredMatch.isValid() || isCancelled(feedback)) {
            return preferredMatch;
        }
    }

    const DicomDirectoryMatch rootMatch = inspectDicomDirectory(rootDir, rootPath, probeImageIO, feedback);
    if (rootMatch.isValid() || isCancelled(feedback)) {
        return rootMatch;
    }
    if (rootMatch.isValid()) {
        bestMatch = rootMatch;
    }

    const QStringList directories = candidateDirectories(rootDir);
    const int totalCandidates = std::max(1, directories.size());
    int scannedCandidateCount = 0;
    for (const QString &directoryPath : directories) {
        if (sameDirectoryPath(directoryPath, rootPath)
            || (!preferredPath.isEmpty() && sameDirectoryPath(directoryPath, preferredPath))) {
            continue;
        }

        if (isCancelled(feedback)) {
            return {};
        }

        ++scannedCandidateCount;
        const int percent = 10 + static_cast<int>((25.0 * scannedCandidateCount) / totalCandidates);
        reportProgress(feedback,
                       QStringLiteral("正在扫描 DICOM 候选目录...\n%1").arg(directoryPath),
                       percent);

        const DicomDirectoryMatch candidate = inspectDicomDirectory(rootDir, directoryPath, probeImageIO, feedback);
        if (isBetterDicomMatch(candidate, bestMatch)) {
            bestMatch = candidate;
        }
    }

    return bestMatch;
}

NiftiFileMatch inspectNiftiDirectory(const QDir &rootDir,
                                    const QString &directoryPath,
                                    const StudyLoadFeedback *feedback)
{
    NiftiFileMatch bestMatch;
    if (directoryPath.isEmpty() || isCancelled(feedback)) {
        return bestMatch;
    }

    const QDir directory(directoryPath);
    const QFileInfoList files = directory.entryInfoList({QStringLiteral("*.nii"), QStringLiteral("*.nii.gz")},
                                                        QDir::Files | QDir::Readable | QDir::NoSymLinks,
                                                        QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &fileInfo : files) {
        if (!isLikelyNiftiFile(fileInfo)) {
            continue;
        }

        NiftiFileMatch candidate;
        candidate.filePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        candidate.depth = directoryDepth(rootDir, directoryPath);
        candidate.preferredDirectory = hasPreferredNiftiDirectoryName(directoryPath);
        if (isBetterNiftiMatch(candidate, bestMatch)) {
            bestMatch = candidate;
        }
    }

    return bestMatch;
}

NiftiFileMatch resolveNiftiFile(const QDir &rootDir, const StudyLoadFeedback *feedback)
{
    NiftiFileMatch bestMatch;
    const QString rootPath = QDir::toNativeSeparators(rootDir.absolutePath());
    const QString preferredPath = findChildDirectory(rootDir, {
        QStringLiteral("nifti"),
        QStringLiteral("nii"),
        QStringLiteral("image"),
        QStringLiteral("images")
    });

    reportProgress(feedback, QStringLiteral("正在扫描病例目录中的 NIfTI 文件..."), 30);

    if (!preferredPath.isEmpty()) {
        const NiftiFileMatch preferredMatch = inspectNiftiDirectory(rootDir, preferredPath, feedback);
        if (preferredMatch.isValid() || isCancelled(feedback)) {
            return preferredMatch;
        }
    }

    const NiftiFileMatch rootMatch = inspectNiftiDirectory(rootDir, rootPath, feedback);
    if (rootMatch.isValid() || isCancelled(feedback)) {
        return rootMatch;
    }

    const QStringList directories = candidateDirectories(rootDir);
    const int totalCandidates = std::max(1, directories.size());
    int scannedCandidateCount = 0;
    for (const QString &directoryPath : directories) {
        if (sameDirectoryPath(directoryPath, rootPath)
            || (!preferredPath.isEmpty() && sameDirectoryPath(directoryPath, preferredPath))) {
            continue;
        }

        if (isCancelled(feedback)) {
            return {};
        }

        ++scannedCandidateCount;
        const int percent = 30 + static_cast<int>((5.0 * scannedCandidateCount) / totalCandidates);
        reportProgress(feedback,
                       QStringLiteral("正在扫描 NIfTI 候选目录...\n%1").arg(directoryPath),
                       percent);

        const NiftiFileMatch candidate = inspectNiftiDirectory(rootDir, directoryPath, feedback);
        if (isBetterNiftiMatch(candidate, bestMatch)) {
            bestMatch = candidate;
        }
    }

    return bestMatch;
}
}

StudyPackage CasePackageReader::readFromDirectory(const QString &rootPath,
                                                  QString *errorMessage,
                                                  const StudyLoadFeedback *feedback) const
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
    const DicomDirectoryMatch dicomMatch = resolveDicomDirectory(rootDir, feedback);
    if (isCancelled(feedback)) {
        return package;
    }
    const NiftiFileMatch niftiMatch = dicomMatch.isValid() ? NiftiFileMatch {} : resolveNiftiFile(rootDir, feedback);
    if (isCancelled(feedback)) {
        return package;
    }

    reportProgress(feedback, QStringLiteral("正在整理病例目录中的模型和场景文件..."), 35);
    const QString modelPath = findChildDirectory(rootDir, {QStringLiteral("model"), QStringLiteral("models")});
    const QString metaPath = findChildDirectory(rootDir, {QStringLiteral("meta")});
    const QString sceneFilePath = metaPath.isEmpty()
        ? QString()
        : QDir(metaPath).filePath(QStringLiteral("scene.json"));

    if (dicomMatch.isValid()) {
        package.dicomPath = QDir::toNativeSeparators(dicomMatch.directoryPath);
        package.dicomFiles = dicomMatch.files;
    }
    if (niftiMatch.isValid()) {
        package.niftiFilePath = niftiMatch.filePath;
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
        *errorMessage = QStringLiteral("未在目录中找到可识别的数据。请选择包含 DICOM 序列、NIfTI 文件，或包含 dicom/、model/ 子目录的病例目录。");
    }

    return package;
}
