#include "src/core/casepackagereader.h"
#include "src/core/studyloader.h"
#include "src/view/mprslicemath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

#include <itkImage.h>
#include <itkImageFileWriter.h>
#include <itkNiftiImageIO.h>

#include <vtkImageData.h>
#include <vtkMatrix3x3.h>
#include <vtkSmartPointer.h>

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
using TestNiftiImageType = itk::Image<short, 3>;

void writeTestNiftiFile(const QString &filePath,
                        const std::array<double, 3> &originValues,
                        const std::array<double, 3> &spacingValues,
                        const std::array<std::array<double, 3>, 3> &directionValues);
void writeTestObjFile(const QString &filePath);

[[noreturn]] void fail(const QString &message)
{
    std::cerr << message.toStdString() << std::endl;
    std::exit(1);
}

void expect(bool condition, const QString &message)
{
    if (!condition) {
        fail(message);
    }
}

bool nearlyEqual(double lhs, double rhs, double epsilon = 1e-6)
{
    return std::abs(lhs - rhs) <= epsilon;
}

void writeTestNiftiFile(const QString &filePath)
{
    const std::array<double, 3> defaultOrigin { 0.0, 0.0, 0.0 };
    const std::array<double, 3> defaultSpacing { 1.0, 1.5, 2.0 };
    const std::array<std::array<double, 3>, 3> defaultDirection {{
        {{ 1.0, 0.0, 0.0 }},
        {{ 0.0, 1.0, 0.0 }},
        {{ 0.0, 0.0, 1.0 }},
    }};

    writeTestNiftiFile(filePath, defaultOrigin, defaultSpacing, defaultDirection);
}

void writeTestNiftiFile(const QString &filePath,
                        const std::array<double, 3> &originValues,
                        const std::array<double, 3> &spacingValues,
                        const std::array<std::array<double, 3>, 3> &directionValues)
{
    auto image = TestNiftiImageType::New();
    TestNiftiImageType::IndexType start;
    start.Fill(0);
    TestNiftiImageType::SizeType size;
    size[0] = 2;
    size[1] = 2;
    size[2] = 2;

    TestNiftiImageType::RegionType region;
    region.SetIndex(start);
    region.SetSize(size);
    image->SetRegions(region);
    image->Allocate();
    image->FillBuffer(42);

    TestNiftiImageType::SpacingType spacing;
    spacing[0] = spacingValues[0];
    spacing[1] = spacingValues[1];
    spacing[2] = spacingValues[2];
    image->SetSpacing(spacing);

    TestNiftiImageType::PointType origin;
    origin[0] = originValues[0];
    origin[1] = originValues[1];
    origin[2] = originValues[2];
    image->SetOrigin(origin);

    TestNiftiImageType::DirectionType direction;
    for (unsigned int row = 0; row < 3; ++row) {
        for (unsigned int column = 0; column < 3; ++column) {
            direction(row, column) = directionValues[row][column];
        }
    }
    image->SetDirection(direction);

    auto writer = itk::ImageFileWriter<TestNiftiImageType>::New();
    writer->SetImageIO(itk::NiftiImageIO::New());
    writer->SetFileName(QDir::toNativeSeparators(filePath).toStdString());
    writer->SetInput(image);
    try {
        writer->Update();
    } catch (const itk::ExceptionObject &ex) {
        fail(QStringLiteral("写入测试 NIfTI 文件失败: %1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (const std::exception &ex) {
        fail(QStringLiteral("写入测试 NIfTI 文件失败: %1").arg(QString::fromLocal8Bit(ex.what())));
    }
}

void writeTestObjFile(const QString &filePath)
{
    QFile modelFile(filePath);
    expect(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
           QStringLiteral("无法创建测试 OBJ 模型文件。"));
    modelFile.write("v 0 0 0\n");
    modelFile.write("v 1 0 0\n");
    modelFile.write("v 0 1 0\n");
    modelFile.write("f 1 2 3\n");
    modelFile.close();
}

void testMprSliceMathRoundTrip()
{
    auto imageData = vtkSmartPointer<vtkImageData>::New();
    imageData->SetOrigin(10.0, 20.0, 30.0);
    imageData->SetSpacing(2.0, 3.0, 4.0);
    imageData->SetExtent(0, 3, 0, 2, 0, 1);
    imageData->AllocateScalars(VTK_SHORT, 1);

    const auto geometry = MprSliceMath::buildSliceGeometry(imageData, MprSliceMath::Orientation::Axial);
    expect(geometry.sliceCount == 2, QStringLiteral("Axial 几何的切片数不符合预期。"));
    expect(geometry.reverseSlider, QStringLiteral("Axial 几何应当启用反向滑条。"));
    expect(geometry.outputExtent[1] == 3, QStringLiteral("Axial 几何的 X 输出尺寸不符合预期。"));
    expect(geometry.outputExtent[3] == 2, QStringLiteral("Axial 几何的 Y 输出尺寸不符合预期。"));

    for (int sliderValue : {0, geometry.sliceCount - 1}) {
        const auto worldPosition = MprSliceMath::sliceOriginForSliderValue(geometry, sliderValue);
        const int roundTripValue = MprSliceMath::sliderValueForWorldPosition(geometry, worldPosition);
        expect(roundTripValue == sliderValue, QStringLiteral("滑条值与世界坐标换算没有保持往返一致。"));
    }

    const int centerValue = MprSliceMath::sliderValueForWorldPosition(geometry, geometry.center);
    expect(centerValue >= 0 && centerValue < geometry.sliceCount,
           QStringLiteral("中心点应当映射到有效的滑条范围内。"));

    const auto centerSliceOrigin = MprSliceMath::sliceOriginForSliderValue(geometry, centerValue);
    const auto offset = MprSliceMath::subtractAxes(centerSliceOrigin, geometry.center);
    expect(nearlyEqual(MprSliceMath::dotProduct(offset, geometry.xAxis), 0.0),
           QStringLiteral("中心切片原点不应沿切片 X 轴偏移。"));
    expect(nearlyEqual(MprSliceMath::dotProduct(offset, geometry.yAxis), 0.0),
           QStringLiteral("中心切片原点不应沿切片 Y 轴偏移。"));
}

void testCasePackageReaderWithModelAndMeta()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建临时目录。"));

    const QDir rootDir(tempDir.path());
    expect(rootDir.mkpath(QStringLiteral("model")), QStringLiteral("无法创建 model 目录。"));
    expect(rootDir.mkpath(QStringLiteral("meta")), QStringLiteral("无法创建 meta 目录。"));

    QFile modelFile(rootDir.filePath(QStringLiteral("model/surface.obj")));
    expect(modelFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
           QStringLiteral("无法创建测试模型文件。"));
    modelFile.write("# mock obj\n");
    modelFile.close();

    QFile sceneFile(rootDir.filePath(QStringLiteral("meta/scene.json")));
    expect(sceneFile.open(QIODevice::WriteOnly | QIODevice::Truncate),
           QStringLiteral("无法创建测试场景文件。"));
    sceneFile.write("{}\n");
    sceneFile.close();

    CasePackageReader reader;
    QString errorMessage;
    const StudyPackage package = reader.readFromDirectory(tempDir.path(), &errorMessage, nullptr);

    expect(errorMessage.isEmpty(), QStringLiteral("model/meta 场景不应产生错误信息。"));
    expect(package.isValid(), QStringLiteral("仅包含模型和场景文件的目录应被识别为有效病例包。"));
    expect(package.dicomFiles.isEmpty(), QStringLiteral("该测试目录不应识别出 DICOM 文件。"));
    expect(package.modelFiles.size() == 1, QStringLiteral("应当识别出一个模型文件。"));
    expect(package.sceneFilePath.endsWith(QStringLiteral("scene.json")),
           QStringLiteral("scene.json 应当被识别为场景配置文件。"));
}

void testCasePackageReaderMissingDirectory()
{
    CasePackageReader reader;
    QString errorMessage;
    const StudyPackage package = reader.readFromDirectory(QStringLiteral("Z:/this/path/should/not/exist"),
                                                          &errorMessage,
                                                          nullptr);

    expect(!package.isValid(), QStringLiteral("不存在的目录不应返回有效病例包。"));
    expect(!errorMessage.isEmpty(), QStringLiteral("不存在的目录应返回错误信息。"));
}

void testCasePackageReaderWithNiftiVolume()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建 NIfTI 测试临时目录。"));

    const QDir rootDir(tempDir.path());
    expect(rootDir.mkpath(QStringLiteral("nifti")), QStringLiteral("无法创建 nifti 目录。"));

    const QString niftiPath = rootDir.filePath(QStringLiteral("nifti/brain.nii.gz"));
    writeTestNiftiFile(niftiPath);

    CasePackageReader reader;
    QString errorMessage;
    const StudyPackage package = reader.readFromDirectory(tempDir.path(), &errorMessage, nullptr);

    expect(errorMessage.isEmpty(), QStringLiteral("包含 NIfTI 影像的目录不应产生错误信息。"));
    expect(package.isValid(), QStringLiteral("包含 NIfTI 影像的目录应被识别为有效病例包。"));
    expect(package.hasNiftiVolume(), QStringLiteral("目录中的 NIfTI 影像应被识别。"));
    expect(package.niftiFilePath.endsWith(QStringLiteral("brain.nii.gz")),
           QStringLiteral("应当记录被识别到的 NIfTI 文件路径。"));
    expect(package.dicomFiles.isEmpty(), QStringLiteral("该测试目录不应误识别出 DICOM 文件。"));
}

void testCasePackageReaderWithRootLevelModelsAndNiftiVolume()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建根目录模型测试临时目录。"));

    const QDir rootDir(tempDir.path());
    writeTestNiftiFile(rootDir.filePath(QStringLiteral("lung.nii")));
    writeTestObjFile(rootDir.filePath(QStringLiteral("surface.obj")));

    CasePackageReader reader;
    QString errorMessage;
    const StudyPackage package = reader.readFromDirectory(tempDir.path(), &errorMessage, nullptr);

    expect(errorMessage.isEmpty(), QStringLiteral("根目录同时包含 NIfTI 和模型文件时不应产生错误信息。"));
    expect(package.isValid(), QStringLiteral("根目录同时包含 NIfTI 和模型文件的目录应被识别为有效病例包。"));
    expect(package.hasNiftiVolume(), QStringLiteral("根目录中的 NIfTI 文件应被识别。"));
    expect(package.modelFiles.size() == 1, QStringLiteral("根目录中的模型文件应被识别。"));
    expect(package.modelPath == QDir::toNativeSeparators(rootDir.absolutePath()),
           QStringLiteral("根目录模型文件应把模型路径记录为病例根目录。"));
}

void testStudyLoaderWithUnicodeNiftiPath()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建 Unicode NIfTI 测试临时目录。"));

    const QDir rootDir(tempDir.path());
    expect(rootDir.mkpath(QStringLiteral("中文目录")), QStringLiteral("无法创建中文测试目录。"));

    const QString asciiNiftiPath = rootDir.filePath(QStringLiteral("ascii-source.nii"));
    writeTestNiftiFile(asciiNiftiPath);

    const QString niftiPath = rootDir.filePath(QStringLiteral("中文目录/lung.nii"));
    expect(QFile::copy(asciiNiftiPath, niftiPath), QStringLiteral("无法复制测试 NIfTI 文件到中文目录。"));

    const StudyLoadResult result = StudyLoader::loadFromFile(niftiPath);
    expect(result.succeeded(), QStringLiteral("中文目录下的 NIfTI 文件应可直接加载，错误: %1").arg(result.errorMessage));
    expect(result.imageData != nullptr, QStringLiteral("中文目录下的 NIfTI 文件应生成可显示影像。"));
    expect(result.package.hasNiftiVolume(), QStringLiteral("StudyLoader 应记录直接打开的 NIfTI 文件。"));
}

void testStudyLoaderNormalizesNiftiToInternalVolumeContract()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建 NIfTI 契约测试临时目录。"));

    const QDir rootDir(tempDir.path());
    const QString niftiPath = rootDir.filePath(QStringLiteral("geometry.nii"));
    const std::array<double, 3> origin { 10.0, 20.0, 30.0 };
    const std::array<double, 3> spacing { 1.0, 1.5, 2.0 };
    const std::array<std::array<double, 3>, 3> direction {{
        {{ 1.0, 0.0, 0.0 }},
        {{ 0.0, 1.0, 0.0 }},
        {{ 0.0, 0.0, 1.0 }},
    }};
    writeTestNiftiFile(niftiPath, origin, spacing, direction);

    const StudyLoadResult result = StudyLoader::loadFromFile(niftiPath);
    expect(result.succeeded(), QStringLiteral("NIfTI 文件应可成功加载，错误: %1").arg(result.errorMessage));
    expect(result.imageData != nullptr, QStringLiteral("NIfTI 文件应生成影像数据。"));

    double loadedOrigin[3] { 0.0, 0.0, 0.0 };
    double loadedSpacing[3] { 0.0, 0.0, 0.0 };
    result.imageData->GetOrigin(loadedOrigin);
    result.imageData->GetSpacing(loadedSpacing);
    expect(nearlyEqual(loadedOrigin[0], origin[0]) && nearlyEqual(loadedOrigin[1], origin[1]) && nearlyEqual(loadedOrigin[2], origin[2]),
           QStringLiteral("NIfTI 加载后的 origin 应与 ITK 写入语义一致。"));
    expect(nearlyEqual(loadedSpacing[0], spacing[0]) && nearlyEqual(loadedSpacing[1], spacing[1]) && nearlyEqual(loadedSpacing[2], spacing[2]),
           QStringLiteral("NIfTI 加载后的 spacing 应与写入值一致。"));

    auto *directionMatrix = result.imageData->GetDirectionMatrix();
    expect(directionMatrix != nullptr, QStringLiteral("NIfTI 加载后的方向矩阵不应为空。"));
    expect(nearlyEqual(directionMatrix->GetElement(0, 0), 1.0)
               && nearlyEqual(directionMatrix->GetElement(1, 1), 1.0)
               && nearlyEqual(directionMatrix->GetElement(2, 2), 1.0)
               && nearlyEqual(directionMatrix->GetElement(0, 1), 0.0)
               && nearlyEqual(directionMatrix->GetElement(0, 2), 0.0)
               && nearlyEqual(directionMatrix->GetElement(1, 0), 0.0)
               && nearlyEqual(directionMatrix->GetElement(1, 2), 0.0)
               && nearlyEqual(directionMatrix->GetElement(2, 0), 0.0)
               && nearlyEqual(directionMatrix->GetElement(2, 1), 0.0),
           QStringLiteral("NIfTI 加载后的方向矩阵应回到与 ITK/DICOM 一致的内部语义。"));
}

void testStudyLoaderLoadsSiblingModelsWhenOpeningNiftiFile()
{
    QTemporaryDir tempDir;
    expect(tempDir.isValid(), QStringLiteral("无法创建 NIfTI 同目录模型测试临时目录。"));

    const QDir rootDir(tempDir.path());
    expect(rootDir.mkpath(QStringLiteral("中文包")), QStringLiteral("无法创建中文模型测试目录。"));

    const QString asciiNiftiPath = rootDir.filePath(QStringLiteral("ascii-source.nii"));
    writeTestNiftiFile(asciiNiftiPath);

    const QString packagePath = rootDir.filePath(QStringLiteral("中文包"));
    const QString niftiPath = rootDir.filePath(QStringLiteral("中文包/lung.nii"));
    const QString modelPath = rootDir.filePath(QStringLiteral("中文包/surface.obj"));
    expect(QFile::copy(asciiNiftiPath, niftiPath), QStringLiteral("无法复制测试 NIfTI 文件到中文模型目录。"));
    expect(QFileInfo::exists(niftiPath), QStringLiteral("复制后的中文路径 NIfTI 文件应当存在。"));
    writeTestObjFile(modelPath);

    const StudyLoadResult result = StudyLoader::loadFromFile(niftiPath);
    expect(result.succeeded(), QStringLiteral("直接打开 NIfTI 时应同步加载同目录模型，错误: %1").arg(result.errorMessage));
    expect(result.imageData != nullptr, QStringLiteral("直接打开 NIfTI 时应生成影像数据。"));
    expect(result.package.rootPath == QDir::toNativeSeparators(packagePath),
           QStringLiteral("直接打开 NIfTI 时病例根路径应记录为其所在目录。"));
    expect(result.package.hasNiftiVolume(), QStringLiteral("直接打开 NIfTI 时应记录影像文件路径。"));
    expect(result.package.modelFiles.size() == 1, QStringLiteral("直接打开 NIfTI 时应识别同目录模型文件。"));
    expect(result.models.size() == 1, QStringLiteral("直接打开 NIfTI 时应实际加载一个模型。"));
    expect(result.models.front().polyData != nullptr, QStringLiteral("加载出的模型 polydata 不应为空。"));
    expect(result.models.front().polyData->GetNumberOfCells() > 0,
           QStringLiteral("加载出的模型 polydata 应包含有效几何。"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testMprSliceMathRoundTrip();
    testCasePackageReaderWithModelAndMeta();
    testCasePackageReaderMissingDirectory();
    testCasePackageReaderWithNiftiVolume();
    testCasePackageReaderWithRootLevelModelsAndNiftiVolume();
    testStudyLoaderWithUnicodeNiftiPath();
    testStudyLoaderNormalizesNiftiToInternalVolumeContract();
    testStudyLoaderLoadsSiblingModelsWhenOpeningNiftiFile();

    std::cout << "logic tests passed" << std::endl;
    return 0;
}
