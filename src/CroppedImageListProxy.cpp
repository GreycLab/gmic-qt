/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file CroppedImageListProxy.cpp
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

#include "CroppedImageListProxy.h"
#include <QDebug>
#include <algorithm>
#include <cmath>
#include <mutex>
#include "Common.h"
#include "Host/GmicQtHost.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{

// The caches are static: serialize the accesses, so that the lists cannot be modified while they are being copied.
// The mutex is recursive because get() calls update().
std::recursive_mutex & proxyMutex()
{
  static std::recursive_mutex mutex;
  return mutex;
}

// Size of a scaled dimension. It is at least 1 pixel: for CImg::resize(), a null size would
// empty the image and a negative one means a percentage.
int scaledSize(int size, double zoom)
{
  return static_cast<int>(std::max(1.0, std::min(std::round(size * zoom), static_cast<double>(size))));
}

} // namespace

double CroppedImageListProxy::_x = -1.0;
double CroppedImageListProxy::_y = -1.0;
double CroppedImageListProxy::_width = -1.0;
double CroppedImageListProxy::_height = -1.0;
double CroppedImageListProxy::_zoom = 0.0;
InputMode CroppedImageListProxy::_inputMode = InputMode::Unspecified;
std::unique_ptr<gmic_library::gmic_list<gmic_pixel_type>> CroppedImageListProxy::_cachedImageList(new gmic_library::gmic_list<gmic_pixel_type>);
std::unique_ptr<gmic_library::gmic_list<char>> CroppedImageListProxy::_cachedImageNames(new gmic_library::gmic_list<char>);

void CroppedImageListProxy::get(gmic_library::gmic_list<gmic_pixel_type> & images, gmic_library::gmic_list<char> & imageNames,
                                double x, double y, double width, double height, InputMode mode, double zoom)
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  if ((x != _x) || (y != _y) || (width != _width) || (height != _height) || (mode != _inputMode) || (zoom != _zoom)) {
    update(x, y, width, height, mode, zoom);
  }
  images = *_cachedImageList;
  imageNames = *_cachedImageNames;
}

void CroppedImageListProxy::update(double x, double y, double width, double height, InputMode mode, double zoom)
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  // The parameters are recorded only once the images have been successfully retrieved: if the host
  // throws (e.g. std::bad_alloc), the cache must not pretend to hold the images of these parameters.
  // Clearing first also guarantees that the host never appends to some outdated list.
  clear();
  try {
    GmicQtHost::getCroppedImages(*_cachedImageList, *_cachedImageNames, x, y, width, height, mode);
    if (std::isfinite(zoom) && (zoom > 0.0) && (zoom < 1.0)) {
      for (unsigned int i = 0; i < _cachedImageList->size(); ++i) {
        gmic_image<float> & image = (*_cachedImageList)[i];
        if (!image.is_empty()) {
          image.resize(scaledSize(image.width(), zoom), scaledSize(image.height(), zoom), 1, -100, 1);
        }
      }
    }
  } catch (...) {
    _cachedImageList->assign();
    _cachedImageNames->assign();
    throw;
  }
  _x = x;
  _y = y;
  _width = width;
  _height = height;
  _inputMode = mode;
  _zoom = zoom;
}

void CroppedImageListProxy::clear()
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  _cachedImageList->assign();
  _cachedImageNames->assign();
  _x = _y = _width = _height = -1.0;
  _inputMode = InputMode::Unspecified;
  _zoom = 0.0;
}

} // namespace GmicQt
