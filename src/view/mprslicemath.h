#pragma once

#include <array>

#include <QString>

class vtkImageData;

namespace MprSliceMath
{
// 集中处理不同朝向下的切片几何和世界坐标/索引坐标换算，
// 让视图部件只负责重切片管线和交互编排。
using Axis = std::array<double, 3>;

enum class Orientation
{
    Axial,
    Coronal,
    Sagittal
};

struct SliceGeometry
{
    Axis center { 0.0, 0.0, 0.0 };
    Axis xAxis { 1.0, 0.0, 0.0 };
    Axis yAxis { 0.0, 1.0, 0.0 };
    Axis normalAxis { 0.0, 0.0, 1.0 };
    Axis outputOrigin { 0.0, 0.0, 0.0 };
    std::array<int, 6> outputExtent { 0, -1, 0, -1, 0, 0 };
    double xSpacing = 1.0;
    double ySpacing = 1.0;
    double sliceSpacing = 1.0;
    double minSlice = 0.0;
    int sliceCount = 0;
    bool reverseSlider = false;
};

QString orientationName(Orientation orientation);
Axis addAxes(const Axis &lhs, const Axis &rhs);
Axis subtractAxes(const Axis &lhs, const Axis &rhs);
Axis scaleAxis(const Axis &axis, double scale);
double dotProduct(const Axis &lhs, const Axis &rhs);
double maxOutputCoordinate(double origin, double spacing, int minExtent, int maxExtent);
SliceGeometry buildSliceGeometry(vtkImageData *imageData, Orientation orientation);
Axis sliceOriginForSliderValue(const SliceGeometry &geometry, int sliderValue);
int sliderValueForWorldPosition(const SliceGeometry &geometry, const Axis &worldPosition);
}
