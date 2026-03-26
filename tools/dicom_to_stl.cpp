#include <vtkCleanPolyData.h>
#include <vtkDICOMImageReader.h>
#include <vtkFlyingEdges3D.h>
#include <vtkImageData.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkSTLWriter.h>
#include <vtkSmoothPolyDataFilter.h>
#include <vtkTriangleFilter.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

int main(int argc, char *argv[])
{
    if (argc < 3) {
        std::cerr << "Usage: dicom_to_stl <dicom_dir> <output_stl> [iso_value]\n";
        return 1;
    }

    const fs::path dicomDir = fs::u8path(argv[1]);
    const fs::path outputFile = fs::u8path(argv[2]);
    const double isoValue = (argc >= 4) ? std::stod(argv[3]) : 300.0;

    if (!fs::exists(dicomDir) || !fs::is_directory(dicomDir)) {
        std::cerr << "DICOM directory not found: " << dicomDir.string() << "\n";
        return 2;
    }

    fs::create_directories(outputFile.parent_path());

    vtkNew<vtkDICOMImageReader> reader;
    reader->SetDirectoryName(dicomDir.string().c_str());
    reader->Update();

    vtkImageData *image = reader->GetOutput();
    if (image == nullptr || image->GetPointData() == nullptr || image->GetPointData()->GetScalars() == nullptr) {
        std::cerr << "Failed to load DICOM series from: " << dicomDir.string() << "\n";
        return 3;
    }

    double scalarRange[2] = {0.0, 0.0};
    image->GetScalarRange(scalarRange);
    std::cout << "Loaded DICOM series from: " << dicomDir.string() << "\n";
    std::cout << "Scalar range: [" << scalarRange[0] << ", " << scalarRange[1] << "]\n";
    std::cout << "Generating isosurface at: " << isoValue << "\n";

    vtkNew<vtkFlyingEdges3D> contour;
    contour->SetInputConnection(reader->GetOutputPort());
    contour->SetValue(0, isoValue);
    contour->ComputeNormalsOn();
    contour->Update();

    vtkNew<vtkTriangleFilter> triangulate;
    triangulate->SetInputConnection(contour->GetOutputPort());
    triangulate->Update();

    vtkNew<vtkCleanPolyData> clean;
    clean->SetInputConnection(triangulate->GetOutputPort());
    clean->Update();

    vtkNew<vtkSmoothPolyDataFilter> smooth;
    smooth->SetInputConnection(clean->GetOutputPort());
    smooth->SetNumberOfIterations(20);
    smooth->SetRelaxationFactor(0.1);
    smooth->FeatureEdgeSmoothingOff();
    smooth->BoundarySmoothingOn();
    smooth->Update();

    vtkPolyData *surface = smooth->GetOutput();
    if (surface == nullptr || surface->GetNumberOfPoints() == 0) {
        std::cerr << "No surface generated. Try a lower iso value.\n";
        return 4;
    }

    vtkNew<vtkSTLWriter> writer;
    writer->SetFileName(outputFile.string().c_str());
    writer->SetInputData(surface);
    writer->SetFileTypeToBinary();
    if (writer->Write() == 0) {
        std::cerr << "Failed to write STL: " << outputFile.string() << "\n";
        return 5;
    }

    std::cout << "STL generated: " << outputFile.string() << "\n";
    std::cout << "Points: " << surface->GetNumberOfPoints() << ", Polys: " << surface->GetNumberOfPolys() << "\n";
    return 0;
}
