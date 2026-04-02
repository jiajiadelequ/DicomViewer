#include <vtkCleanPolyData.h>
#include <vtkDecimatePro.h>
#include <vtkFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkMatrix3x3.h>
#include <vtkNew.h>
#include <vtkOBJWriter.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkPolyDataConnectivityFilter.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkTriangleFilter.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <gdcmException.h>
#include <itkGDCMImageIO.h>
#include <itkGDCMSeriesFileNames.h>
#include <itkImage.h>
#include <itkImageSeriesReader.h>
#include <itkImageToVTKImageFilter.h>

namespace fs = std::filesystem;

namespace
{
using VolumePixelType = short;
constexpr unsigned int VolumeDimension = 3;
using VolumeImageType = itk::Image<VolumePixelType, VolumeDimension>;
using VolumeReaderType = itk::ImageSeriesReader<VolumeImageType>;
using VolumeNamesGeneratorType = itk::GDCMSeriesFileNames;
using VolumeImageIOType = itk::GDCMImageIO;
using VolumeConnectorType = itk::ImageToVTKImageFilter<VolumeImageType>;

void applyItkDirectionToVtkImage(VolumeImageType *itkImage, vtkImageData *vtkImage)
{
    if (itkImage == nullptr || vtkImage == nullptr) {
        return;
    }

    vtkNew<vtkMatrix3x3> directionMatrix;
    const auto &itkDirection = itkImage->GetDirection();
    for (unsigned int row = 0; row < VolumeDimension; ++row) {
        for (unsigned int column = 0; column < VolumeDimension; ++column) {
            directionMatrix->SetElement(static_cast<int>(row),
                                        static_cast<int>(column),
                                        itkDirection(row, column));
        }
    }

    vtkImage->SetDirectionMatrix(directionMatrix);
}

std::vector<std::string> selectLargestSeriesFiles(VolumeNamesGeneratorType *namesGenerator)
{
    std::vector<std::string> largestSeriesFiles;
    if (namesGenerator == nullptr) {
        return largestSeriesFiles;
    }

    const auto &seriesUIDs = namesGenerator->GetSeriesUIDs();
    for (const auto &seriesUID : seriesUIDs) {
        const auto &seriesFiles = namesGenerator->GetFileNames(seriesUID);
        if (seriesFiles.size() > largestSeriesFiles.size()) {
            largestSeriesFiles = seriesFiles;
        }
    }

    return largestSeriesFiles;
}
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: dicom_to_obj <dicom_dir> <output_obj> [iso_value] [target_reduction]\n";
        return 1;
    }

    const fs::path dicomDir = fs::u8path(argv[1]);
    const fs::path outputFile = fs::u8path(argv[2]);
    const double isoValue = (argc >= 4) ? std::stod(argv[3]) : -250.0;
    const double targetReduction = (argc >= 5)
        ? std::clamp(std::stod(argv[4]), 0.0, 0.98)
        : 0.90;

    if (!fs::exists(dicomDir) || !fs::is_directory(dicomDir)) {
        std::cerr << "DICOM directory not found: " << dicomDir.string() << "\n";
        return 2;
    }

    try {
        auto namesGenerator = VolumeNamesGeneratorType::New();
        namesGenerator->SetUseSeriesDetails(true);
        namesGenerator->SetDirectory(dicomDir.string());

        const auto fileNames = selectLargestSeriesFiles(namesGenerator);
        if (fileNames.empty()) {
            std::cerr << "No readable DICOM series found in: " << dicomDir.string() << "\n";
            return 3;
        }

        auto imageIO = VolumeImageIOType::New();
        auto reader = VolumeReaderType::New();
        reader->SetImageIO(imageIO);
        reader->SetFileNames(fileNames);
        reader->ForceOrthogonalDirectionOff();
        reader->Update();

        auto connector = VolumeConnectorType::New();
        connector->SetInput(reader->GetOutput());
        connector->Update();

        auto *image = connector->GetOutput();
        if (image == nullptr || image->GetPointData() == nullptr || image->GetPointData()->GetScalars() == nullptr) {
            std::cerr << "Failed to convert DICOM series to VTK image: " << dicomDir.string() << "\n";
            return 3;
        }

        applyItkDirectionToVtkImage(reader->GetOutput(), image);

        fs::create_directories(outputFile.parent_path());

        double scalarRange[2] = { 0.0, 0.0 };
        image->GetScalarRange(scalarRange);
        std::cout << "Loaded DICOM series from: " << dicomDir.string() << "\n";
        std::cout << "Slice count: " << fileNames.size() << "\n";
        std::cout << "Scalar range: [" << scalarRange[0] << ", " << scalarRange[1] << "]\n";
        std::cout << "Generating body surface at iso: " << isoValue << "\n";
        std::cout << "Target reduction: " << targetReduction << "\n";

        vtkNew<vtkFlyingEdges3D> contour;
        contour->SetInputData(image);
        contour->SetValue(0, isoValue);
        contour->ComputeNormalsOn();
        contour->ComputeGradientsOff();
        contour->Update();

        vtkNew<vtkTriangleFilter> triangulate;
        triangulate->SetInputConnection(contour->GetOutputPort());
        triangulate->Update();

        vtkNew<vtkCleanPolyData> cleanInitial;
        cleanInitial->SetInputConnection(triangulate->GetOutputPort());
        cleanInitial->Update();

        vtkNew<vtkPolyDataConnectivityFilter> largestRegion;
        largestRegion->SetInputConnection(cleanInitial->GetOutputPort());
        largestRegion->SetExtractionModeToLargestRegion();
        largestRegion->Update();

        vtkNew<vtkSmoothPolyDataFilter> smooth;
        smooth->SetInputConnection(largestRegion->GetOutputPort());
        smooth->SetNumberOfIterations(15);
        smooth->SetRelaxationFactor(0.08);
        smooth->FeatureEdgeSmoothingOff();
        smooth->BoundarySmoothingOn();
        smooth->Update();

        vtkNew<vtkDecimatePro> decimate;
        decimate->SetInputConnection(smooth->GetOutputPort());
        decimate->SetTargetReduction(targetReduction);
        decimate->PreserveTopologyOn();
        decimate->SplittingOff();
        decimate->BoundaryVertexDeletionOff();
        decimate->Update();

        vtkNew<vtkCleanPolyData> cleanFinal;
        cleanFinal->SetInputConnection(decimate->GetOutputPort());
        cleanFinal->Update();

        vtkPolyData *surface = cleanFinal->GetOutput();
        if (surface == nullptr || surface->GetNumberOfPoints() == 0) {
            std::cerr << "No surface generated. Try a lower iso value.\n";
            return 4;
        }

        vtkNew<vtkOBJWriter> writer;
        writer->SetFileName(outputFile.string().c_str());
        writer->SetInputData(surface);
        if (writer->Write() == 0) {
            std::cerr << "Failed to write OBJ: " << outputFile.string() << "\n";
            return 5;
        }

        std::cout << "OBJ generated: " << outputFile.string() << "\n";
        std::cout << "Points: " << surface->GetNumberOfPoints()
                  << ", Polys: " << surface->GetNumberOfPolys() << "\n";
        return 0;
    } catch (const itk::ExceptionObject &ex) {
        std::cerr << "ITK read failed: " << ex << "\n";
        return 6;
    } catch (const gdcm::Exception &ex) {
        std::cerr << "GDCM read failed: " << ex.what() << "\n";
        return 7;
    } catch (const std::exception &ex) {
        std::cerr << "Unexpected error: " << ex.what() << "\n";
        return 8;
    }
}
