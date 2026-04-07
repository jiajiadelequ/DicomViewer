#pragma once

#include "src/model/studypackage.h"

#include <QString>
#include <vector>
#include <vtkImageData.h>
#include <vtkPolyData.h>
#include <vtkSmartPointer.h>

struct WindowLevelPresetData
{
    double window = 0.0;
    double level = 0.0;
    QString explanation;
    bool isValid = false;
};

struct LoadedModelData
{
    struct MaterialData
    {
        double color[3] = { 0.85, 0.85, 0.85 };
        double opacity = 1.0;
        double specularColor[3] = { 1.0, 1.0, 1.0 };
        double specularStrength = 0.0;
        double specularPower = 1.0;
        bool hasMaterial = false;
    };

    QString filePath;
    vtkSmartPointer<vtkPolyData> polyData;
    MaterialData material;
};

struct StudyLoadResult
{
    StudyPackage package;
    vtkSmartPointer<vtkImageData> imageData;
    WindowLevelPresetData windowLevelPreset;
    std::vector<LoadedModelData> models;
    QString errorMessage;
    bool cancelled = false;

    [[nodiscard]] bool succeeded() const
    {
        return !cancelled && errorMessage.isEmpty() && package.isValid();
    }
};
