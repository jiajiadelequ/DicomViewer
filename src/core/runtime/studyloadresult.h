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
    QString filePath;
    vtkSmartPointer<vtkPolyData> polyData;
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
