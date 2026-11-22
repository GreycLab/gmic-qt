/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file CroppedActiveLayerProxy.cpp
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

#include "CroppedActiveLayerProxy.h"
#include <QDebug>
#include <mutex>
#include "Common.h"
#include "Host/GmicQtHost.h"
#include "gmic.h"

namespace GmicQt
{

namespace
{

// The cache is static: serialize the accesses. The mutex is recursive because get() and getSize() call update().
std::recursive_mutex & proxyMutex()
{
  static std::recursive_mutex mutex;
  return mutex;
}

} // namespace

double CroppedActiveLayerProxy::_x = -1.0;
double CroppedActiveLayerProxy::_y = -1.0;
double CroppedActiveLayerProxy::_width = -1.0;
double CroppedActiveLayerProxy::_height = -1.0;
std::unique_ptr<gmic_library::gmic_image<gmic_pixel_type>> CroppedActiveLayerProxy::_cachedImage(new gmic_library::gmic_image<gmic_pixel_type>);

void CroppedActiveLayerProxy::get(gmic_library::gmic_image<gmic_pixel_type> & image, double x, double y, double width, double height)
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  if ((x != _x) || (y != _y) || (width != _width) || (height != _height)) {
    update(x, y, width, height);
  }
  image = *_cachedImage;
}

QSize CroppedActiveLayerProxy::getSize(double x, double y, double width, double height)
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  if ((x != _x) || (y != _y) || (width != _width) || (height != _height)) {
    update(x, y, width, height);
  }
  return QSize(_cachedImage->width(), _cachedImage->height());
}

void CroppedActiveLayerProxy::clear()
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  _cachedImage->assign();
  _x = _y = _width = _height = -1.0;
}

void CroppedActiveLayerProxy::update(double x, double y, double width, double height)
{
  std::lock_guard<std::recursive_mutex> lock(proxyMutex());
  // The parameters are recorded only once the image has been successfully retrieved: if the host
  // throws (e.g. std::bad_alloc), the cache must not pretend to hold the image of these parameters.
  clear();
  gmic_library::gmic_list<gmic_pixel_type> images;
  gmic_library::gmic_list<char> imageNames;
  GmicQtHost::getCroppedImages(images, imageNames, x, y, width, height, InputMode::Active);
  if (images.size() > 0) {
    GmicQtHost::applyColorProfile(images.front());
    _cachedImage->swap(images.front());
    _x = x;
    _y = y;
    _width = width;
    _height = height;
  }
  // Otherwise the cache stays cleared (and will be filled again at the next request)
}

} // namespace GmicQt
