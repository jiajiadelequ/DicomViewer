#include "src/core/casepackagereader.h"
#include "src/view/mprslicemath.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QTemporaryDir>

#include <vtkImageData.h>
#include <vtkSmartPointer.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace
{
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
    const StudyPackage package = reader.readFromDirectory(tempDir.path(), &errorMessage);

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
    const StudyPackage package = reader.readFromDirectory(QStringLiteral("Z:/this/path/should/not/exist"), &errorMessage);

    expect(!package.isValid(), QStringLiteral("不存在的目录不应返回有效病例包。"));
    expect(!errorMessage.isEmpty(), QStringLiteral("不存在的目录应返回错误信息。"));
}
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testMprSliceMathRoundTrip();
    testCasePackageReaderWithModelAndMeta();
    testCasePackageReaderMissingDirectory();

    std::cout << "logic tests passed" << std::endl;
    return 0;
}
