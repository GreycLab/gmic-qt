/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file KeypointList.cpp
 *
 *  Copyright 2017 Sebastien Fourey
 *
 *  This file is part of G'MIC-Qt, a generic plug-in for raster graphics
 *  editors, offering hundreds of filters thanks to the underlying G'MIC
 *  image processing framework.
 *
 *  gmic_qt is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  gmic_qt is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with gmic_qt.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#include "KeypointList.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace
{

// The bit pattern is tested, rather than calling std::isnan(), so that the result
// does not depend on floating point optimization flags such as -ffast-math.
inline bool isNaNFloat(const float value)
{
  static_assert(std::numeric_limits<float>::is_iec559 && (sizeof(float) == sizeof(std::uint32_t)), "KeypointList requires 32-bit IEEE 754 floats");
  std::uint32_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return (bits & 0x7fffffffu) > 0x7f800000u;
}

} // namespace

namespace GmicQt
{

const float KeypointList::Keypoint::DefaultRadius = 9.0f;

KeypointList::KeypointList() = default;

void KeypointList::add(const KeypointList::Keypoint & keypoint)
{
  _keypoints.push_back(keypoint);
}

bool KeypointList::isEmpty() const
{
  return _keypoints.empty();
}

void KeypointList::clear()
{
  _keypoints.clear();
}

bool KeypointList::isValidIndex(int n) const
{
  return (n >= 0) && (static_cast<size_type>(n) < _keypoints.size());
}

QPointF KeypointList::position(int n) const
{
  Q_ASSERT_X(isValidIndex(n), Q_FUNC_INFO, "Invalid keypoint index");
  if (!isValidIndex(n)) {
    // Same convention as for a removed keypoint
    return {std::numeric_limits<qreal>::quiet_NaN(), std::numeric_limits<qreal>::quiet_NaN()};
  }
  const Keypoint & kp = _keypoints[static_cast<size_type>(n)];
  return {static_cast<qreal>(kp.x), static_cast<qreal>(kp.y)};
}

QColor KeypointList::color(int n) const
{
  Q_ASSERT_X(isValidIndex(n), Q_FUNC_INFO, "Invalid keypoint index");
  if (!isValidIndex(n)) {
    return QColor(); // Invalid color
  }
  return _keypoints[static_cast<size_type>(n)].color;
}

bool KeypointList::isRemovable(int n) const
{
  Q_ASSERT_X(isValidIndex(n), Q_FUNC_INFO, "Invalid keypoint index");
  if (!isValidIndex(n)) {
    return false;
  }
  return _keypoints[static_cast<size_type>(n)].removable;
}

KeypointList::Keypoint::Keypoint(float x, float y, const QColor & color, bool removable, float radius, bool keepOpacityWhenSelected)
    : x(x), y(y), color(color), removable(removable), radius(radius), keepOpacityWhenSelected(keepOpacityWhenSelected)
{
}

KeypointList::Keypoint::Keypoint(const QPointF & point, const QColor & color, bool removable, float radius, bool keepOpacityWhenSelected)
    : x(static_cast<float>(point.x())), y(static_cast<float>(point.y())), color(color), removable(removable), radius(radius),
      keepOpacityWhenSelected(keepOpacityWhenSelected)
{
}

KeypointList::Keypoint::Keypoint(const QColor & color, bool removable, float radius, bool keepOpacityWhenSelected)
    : x(std::numeric_limits<float>::quiet_NaN()), y(std::numeric_limits<float>::quiet_NaN()), color(color), removable(removable), radius(radius),
      keepOpacityWhenSelected(keepOpacityWhenSelected)
{
}

bool KeypointList::Keypoint::isNaN() const
{
  return isNaNFloat(x) || isNaNFloat(y);
}

KeypointList::Keypoint & KeypointList::Keypoint::setNaN()
{
  x = y = std::numeric_limits<float>::quiet_NaN();
  return *this;
}

} // namespace GmicQt
