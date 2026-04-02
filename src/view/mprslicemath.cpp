#include "mprslicemath.h"

#include <vtkImageData.h>
#include <vtkMatrix3x3.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
using Axis = MprSliceMath::Axis;
using Orientation = MprSliceMath::Orientation;
using SliceGeometry = MprSliceMath::SliceGeometry;

struct OrientationPreset
{
    Axis xAxis;
    Axis yAxis;
    bool reverseSlider = false;
};

OrientationPreset orientationPreset(Orientation orientation)
{
    switch (orientation) {
    case Orientation::Axial:
        return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, -1.0, 0.0 }, true };
    case Orientation::Coronal:
        return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, 0.0, 1.0 }, false };
    case Orientation::Sagittal:
        return { Axis { 0.0, 1.0, 0.0 }, Axis { 0.0, 0.0, 1.0 }, false };
    }

    return { Axis { 1.0, 0.0, 0.0 }, Axis { 0.0, 1.0, 0.0 }, false };
}

double directionElement(vtkImageData *imageData, int row, int column)
{
    auto *directionMatrix = imageData != nullptr ? imageData->GetDirectionMatrix() : nullptr;
    if (directionMatrix == nullptr) {
        return row == column ? 1.0 : 0.0;
    }

    return directionMatrix->GetElement(row, column);
}

Axis crossProduct(const Axis &lhs, const Axis &rhs)
{
    return Axis {
        lhs[1] * rhs[2] - lhs[2] * rhs[1],
        lhs[2] * rhs[0] - lhs[0] * rhs[2],
        lhs[0] * rhs[1] - lhs[1] * rhs[0]
    };
}

double magnitude(const Axis &axis)
{
    return std::sqrt(MprSliceMath::dotProduct(axis, axis));
}

Axis normalizeAxis(const Axis &axis)
{
    const double length = magnitude(axis);
    if (length <= 0.0) {
        return Axis { 0.0, 0.0, 1.0 };
    }

    return Axis { axis[0] / length, axis[1] / length, axis[2] / length };
}

Axis pointFromIndex(vtkImageData *imageData, double i, double j, double k)
{
    double origin[3] { 0.0, 0.0, 0.0 };
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetOrigin(origin);
    imageData->GetSpacing(spacing);

    const double scaledIndex[3] { i * spacing[0], j * spacing[1], k * spacing[2] };
    Axis point { origin[0], origin[1], origin[2] };
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            point[row] += directionElement(imageData, row, column) * scaledIndex[column];
        }
    }

    return point;
}

Axis imageCenter(vtkImageData *imageData)
{
    int extent[6] { 0, -1, 0, -1, 0, -1 };
    imageData->GetExtent(extent);

    return pointFromIndex(imageData,
                          0.5 * static_cast<double>(extent[0] + extent[1]),
                          0.5 * static_cast<double>(extent[2] + extent[3]),
                          0.5 * static_cast<double>(extent[4] + extent[5]));
}

std::pair<double, double> projectionRange(vtkImageData *imageData, const Axis &center, const Axis &axis)
{
    int extent[6] { 0, -1, 0, -1, 0, -1 };
    imageData->GetExtent(extent);

    double minimum = 0.0;
    double maximum = 0.0;
    bool firstSample = true;

    for (int i : { extent[0], extent[1] }) {
        for (int j : { extent[2], extent[3] }) {
            for (int k : { extent[4], extent[5] }) {
                const Axis point = pointFromIndex(imageData, i, j, k);
                const double projection = MprSliceMath::dotProduct(MprSliceMath::subtractAxes(point, center), axis);
                if (firstSample) {
                    minimum = projection;
                    maximum = projection;
                    firstSample = false;
                } else {
                    minimum = std::min(minimum, projection);
                    maximum = std::max(maximum, projection);
                }
            }
        }
    }

    return { minimum, maximum };
}

double spacingAlongAxis(vtkImageData *imageData, const Axis &axis)
{
    double spacing[3] { 1.0, 1.0, 1.0 };
    imageData->GetSpacing(spacing);

    Axis worldToIndex {
        (directionElement(imageData, 0, 0) * axis[0] + directionElement(imageData, 1, 0) * axis[1] + directionElement(imageData, 2, 0) * axis[2])
            / std::max(spacing[0], 1e-6),
        (directionElement(imageData, 0, 1) * axis[0] + directionElement(imageData, 1, 1) * axis[1] + directionElement(imageData, 2, 1) * axis[2])
            / std::max(spacing[1], 1e-6),
        (directionElement(imageData, 0, 2) * axis[0] + directionElement(imageData, 1, 2) * axis[1] + directionElement(imageData, 2, 2) * axis[2])
            / std::max(spacing[2], 1e-6)
    };

    const double indexStep = magnitude(worldToIndex);
    return indexStep > 0.0 ? 1.0 / indexStep : 1.0;
}

int sampleCount(double minimum, double maximum, double spacing)
{
    if (spacing <= 0.0) {
        return 1;
    }

    return std::max(1, static_cast<int>(std::lround((maximum - minimum) / spacing)) + 1);
}
}

namespace MprSliceMath
{
QString orientationName(Orientation orientation)
{
    switch (orientation) {
    case Orientation::Axial:
        return QStringLiteral("Axial");
    case Orientation::Coronal:
        return QStringLiteral("Coronal");
    case Orientation::Sagittal:
        return QStringLiteral("Sagittal");
    }

    return QStringLiteral("Unknown");
}

Axis addAxes(const Axis &lhs, const Axis &rhs)
{
    return Axis { lhs[0] + rhs[0], lhs[1] + rhs[1], lhs[2] + rhs[2] };
}

Axis subtractAxes(const Axis &lhs, const Axis &rhs)
{
    return Axis { lhs[0] - rhs[0], lhs[1] - rhs[1], lhs[2] - rhs[2] };
}

Axis scaleAxis(const Axis &axis, double scale)
{
    return Axis { axis[0] * scale, axis[1] * scale, axis[2] * scale };
}

double dotProduct(const Axis &lhs, const Axis &rhs)
{
    return lhs[0] * rhs[0] + lhs[1] * rhs[1] + lhs[2] * rhs[2];
}

double maxOutputCoordinate(double origin, double spacing, int minExtent, int maxExtent)
{
    return origin + static_cast<double>(std::max(0, maxExtent - minExtent)) * spacing;
}

SliceGeometry buildSliceGeometry(vtkImageData *imageData, Orientation orientation)
{
    SliceGeometry geometry;
    if (imageData == nullptr) {
        return geometry;
    }

    // 预先算好这一朝向下完整且稳定的重切片平面描述，后续渲染和定位都复用它。
    const auto preset = orientationPreset(orientation);
    geometry.center = imageCenter(imageData);
    geometry.xAxis = normalizeAxis(preset.xAxis);
    geometry.yAxis = normalizeAxis(preset.yAxis);
    geometry.normalAxis = normalizeAxis(crossProduct(geometry.xAxis, geometry.yAxis));
    geometry.reverseSlider = preset.reverseSlider;

    const auto xRange = projectionRange(imageData, geometry.center, geometry.xAxis);
    const auto yRange = projectionRange(imageData, geometry.center, geometry.yAxis);
    const auto sliceRange = projectionRange(imageData, geometry.center, geometry.normalAxis);

    geometry.xSpacing = std::max(1e-3, spacingAlongAxis(imageData, geometry.xAxis));
    geometry.ySpacing = std::max(1e-3, spacingAlongAxis(imageData, geometry.yAxis));
    geometry.sliceSpacing = std::max(1e-3, spacingAlongAxis(imageData, geometry.normalAxis));
    geometry.minSlice = sliceRange.first;
    geometry.sliceCount = sampleCount(sliceRange.first, sliceRange.second, geometry.sliceSpacing);
    geometry.outputOrigin = { xRange.first, yRange.first, 0.0 };
    geometry.outputExtent = {
        0,
        sampleCount(xRange.first, xRange.second, geometry.xSpacing) - 1,
        0,
        sampleCount(yRange.first, yRange.second, geometry.ySpacing) - 1,
        0,
        0
    };

    return geometry;
}

Axis sliceOriginForSliderValue(const SliceGeometry &geometry, int sliderValue)
{
    if (geometry.sliceCount <= 0) {
        return geometry.center;
    }

    const int clampedValue = std::clamp(sliderValue, 0, geometry.sliceCount - 1);
    const int sliceIndex = geometry.reverseSlider
        ? (geometry.sliceCount - 1 - clampedValue)
        : clampedValue;
    const double sliceOffset = geometry.minSlice + sliceIndex * geometry.sliceSpacing;
    return addAxes(geometry.center,
                   scaleAxis(geometry.normalAxis, sliceOffset));
}

int sliderValueForWorldPosition(const SliceGeometry &geometry, const Axis &worldPosition)
{
    if (geometry.sliceCount <= 0 || geometry.sliceSpacing <= 0.0) {
        return 0;
    }

    const double projection = dotProduct(subtractAxes(worldPosition, geometry.center), geometry.normalAxis);
    const int sliceIndex = std::clamp(static_cast<int>(std::lround((projection - geometry.minSlice) / geometry.sliceSpacing)),
                                      0,
                                      std::max(0, geometry.sliceCount - 1));
    return geometry.reverseSlider
        ? (geometry.sliceCount - 1 - sliceIndex)
        : sliceIndex;
}
}
