/** -*- mode: c++ ; c-basic-offset: 2 -*-
 *
 *  @file host_gimp.cpp
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
#include <libgimp/gimp.h>
#include <QDebug>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <list>
#include <stack>
#include <vector>
#ifdef _IS_WINDOWS_
#include <QTextCodec>
#endif
#include "Common.h"
#include "GmicQt.h"
#include "Host/GmicQtHost.h"
#include "ImageTools.h"
#include "gmic.h"

#if GIMP_CHECK_VERSION(3, 0, 0)
#define _gimp_image_get_width gimp_image_get_width
#define _gimp_image_get_height gimp_image_get_height
#define _gimp_image_get_base_type gimp_image_get_base_type
#define _gimp_drawable_get_width gimp_drawable_get_width
#define _gimp_drawable_get_height gimp_drawable_get_height
#define _gimp_drawable_get_offsets gimp_drawable_get_offsets
#else
#define _gimp_image_get_width gimp_image_width
#define _gimp_image_get_height gimp_image_height
#define _gimp_image_get_base_type gimp_image_base_type
#define _gimp_drawable_get_width gimp_drawable_width
#define _gimp_drawable_get_height gimp_drawable_height
#define _gimp_drawable_get_offsets gimp_drawable_offsets
#endif

#if GIMP_CHECK_VERSION(3, 0, 0)
typedef GimpImage * _GimpImagePtr;
typedef GimpLayer * _GimpLayerPtr;
typedef GimpItem * _GimpItemPtr;
#define _GIMP_ITEM(item) GIMP_ITEM(item)
#define _GIMP_DRAWABLE(drawable) GIMP_DRAWABLE(drawable)
#define _GIMP_LAYER(layer) GIMP_LAYER(layer)
#define _GIMP_NULL_LAYER NULL
#define _GIMP_LAYER_IS_NOT_NULL(layer) ((layer) != NULL)
#define _gimp_top_layer gimp_layer_get_by_id(0)
#define PLUG_IN_PROC "plug-in-gmic-qt"

typedef struct _GmicQtPlugin GmicQtPlugin;
typedef struct _GmicQtPluginClass GmicQtPluginClass;

struct _GmicQtPlugin {
  GimpPlugIn parent_instance;
};

struct _GmicQtPluginClass {
  GimpPlugInClass parent_class;
};

#define GMIC_QT_TYPE (gmic_qt_get_type())
// The object is called GmicQtPlugin to avoid name conflict with the namespace.
#define GMIC_QT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), GMIC_QT_TYPE, GmicQtPlugin))

GType gmic_qt_get_type(void) G_GNUC_CONST;

static GList * gmic_qt_query(GimpPlugIn * plug_in);
static GimpProcedure * gmic_qt_create_procedure(GimpPlugIn * plug_in, const gchar * name);

static GimpValueArray * gmic_qt_run(GimpProcedure * procedure, GimpRunMode run_mode, GimpImage * image, GimpDrawable ** drawables, GimpProcedureConfig * config, gpointer run_data);

G_DEFINE_TYPE(GmicQtPlugin, gmic_qt, GIMP_TYPE_PLUG_IN)

GIMP_MAIN(GMIC_QT_TYPE)

static void gmic_qt_class_init(GmicQtPluginClass * klass)
{
  GimpPlugInClass * plug_in_class = GIMP_PLUG_IN_CLASS(klass);

  plug_in_class->query_procedures = gmic_qt_query;
  plug_in_class->create_procedure = gmic_qt_create_procedure;
}

static void gmic_qt_init(GmicQtPlugin * gmic_qt) { unused(gmic_qt); }

#else
#define _GimpImagePtr int
#define _GimpLayerPtr int
#define _GimpItemPtr int
#define _GIMP_ITEM(item) (item)
#define _GIMP_DRAWABLE(drawable) (drawable)
#define _GIMP_LAYER(layer) (layer)
#define _GIMP_NULL_LAYER -1
#define _GIMP_LAYER_IS_NOT_NULL(layer) ((layer) >= 0)
#define _gimp_top_layer 0
#endif

namespace GmicQtHost
{
const QString ApplicationName = QString("GIMP %1.%2").arg(GIMP_MAJOR_VERSION).arg(GIMP_MINOR_VERSION);
const char * const ApplicationShortname = GMIC_QT_XSTRINGIFY(GMIC_HOST);
const bool DarkThemeIsDefault = true;

} // namespace GmicQtHost

namespace
{
_GimpImagePtr gmic_qt_gimp_image_id;

gmic_library::gmic_image<int> inputLayerDimensions;
std::vector<_GimpLayerPtr> inputLayers;

#if !defined(GIMP_NORMAL_MODE)
typedef GimpLayerMode GimpLayerModeEffects;
#define GIMP_NORMAL_MODE GIMP_LAYER_MODE_NORMAL
const QMap<QString, GimpLayerModeEffects> BlendingModesMap = {{QString("alpha"), GIMP_LAYER_MODE_NORMAL},
                                                              {QString("normal"), GIMP_LAYER_MODE_NORMAL},
                                                              {QString("dissolve"), GIMP_LAYER_MODE_DISSOLVE},
                                                              {QString("behind"), GIMP_LAYER_MODE_BEHIND},
                                                              {QString("colorerase"), GIMP_LAYER_MODE_COLOR_ERASE},
                                                              {QString("erase"), GIMP_LAYER_MODE_ERASE},
                                                              {QString("merge"), GIMP_LAYER_MODE_MERGE},
                                                              {QString("split"), GIMP_LAYER_MODE_SPLIT},
                                                              {QString("lighten"), GIMP_LAYER_MODE_LIGHTEN_ONLY},
                                                              {QString("lumalighten"), GIMP_LAYER_MODE_LUMA_LIGHTEN_ONLY},
                                                              {QString("screen"), GIMP_LAYER_MODE_SCREEN},
                                                              {QString("dodge"), GIMP_LAYER_MODE_DODGE},
                                                              {QString("addition"), GIMP_LAYER_MODE_ADDITION},
                                                              {QString("darken"), GIMP_LAYER_MODE_DARKEN_ONLY},
                                                              {QString("lumadarken"), GIMP_LAYER_MODE_LUMA_DARKEN_ONLY},
                                                              {QString("multiply"), GIMP_LAYER_MODE_MULTIPLY},
                                                              {QString("burn"), GIMP_LAYER_MODE_BURN},
                                                              {QString("overlay"), GIMP_LAYER_MODE_OVERLAY},
                                                              {QString("softlight"), GIMP_LAYER_MODE_SOFTLIGHT},
                                                              {QString("hardlight"), GIMP_LAYER_MODE_HARDLIGHT},
                                                              {QString("vividlight"), GIMP_LAYER_MODE_VIVID_LIGHT},
                                                              {QString("pinlight"), GIMP_LAYER_MODE_PIN_LIGHT},
                                                              {QString("linearlight"), GIMP_LAYER_MODE_LINEAR_LIGHT},
                                                              {QString("hardmix"), GIMP_LAYER_MODE_HARD_MIX},
                                                              {QString("difference"), GIMP_LAYER_MODE_DIFFERENCE},
                                                              {QString("subtract"), GIMP_LAYER_MODE_SUBTRACT},
                                                              {QString("grainextract"), GIMP_LAYER_MODE_GRAIN_EXTRACT},
                                                              {QString("grainmerge"), GIMP_LAYER_MODE_GRAIN_MERGE},
                                                              {QString("divide"), GIMP_LAYER_MODE_DIVIDE},
                                                              {QString("hue"), GIMP_LAYER_MODE_HSV_HUE},
                                                              {QString("saturation"), GIMP_LAYER_MODE_HSV_SATURATION},
                                                              {QString("color"), GIMP_LAYER_MODE_HSL_COLOR},
                                                              {QString("value"), GIMP_LAYER_MODE_HSV_VALUE},
                                                              {QString("lchhue"), GIMP_LAYER_MODE_LCH_HUE},
                                                              {QString("lchchroma"), GIMP_LAYER_MODE_LCH_CHROMA},
                                                              {QString("lchcolor"), GIMP_LAYER_MODE_LCH_COLOR},
                                                              {QString("lchlightness"), GIMP_LAYER_MODE_LCH_LIGHTNESS},
                                                              {QString("luminance"), GIMP_LAYER_MODE_LUMINANCE},
                                                              {QString("exclusion"), GIMP_LAYER_MODE_EXCLUSION}};
#else
const QMap<QString, GimpLayerModeEffects> BlendingModesMap = {{QString("alpha"), GIMP_NORMAL_MODE},
                                                              {QString("normal"), GIMP_NORMAL_MODE},
                                                              {QString("dissolve"), GIMP_DISSOLVE_MODE},
                                                              {QString("lighten"), GIMP_LIGHTEN_ONLY_MODE},
                                                              {QString("screen"), GIMP_SCREEN_MODE},
                                                              {QString("dodge"), GIMP_DODGE_MODE},
                                                              {QString("addition"), GIMP_ADDITION_MODE},
                                                              {QString("darken"), GIMP_DARKEN_ONLY_MODE},
                                                              {QString("multiply"), GIMP_MULTIPLY_MODE},
                                                              {QString("burn"), GIMP_BURN_MODE},
                                                              {QString("overlay"), GIMP_OVERLAY_MODE},
                                                              {QString("softlight"), GIMP_SOFTLIGHT_MODE},
                                                              {QString("hardlight"), GIMP_HARDLIGHT_MODE},
                                                              {QString("difference"), GIMP_DIFFERENCE_MODE},
                                                              {QString("subtract"), GIMP_SUBTRACT_MODE},
                                                              {QString("grainextract"), GIMP_GRAIN_EXTRACT_MODE},
                                                              {QString("grainmerge"), GIMP_GRAIN_MERGE_MODE},
                                                              {QString("divide"), GIMP_DIVIDE_MODE},
                                                              {QString("hue"), GIMP_HUE_MODE},
                                                              {QString("saturation"), GIMP_SATURATION_MODE},
                                                              {QString("color"), GIMP_COLOR_MODE},
                                                              {QString("value"), GIMP_VALUE_MODE}};
#endif

_GimpLayerPtr getActiveLayer(_GimpImagePtr imageId)
{
#if GIMP_CHECK_VERSION(3, 0, 0)
  GList * selected_layers = gimp_image_list_selected_layers(imageId);
  _GimpLayerPtr active_layer = selected_layers?_GIMP_LAYER(selected_layers->data):_GIMP_NULL_LAYER;
  g_list_free(selected_layers);
  return active_layer;
#else
  return gimp_image_get_active_layer(imageId);
#endif
}

// RAII helper: guarantees that an undo group is closed, even if an exception is thrown.
class GimpUndoGroupGuard {
public:
  explicit GimpUndoGroupGuard(_GimpImagePtr image) : _image(image) { gimp_image_undo_group_start(_image); }
  ~GimpUndoGroupGuard() { gimp_image_undo_group_end(_image); }
  GimpUndoGroupGuard(const GimpUndoGroupGuard &) = delete;
  GimpUndoGroupGuard & operator=(const GimpUndoGroupGuard &) = delete;

private:
  _GimpImagePtr _image;
};

// Refresh the whole area of a layer, using the dimensions of the *layer*
// (not those of a CImg image, which may have been permuted).
void updateWholeDrawable(_GimpLayerPtr layer)
{
  auto drawable = _GIMP_DRAWABLE(layer);
  gimp_drawable_update(drawable, 0, 0, _gimp_drawable_get_width(drawable), _gimp_drawable_get_height(drawable));
}

// GEGL gives/expects interleaved float pixels in [0,1]; G'MIC uses planar pixels in [0,255].
//
// Two strategies are available for the conversion:
//
//  - "in place": the GEGL pixels are read into (or written from) the G'MIC image buffer itself,
//    and the interleaved <-> planar re-layout is done by CImg<T>::permute_axes() (reduced-buffer
//    implementation, which only needs a small temporary buffer). Only ONE image-sized buffer is
//    ever alive: the memory peak is N floats instead of 2N.
//
//  - "out of place": the conversion (scale + re-layout) is done in ONE multi-threaded pass between
//    two image-sized buffers (2N floats at peak). It is faster for small images, for which the
//    memory is not an issue, hence it is used below InPlaceMinFloats and for shared images.
//
// Single-channel images need no re-layout at all (planar == interleaved): they are always
// converted in place.
//
// InPlaceMinFloats (number of float values of the image) is the size from which the in-place
// permutation is preferred. It is a tuning knob: adjust it according to benchmarks.
constexpr long long InPlaceMinFloats = 8LL * 1024 * 1024; // 32 MiB

// Out of place: src is interleaved (c fastest), dst is planar, both with pixelsPerPlane * spectrum floats.
void interleavedToPlanar255(const float * src, float * dst, long long pixelsPerPlane, int spectrum)
{
  cimg_pragma_openmp(parallel for cimg_openmp_if(pixelsPerPlane >= 32768))
  for (long long i = 0; i < pixelsPerPlane; ++i) {
    const float * p = src + i * spectrum;
    for (int c = 0; c < spectrum; ++c) {
      dst[c * pixelsPerPlane + i] = p[c] * 255.0f;
    }
  }
}

// Out of place, inverse operation: planar [0,255] -> interleaved [0,1].
void planar255ToInterleaved(const float * src, float * dst, long long pixelsPerPlane, int spectrum)
{
  cimg_pragma_openmp(parallel for cimg_openmp_if(pixelsPerPlane >= 32768))
  for (long long i = 0; i < pixelsPerPlane; ++i) {
    float * p = dst + i * spectrum;
    for (int c = 0; c < spectrum; ++c) {
      p[c] = src[c * pixelsPerPlane + i] / 255.0f;
    }
  }
}

// Reads a region of a GEGL buffer into a G'MIC image (planar, [0,255]) of size
// (rect->width, rect->height, 1, spectrum). The previous content of result is discarded.
void readBufferToPlanar255(GeglBuffer * buffer, const GeglRectangle * rect, const char * format, int spectrum, gmic_library::gmic_image<float> & result)
{
  const int width = rect->width;
  const int height = rect->height;
  const long long floatsCount = (long long)width * height * spectrum;
  if (spectrum == 1) {
    // Planar == interleaved: read directly in the final buffer.
    result.assign(width, height, 1, 1); // Not initialized
    gegl_buffer_get(buffer, rect, 1, babl_format(format), result.data(), 0, GEGL_ABYSS_NONE);
    result *= 255.0f;
  } else if (floatsCount >= InPlaceMinFloats) {
    // In place: the interleaved pixels are read in the final buffer, seen as a (spectrum,width,height) image,
    // which is then re-laid out as (width,height,1,spectrum) by the reduced-buffer permute_axes().
    result.assign(spectrum, width, height, 1); // Not initialized
    gegl_buffer_get(buffer, rect, 1, babl_format(format), result.data(), 0, GEGL_ABYSS_NONE);
    result.permute_axes("yzcx");
    result *= 255.0f;
  } else {
    // Out of place (small image).
    gmic_library::gmic_image<float> interleaved(spectrum, width, height); // Not initialized
    gegl_buffer_get(buffer, rect, 1, babl_format(format), interleaved.data(), 0, GEGL_ABYSS_NONE);
    result.assign(width, height, 1, spectrum); // Not initialized
    interleavedToPlanar255(interleaved.data(), result.data(), (long long)width * height, spectrum);
  }
}

// Writes a G'MIC image (planar, [0,255]) into a GEGL buffer.
// The image is CONSUMED: it is modified in place (and its dimensions may change) when the in-place
// strategy is used, and it is released before returning in all cases.
void writePlanar255ToBuffer(GeglBuffer * buffer, const GeglRectangle * rect, gmic_library::gmic_image<gmic_pixel_type> & img)
{
  const int spectrum = img.spectrum();
  const char * const format = spectrum == 1 ? "Y' float" : spectrum == 2 ? "Y'A float" : spectrum == 3 ? "R'G'B' float" : "R'G'B'A float";
  const long long pixelsPerPlane = (long long)img.width() * img.height() * img.depth();
  // A shared image must not be modified: its buffer belongs to someone else.
  const bool inPlace = !img.is_shared() && (spectrum == 1 || pixelsPerPlane * spectrum >= InPlaceMinFloats);
  if (inPlace) {
    if (spectrum > 1) {
      img.permute_axes("cxyz"); // (x,y,z,c) -> (c,x,y,z), reduced-buffer implementation
    }
    img /= 255.0f;
    gegl_buffer_set(buffer, rect, 0, babl_format(format), img.data(), 0);
    img.assign();
  } else {
    gmic_library::gmic_image<float> interleaved(spectrum, img.width(), img.height(), img.depth()); // Not initialized
    planar255ToInterleaved(img.data(), interleaved.data(), pixelsPerPlane, spectrum);
    img.assign();
    gegl_buffer_set(buffer, rect, 0, babl_format(format), interleaved.data(), 0);
  }
}

QMap<GimpLayerModeEffects, QString> reverseBlendingModeMap(const QMap<QString, GimpLayerModeEffects> & string2mode)
{
  QMap<GimpLayerModeEffects, QString> result;
  QMap<QString, GimpLayerModeEffects>::const_iterator it = string2mode.cbegin();
  while (it != string2mode.cend()) {
    result[it.value()] = it.key();
    ++it;
  }
  result[GIMP_NORMAL_MODE] = QString("alpha");
  return result;
}

QString blendingMode2String(const GimpLayerModeEffects & blendingMode)
{
  static QMap<GimpLayerModeEffects, QString> mode2string = reverseBlendingModeMap(BlendingModesMap);
  QMap<GimpLayerModeEffects, QString>::const_iterator it = mode2string.find(blendingMode);
  if (it != mode2string.cend()) {
    return it.value();
  }
  // Unknown mode (e.g. legacy modes): use a name that is NOT in BlendingModesMap, so that
  // the layer keeps its original mode instead of being silently reset to "normal".
  return QString("unknown");
}

#ifdef _IS_WINDOWS_
QByteArray mapToASCII(const char * str)
{
  static const QTextCodec * codec = QTextCodec::codecForName("ASCII");
  if (!codec) {
    return QByteArray(str);
  }
  return codec->fromUnicode(QString::fromUtf8(str));
}
inline void _GIMP_ITEM_SET_NAME(_GimpItemPtr item_ID, const gchar * name)
{
  gimp_item_set_name(item_ID, mapToASCII(name).constData());
}
#else
inline void _GIMP_ITEM_SET_NAME(_GimpItemPtr item_ID, const gchar * name)
{
  gimp_item_set_name(item_ID, name);
}
#endif

// gimp_item_get_name() returns a newly allocated string which must be freed with g_free().
// This helper copies it and frees it (an empty string is returned if there is no name).
QByteArray getItemName(_GimpItemPtr item)
{
  gchar * name = gimp_item_get_name(item);
  QByteArray result(name ? name : "");
  g_free(name);
  return result;
}

// Get layer blending mode from string.
//-------------------------------------
void get_output_layer_props(const char * const s, GimpLayerModeEffects & blendmode, double & opacity, int & posx, int & posy, gmic_library::gmic_image<char> & name)
{
  if (!s || !*s)
    return;
  QString str(s);

  // Read output blending mode.
  static const QRegularExpression modeRe(R"_(mode\(\s*([^)]*)\s*\))_");
  QRegularExpressionMatch match = modeRe.match(str);
  if (match.hasMatch()) {
    QString modeStr = match.captured(1).trimmed();
    const auto modeIt = BlendingModesMap.constFind(modeStr);
    if (modeIt != BlendingModesMap.constEnd()) {
      blendmode = modeIt.value();
    }
  }

  // Read output opacity.
  static const QRegularExpression opacityRe(R"_(opacity\(\s*([^)]*)\s*\))_");
  match = opacityRe.match(str);
  if (match.hasMatch()) {
    QString opacityStr = match.captured(1).trimmed();
    bool ok = false;
    double x = opacityStr.toDouble(&ok);
    if (ok) {
      opacity = x;
      if (opacity < 0) {
        opacity = 0;
      } else if (opacity > 100) {
        opacity = 100;
      }
    }
  }

  // Read output positions.
  static const QRegularExpression posRe(R"_(pos\(\s*(-?\d+)\s*[,;\s]\s*(-?\d+)\s*\))_");
  match = posRe.match(str);
  if (match.hasMatch()) {
    QString xStr = match.captured(1);
    QString yStr = match.captured(2);
    bool okX = false;
    bool okY = false;
    int x = xStr.toInt(&okX);
    int y = yStr.toInt(&okY);
    if (okX && okY) {
      posx = x;
      posy = y;
    }
  }

  // Read output name.
  const char * S = std::strstr(s, "name(");
  if (S) {
    const char * ps = S + 5;
    unsigned int level = 1;
    while (*ps && level) {
      if (*ps == '(') {
        ++level;
      } else if (*ps == ')') {
        --level;
      }
      ++ps;
    }
    if (!level || *(ps - 1) == ')') {
      name.assign(S + 5, (unsigned int)(ps - S - 5)).back() = 0;
      cimg_for(name, pn, char)
      {
        if (*pn == 21) {
          *pn = '(';
        } else if (*pn == 22) {
          *pn = ')';
        }
      }
    }
  }
}

// Returns all the non-group layers of an image, from top to bottom (groups are traversed recursively).
std::vector<_GimpLayerPtr> getFlatLayersList(_GimpImagePtr imageId)
{
  std::vector<_GimpLayerPtr> result;
  std::stack<_GimpLayerPtr> idStack;

#if GIMP_CHECK_VERSION(3, 0, 0)
  _GimpLayerPtr * layers = gimp_image_get_layers(imageId);
  int layersCount = layers ? gimp_core_object_array_get_length((GObject **)layers) : 0;
#else
  int layersCount = 0;
  _GimpLayerPtr * layers = gimp_image_get_layers(imageId, &layersCount);
#endif
  for (int i = layersCount - 1; i >= 0; --i) {
    idStack.push(layers[i]);
  }
  g_free(layers);

  while (!idStack.empty()) {
    const _GimpLayerPtr top = idStack.top();
    idStack.pop();
    if (gimp_item_is_group(_GIMP_ITEM(top))) {
      int childCount = 0;
#if GIMP_CHECK_VERSION(3, 0, 0)
      _GimpItemPtr * children = gimp_item_get_children(_GIMP_ITEM(top));
      if (children) {
        for (int i = 0; _GIMP_LAYER(children[i]) != NULL; ++i) {
          ++childCount;
        }
      }
#else
      _GimpItemPtr * children = gimp_item_get_children(_GIMP_ITEM(top), &childCount);
#endif
      for (int i = childCount - 1; i >= 0; --i) {
        idStack.push(_GIMP_LAYER(children[i]));
      }
      g_free(children);
    } else {
      result.push_back(top);
    }
  }
  return result;
}

// Builds the list of input layers corresponding to an input mode.
std::vector<_GimpLayerPtr> getInputLayers(_GimpImagePtr imageId, GmicQt::InputMode mode)
{
  std::vector<_GimpLayerPtr> result;
  if (mode == GmicQt::InputMode::NoInput) {
    return result;
  }
  // Every libgimp query below is an IPC round trip: only do those that the mode really needs.
  const bool needsActiveLayer = (mode == GmicQt::InputMode::Active || mode == GmicQt::InputMode::ActiveAndBelow || mode == GmicQt::InputMode::ActiveAndAbove);
  const bool needsLayersList = (mode != GmicQt::InputMode::Active);
  const std::vector<_GimpLayerPtr> layers = needsLayersList ? getFlatLayersList(imageId) : std::vector<_GimpLayerPtr>();
  const _GimpLayerPtr activeLayer = needsActiveLayer ? getActiveLayer(imageId) : _GIMP_NULL_LAYER;
  const bool hasActiveLayer = needsActiveLayer && _GIMP_LAYER_IS_NOT_NULL(activeLayer) && !gimp_item_is_group(_GIMP_ITEM(activeLayer));
  switch (mode) {
  case GmicQt::InputMode::NoInput:
    break;
  case GmicQt::InputMode::Active:
    if (hasActiveLayer) {
      result.push_back(activeLayer);
    }
    break;
  case GmicQt::InputMode::All:
    result = layers;
    break;
  case GmicQt::InputMode::ActiveAndBelow:
    if (hasActiveLayer) {
      result.push_back(activeLayer);
      const auto it = std::find(layers.begin(), layers.end(), activeLayer);
      if (it != layers.end() && (it + 1) != layers.end()) {
        result.push_back(*(it + 1));
      }
    }
    break;
  case GmicQt::InputMode::ActiveAndAbove:
    if (hasActiveLayer) {
      const auto it = std::find(layers.begin(), layers.end(), activeLayer);
      if (it != layers.end() && it != layers.begin()) {
        result.push_back(*(it - 1));
      }
      result.push_back(activeLayer);
    }
    break;
  case GmicQt::InputMode::AllVisible:
  case GmicQt::InputMode::AllInvisible: {
    const bool visibility = (mode == GmicQt::InputMode::AllVisible);
    for (const _GimpLayerPtr layer : layers) {
      if (gimp_item_get_visible(_GIMP_ITEM(layer)) == visibility) {
        result.push_back(layer);
      }
    }
  } break;
  default:
    break;
  }
  return result;
}

} // namespace

namespace GmicQtHost
{

void showMessage(const char * message)
{
  static bool first = true;

  if (first) {
    gimp_progress_init(message);
    first = false;
  } else {
    gimp_progress_set_text_printf("%s", message);
  }
}

void applyColorProfile(gmic_library::gmic_image<float> & image)
{
  // TODO: not implemented. For GIMP >= 2.9, the idea is to get the image profile
  // (gimp_image_get_effective_color_profile()) and the color configuration
  // (gimp_get_color_configuration()), then to transform the pixels with a GimpColorTransform
  // ("R'G'B' float" / "R'G'B'A float" babl formats).
  unused(image);
}

void getLayersExtent(int * width, int * height, GmicQt::InputMode mode)
{
  const std::vector<_GimpLayerPtr> layers = getInputLayers(gmic_qt_gimp_image_id, mode);

  gint rgn_x, rgn_y, rgn_width, rgn_height;
  *width = 0;
  *height = 0;
  for (const _GimpLayerPtr layer : layers) {
    if (!gimp_item_is_valid(_GIMP_ITEM(layer))) {
      continue;
    }
    if (!gimp_drawable_mask_intersect(_GIMP_DRAWABLE(layer), &rgn_x, &rgn_y, &rgn_width, &rgn_height)) {
      continue;
    }
    *width = std::max(*width, rgn_width);
    *height = std::max(*height, rgn_height);
  }
}

void getCroppedImages(gmic_list<float> & images, gmic_list<char> & imageNames, double x, double y, double width, double height, GmicQt::InputMode mode)
{
  using gmic_library::gmic_image;
  using gmic_library::gmic_list;
  const bool entireImage = (x < 0 && y < 0 && width < 0 && height < 0) || (x == 0.0 && y == 0.0 && width == 1.0 && height == 1.0);
  if (entireImage) {
    x = 0.0;
    y = 0.0;
    width = 1.0;
    height = 1.0;
  }

  // Build list of input layers
  inputLayers = getInputLayers(gmic_qt_gimp_image_id, mode);

  // Retrieve image list
  images.assign(inputLayers.size());
  imageNames.assign(inputLayers.size());
  inputLayerDimensions.assign(inputLayers.size(), 4, 1, 1, 0); // Zero-filled (assign() alone leaves the buffer uninitialized)
  gint rgn_x, rgn_y, rgn_width, rgn_height;

  gboolean isSelection = 0;
  gint selX1 = 0, selY1 = 0, selX2 = 0, selY2 = 0;
  if (!gimp_selection_bounds(gmic_qt_gimp_image_id, &isSelection, &selX1, &selY1, &selX2, &selY2)) {
    isSelection = 0;
    selX1 = 0;
    selY1 = 0;
  }

  cimglist_for(images, l)
  {
    if (!gimp_item_is_valid(_GIMP_ITEM(inputLayers[l]))) {
      continue;
    }
    if (!gimp_drawable_mask_intersect(_GIMP_DRAWABLE(inputLayers[l]), &rgn_x, &rgn_y, &rgn_width, &rgn_height)) {
      inputLayerDimensions(l, 0) = 0;
      inputLayerDimensions(l, 1) = 0;
      inputLayerDimensions(l, 2) = 0;
      inputLayerDimensions(l, 3) = 0;
      continue;
    }
    const int spectrum = (gimp_drawable_is_rgb(_GIMP_DRAWABLE(inputLayers[l])) ? 3 : 1) + (gimp_drawable_has_alpha(_GIMP_DRAWABLE(inputLayers[l])) ? 1 : 0);

    const int dw = static_cast<int>(_gimp_drawable_get_width(_GIMP_DRAWABLE(inputLayers[l])));
    const int dh = static_cast<int>(_gimp_drawable_get_height(_GIMP_DRAWABLE(inputLayers[l])));
    int ix = rgn_x;
    int iy = rgn_y;
    int iw = rgn_width;
    int ih = rgn_height;
    if (!entireImage) {
      // Crop rectangle, clamped to the drawable so that sizes can never be negative
      ix = std::max(0, std::min(dw, static_cast<int>(rgn_x + x * rgn_width)));
      iy = std::max(0, std::min(dh, static_cast<int>(rgn_y + y * rgn_height)));
      iw = std::max(0, std::min(dw - ix, static_cast<int>(1 + std::ceil(rgn_width * width))));
      ih = std::max(0, std::min(dh - iy, static_cast<int>(1 + std::ceil(rgn_height * height))));
    }
    if (iw <= 0 || ih <= 0) {
      continue; // Empty crop: dimensions remain zero, the image remains empty
    }

    inputLayerDimensions(l, 0) = iw;
    inputLayerDimensions(l, 1) = ih;
    inputLayerDimensions(l, 2) = 1;
    inputLayerDimensions(l, 3) = spectrum;

    const double opacity = gimp_layer_get_opacity(inputLayers[l]);
    const GimpLayerModeEffects blendMode = gimp_layer_get_mode(inputLayers[l]);
    int xPos = 0;
    int yPos = 0;
    if (isSelection) {
      xPos = selX1;
      yPos = selY1;
    } else {
      _gimp_drawable_get_offsets(_GIMP_DRAWABLE(inputLayers[l]), &xPos, &yPos);
    }
    QString noParenthesisName = QString::fromUtf8(getItemName(_GIMP_ITEM(inputLayers[l])));
    noParenthesisName.replace(QChar('('), QChar(21)).replace(QChar(')'), QChar(22));

    QString name = QString("mode(%1),opacity(%2),pos(%3,%4),name(%5)").arg(blendingMode2String(blendMode)).arg(opacity).arg(xPos).arg(yPos).arg(noParenthesisName);
    QByteArray ba = name.toUtf8();
    gmic_image<char>::string(ba.constData()).move_to(imageNames[l]);
    GeglRectangle rect;
    gegl_rectangle_set(&rect, ix, iy, iw, ih);
    GeglBuffer * buffer = gimp_drawable_get_buffer(_GIMP_DRAWABLE(inputLayers[l]));
    const char * const format = spectrum == 1 ? "Y' " gmic_pixel_type_str : spectrum == 2 ? "Y'A " gmic_pixel_type_str : spectrum == 3 ? "R'G'B' " gmic_pixel_type_str : "R'G'B'A " gmic_pixel_type_str;
    gmic_image<float> img;
    readBufferToPlanar255(buffer, &rect, format, spectrum, img);
    g_object_unref(buffer);
    img.move_to(images[l]);
  }
}

void outputImages(gmic_list<gmic_pixel_type> & images, const gmic_list<char> & imageNames, GmicQt::OutputMode outputMode)
{
  // Output modes in original gmic_gimp_gtk : 0/Replace 1/New layer 2/New active layer  3/New image

  // spectrum to GIMP image types conversion table
  GimpImageType spectrum2gimpImageTypes[5] = {GIMP_INDEXED_IMAGE, // (unused)
                                              GIMP_GRAY_IMAGE, GIMP_GRAYA_IMAGE, GIMP_RGB_IMAGE, GIMP_RGBA_IMAGE};

  // Get output layers dimensions and check if input/output layers have compatible dimensions.
  unsigned int max_spectrum = 0;
  struct Position {
    gint x, y;
    Position() : x(0), y(0) {}
  } top_left, bottom_right;
  // Empty images are kept in the list (and skipped later), so that indices remain
  // aligned with inputLayers, inputLayerDimensions and imageNames.
  bool hasOutput = false;
  cimglist_for(images, l)
  {
    if (images[l].is_empty()) {
      continue;
    }
    hasOutput = true;

    bottom_right.x = std::max(bottom_right.x, (gint)images[l]._width);
    bottom_right.y = std::max(bottom_right.y, (gint)images[l]._height);
    if (images[l]._spectrum > max_spectrum) {
      max_spectrum = images[l]._spectrum;
    }
  }

  if (!hasOutput) {
    return; // Nothing to transfer back into GIMP (and we must not delete the input layers)
  }

  const int image_nb_layers = static_cast<int>(getFlatLayersList(gmic_qt_gimp_image_id).size());
  const unsigned int image_width = _gimp_image_get_width(gmic_qt_gimp_image_id);
  const unsigned int image_height = _gimp_image_get_height(gmic_qt_gimp_image_id);

  int is_selection = 0, sel_x0 = 0, sel_y0 = 0, sel_x1 = 0, sel_y1 = 0;
  if (!gimp_selection_bounds(gmic_qt_gimp_image_id, &is_selection, &sel_x0, &sel_y0, &sel_x1, &sel_y1)) {
    is_selection = 0;
  } else if (outputMode == GmicQt::OutputMode::InPlace || outputMode == GmicQt::OutputMode::NewImage) {
    sel_x0 = sel_y0 = 0;
  }

  bool is_compatible_dimensions = (images.size() == inputLayers.size());
  for (unsigned int p = 0; p < images.size() && is_compatible_dimensions; ++p) {
    const gmic_library::gmic_image<gmic_pixel_type> & img = images[p];
    if (img.width() != inputLayerDimensions(p, 0) || img.height() != inputLayerDimensions(p, 1)) {
      is_compatible_dimensions = false;
    }
  }

  // Transfer output layers back into GIMP.
  GimpLayerModeEffects layer_blendmode = GIMP_NORMAL_MODE;
  gint layer_posx = 0, layer_posy = 0;
  double layer_opacity = 100;
  gmic_library::gmic_image<char> layer_name;

  if (outputMode == GmicQt::OutputMode::InPlace) {
    gint rgn_x, rgn_y, rgn_width, rgn_height;
    GimpUndoGroupGuard undoGroup(gmic_qt_gimp_image_id);
    if (is_compatible_dimensions) { // Direct replacement of the layer data.
      // Add an alpha channel to the input layers whose output has one (undoable, and only here).
      for (unsigned int p = 0; p < images.size(); ++p) {
        const bool source_is_alpha = (inputLayerDimensions(p, 3) == 2 || inputLayerDimensions(p, 3) >= 4);
        const bool dest_is_alpha = (images[p].spectrum() == 2 || images[p].spectrum() >= 4);
        if (dest_is_alpha && !source_is_alpha) {
          gimp_layer_add_alpha(inputLayers[p]);
          ++inputLayerDimensions(p, 3);
        }
      }
      for (unsigned int p = 0; p < images.size(); ++p) {
        if (images[p].is_empty()) {
          continue;
        }
        layer_blendmode = gimp_layer_get_mode(inputLayers[p]);
        layer_opacity = gimp_layer_get_opacity(inputLayers[p]);
        _gimp_drawable_get_offsets(_GIMP_DRAWABLE(inputLayers[p]), &layer_posx, &layer_posy);
        const QByteArray previousName = getItemName(_GIMP_ITEM(inputLayers[p]));
        gmic_library::gmic_image<char>::string(previousName.constData()).move_to(layer_name);
        const GimpLayerModeEffects previousMode = layer_blendmode;
        const double previousOpacity = layer_opacity;
        const gint previousPosX = layer_posx;
        const gint previousPosY = layer_posy;
        get_output_layer_props(imageNames[p], layer_blendmode, layer_opacity, layer_posx, layer_posy, layer_name);
        gmic_library::gmic_image<gmic_pixel_type> & img = images[p];
        GmicQt::calibrateImage(img, inputLayerDimensions(p, 3), false);
        if (gimp_drawable_mask_intersect(_GIMP_DRAWABLE(inputLayers[p]), &rgn_x, &rgn_y, &rgn_width, &rgn_height)) {
          GeglRectangle rect;
          gegl_rectangle_set(&rect, rgn_x, rgn_y, rgn_width, rgn_height);
          GeglBuffer * buffer = gimp_drawable_get_shadow_buffer(_GIMP_DRAWABLE(inputLayers[p]));
          writePlanar255ToBuffer(buffer, &rect, img);
          g_object_unref(buffer);
          gimp_drawable_merge_shadow(_GIMP_DRAWABLE(inputLayers[p]), true);
          gimp_drawable_update(_GIMP_DRAWABLE(inputLayers[p]), rgn_x, rgn_y, rgn_width, rgn_height);
          if (layer_blendmode != previousMode) {
            gimp_layer_set_mode(inputLayers[p], layer_blendmode);
          }
          // The opacity went through a text round trip (name string), hence the tolerance.
          if (std::fabs(layer_opacity - previousOpacity) > 1e-3) {
            gimp_layer_set_opacity(inputLayers[p], layer_opacity);
          }
          if (!is_selection) {
            if (layer_posx != previousPosX || layer_posy != previousPosY) {
              gimp_layer_set_offsets(inputLayers[p], layer_posx, layer_posy);
            }
          } else {
            gimp_item_transform_translate(_GIMP_ITEM(inputLayers[p]), 0, 0);
          }
          if (layer_name && std::strcmp(layer_name.data(), previousName.constData()) != 0) {
            _GIMP_ITEM_SET_NAME(_GIMP_ITEM(inputLayers[p]), layer_name);
          }
        }
        img.assign();
      }
    } else { // Indirect replacement: create new layers.
      gimp_selection_none(gmic_qt_gimp_image_id);
      // inputLayers may be empty (e.g. input mode "None"): insert at the top in that case.
      const int layer_pos = inputLayers.empty() ? 0 : gimp_image_get_item_position(gmic_qt_gimp_image_id, _GIMP_ITEM(inputLayers[0]));
      unsigned int outIndex = 0; // Number of layers inserted so far
      top_left.x = top_left.y = 0;
      bottom_right.x = bottom_right.y = 0;
      for (unsigned int p = 0; p < images.size(); ++p) {
        if (images[p]) {
          layer_posx = layer_posy = 0;
          if (p < inputLayers.size()) {
            layer_blendmode = gimp_layer_get_mode(inputLayers[p]);
            layer_opacity = gimp_layer_get_opacity(inputLayers[p]);
            if (!is_selection) {
              _gimp_drawable_get_offsets(_GIMP_DRAWABLE(inputLayers[p]), &layer_posx, &layer_posy);
            }
            gmic_library::gmic_image<char>::string(getItemName(_GIMP_ITEM(inputLayers[p])).constData()).move_to(layer_name);
            gimp_image_remove_layer(gmic_qt_gimp_image_id, inputLayers[p]);
          } else {
            layer_blendmode = GIMP_NORMAL_MODE;
            layer_opacity = 100;
            layer_name.assign();
          }
          get_output_layer_props(imageNames[p], layer_blendmode, layer_opacity, layer_posx, layer_posy, layer_name);
          if (is_selection) {
            layer_posx = 0;
            layer_posy = 0;
          }
          top_left.x = std::min(top_left.x, layer_posx);
          top_left.y = std::min(top_left.y, layer_posy);
          bottom_right.x = std::max(bottom_right.x, (gint)(layer_posx + images[p]._width));
          bottom_right.y = std::max(bottom_right.y, (gint)(layer_posy + images[p]._height));
          gmic_library::gmic_image<gmic_pixel_type> & img = images[p];
          if (_gimp_image_get_base_type(gmic_qt_gimp_image_id) == GIMP_GRAY) {
            GmicQt::calibrateImage(img, (img.spectrum() == 1 || img.spectrum() == 3) ? 1 : 2, false);
          } else {
            GmicQt::calibrateImage(img, (img.spectrum() == 1 || img.spectrum() == 3) ? 3 : 4, false);
          }
          _GimpLayerPtr layer_id = gimp_layer_new(gmic_qt_gimp_image_id, nullptr, img.width(), img.height(), spectrum2gimpImageTypes[std::min(img.spectrum(), 4)], layer_opacity, layer_blendmode);
          gimp_layer_set_offsets(layer_id, layer_posx, layer_posy);
          if (layer_name) {
            _GIMP_ITEM_SET_NAME(_GIMP_ITEM(layer_id), layer_name);
          }
          gimp_image_insert_layer(gmic_qt_gimp_image_id, layer_id, _GIMP_NULL_LAYER, static_cast<gint>(layer_pos + outIndex++));

          GeglBuffer * buffer = gimp_drawable_get_shadow_buffer(_GIMP_DRAWABLE(layer_id));
          writePlanar255ToBuffer(buffer, nullptr, img);
          g_object_unref(buffer);
          gimp_drawable_merge_shadow(_GIMP_DRAWABLE(layer_id), true);
          updateWholeDrawable(layer_id);
          img.assign();
        }
      }
      const unsigned int max_width = bottom_right.x - top_left.x;
      const unsigned int max_height = bottom_right.y - top_left.y;
      for (unsigned int p = images._width; p < inputLayers.size(); ++p) {
        gimp_image_remove_layer(gmic_qt_gimp_image_id, inputLayers[p]);
      }
      if (max_width && max_height) { // gimp_image_resize() fails with a null size
        if ((unsigned int)image_nb_layers == inputLayers.size()) {
          gimp_image_resize(gmic_qt_gimp_image_id, max_width, max_height, -top_left.x, -top_left.y);
        } else {
          gimp_image_resize(gmic_qt_gimp_image_id, std::max(image_width, max_width), std::max(image_height, max_height), 0, 0);
        }
      }
    }
  } else if (outputMode == GmicQt::OutputMode::NewActiveLayers || outputMode == GmicQt::OutputMode::NewLayers) {
    _GimpLayerPtr active_layer_id = getActiveLayer(gmic_qt_gimp_image_id);
    if (_GIMP_LAYER_IS_NOT_NULL(active_layer_id)) {
      GimpUndoGroupGuard undoGroup(gmic_qt_gimp_image_id);
      _GimpLayerPtr top_layer_id = _gimp_top_layer;
      _GimpLayerPtr layer_id = _gimp_top_layer;
      bool hasTopLayer = false;
      unsigned int outIndex = 0; // Number of layers inserted so far
      top_left.x = top_left.y = 0;
      bottom_right.x = bottom_right.y = 0;
      for (unsigned int p = 0; p < images.size(); ++p) {
        if (images[p]) {
          layer_blendmode = GIMP_NORMAL_MODE;
          layer_opacity = 100;
          layer_posx = layer_posy = 0;
          if (inputLayers.size() == 1) {
            if (!is_selection) {
              _gimp_drawable_get_offsets(_GIMP_DRAWABLE(active_layer_id), &layer_posx, &layer_posy);
            }
            gmic_library::gmic_image<char>::string(getItemName(_GIMP_ITEM(active_layer_id)).constData()).move_to(layer_name);
          } else {
            layer_name.assign();
          }
          get_output_layer_props(imageNames[p], layer_blendmode, layer_opacity, layer_posx, layer_posy, layer_name);
          top_left.x = std::min(top_left.x, layer_posx);
          top_left.y = std::min(top_left.y, layer_posy);
          bottom_right.x = std::max(bottom_right.x, (gint)(layer_posx + images[p]._width));
          bottom_right.y = std::max(bottom_right.y, (gint)(layer_posy + images[p]._height));

          gmic_library::gmic_image<gmic_pixel_type> & img = images[p];
          if (_gimp_image_get_base_type(gmic_qt_gimp_image_id) == GIMP_GRAY) {
            GmicQt::calibrateImage(img, !is_selection && (img.spectrum() == 1 || img.spectrum() == 3) ? 1 : 2, false);
          } else {
            GmicQt::calibrateImage(img, !is_selection && (img.spectrum() == 1 || img.spectrum() == 3) ? 3 : 4, false);
          }
          layer_id = gimp_layer_new(gmic_qt_gimp_image_id, nullptr, img.width(), img.height(), spectrum2gimpImageTypes[std::min(img.spectrum(), 4)], layer_opacity, layer_blendmode);
          if (!hasTopLayer) {
            top_layer_id = layer_id;
            hasTopLayer = true;
          }
          gimp_layer_set_offsets(layer_id, layer_posx, layer_posy);
          if (layer_name) {
            _GIMP_ITEM_SET_NAME(_GIMP_ITEM(layer_id), layer_name);
          }
          gimp_image_insert_layer(gmic_qt_gimp_image_id, layer_id, _GIMP_NULL_LAYER, static_cast<gint>(outIndex++));

          GeglBuffer * buffer = gimp_drawable_get_shadow_buffer(_GIMP_DRAWABLE(layer_id));
          writePlanar255ToBuffer(buffer, nullptr, img);
          g_object_unref(buffer);
          gimp_drawable_merge_shadow(_GIMP_DRAWABLE(layer_id), true);
          updateWholeDrawable(layer_id);
          img.assign();
        }
      } // End of the loop on output images

      // Resize the image and select the layers once, after all layers have been added.
      const unsigned int max_width = bottom_right.x - top_left.x;
      const unsigned int max_height = bottom_right.y - top_left.y;
      const unsigned int Mw = std::max(image_width, max_width);
      const unsigned int Mh = std::max(image_height, max_height);
      if (Mw && Mh) {
        gimp_image_resize(gmic_qt_gimp_image_id, Mw, Mh, -top_left.x, -top_left.y);
      }

#if GIMP_CHECK_VERSION(3, 0, 0)
      GimpLayer * selected_layer[2] = {0};
      if (outputMode == GmicQt::OutputMode::NewLayers) {
        selected_layer[0] = active_layer_id;
      } else {
        selected_layer[0] = top_layer_id;
      }
      gimp_image_set_selected_layers(gmic_qt_gimp_image_id, (const GimpLayer **)selected_layer);
#else
      if (outputMode == GmicQt::OutputMode::NewLayers) {
        gimp_image_set_active_layer(gmic_qt_gimp_image_id, active_layer_id);
      } else {
        gimp_image_set_active_layer(gmic_qt_gimp_image_id, top_layer_id);
      }
#endif
    }
  } else if (outputMode == GmicQt::OutputMode::NewImage && images.size()) {
    _GimpLayerPtr active_layer_id = getActiveLayer(gmic_qt_gimp_image_id);
    const unsigned int max_width = (unsigned int)bottom_right.x;
    const unsigned int max_height = (unsigned int)bottom_right.y;
    if (_GIMP_LAYER_IS_NOT_NULL(active_layer_id)) {
      _GimpImagePtr nimage_id = gimp_image_new_with_precision(max_width, max_height, max_spectrum <= 2 ? GIMP_GRAY : GIMP_RGB, gimp_image_get_precision(gmic_qt_gimp_image_id));
      GimpUndoGroupGuard undoGroup(nimage_id);
      unsigned int outIndex = 0; // Number of layers inserted so far
      for (unsigned int p = 0; p < images.size(); ++p) {
        if (images[p]) {
          layer_blendmode = GIMP_NORMAL_MODE;
          layer_opacity = 100;
          layer_posx = layer_posy = 0;
          if (inputLayers.size() == 1) {
            if (!is_selection) {
              _gimp_drawable_get_offsets(_GIMP_DRAWABLE(active_layer_id), &layer_posx, &layer_posy);
            }
            gmic_library::gmic_image<char>::string(getItemName(_GIMP_ITEM(active_layer_id)).constData()).move_to(layer_name);
          } else {
            layer_name.assign();
          }
          get_output_layer_props(imageNames[p], layer_blendmode, layer_opacity, layer_posx, layer_posy, layer_name);
          if (is_selection) {
            layer_posx = 0;
            layer_posy = 0;
          }
          gmic_library::gmic_image<gmic_pixel_type> & img = images[p];
          if (_gimp_image_get_base_type(nimage_id) != GIMP_GRAY) {
            GmicQt::calibrateImage(img, (img.spectrum() == 1 || img.spectrum() == 3) ? 3 : 4, false);
          }
          _GimpLayerPtr layer_id = gimp_layer_new(nimage_id, nullptr, img.width(), img.height(), spectrum2gimpImageTypes[std::min(img.spectrum(), 4)], layer_opacity, layer_blendmode);
          gimp_layer_set_offsets(layer_id, layer_posx, layer_posy);
          if (layer_name) {
            _GIMP_ITEM_SET_NAME(_GIMP_ITEM(layer_id), layer_name);
          }
          gimp_image_insert_layer(nimage_id, layer_id, _GIMP_NULL_LAYER, static_cast<gint>(outIndex++));

          GeglBuffer * buffer = gimp_drawable_get_shadow_buffer(_GIMP_DRAWABLE(layer_id));
          writePlanar255ToBuffer(buffer, nullptr, img);
          g_object_unref(buffer);
          gimp_drawable_merge_shadow(_GIMP_DRAWABLE(layer_id), true);
          updateWholeDrawable(layer_id);
          img.assign();
        }
      }
      gimp_display_new(nimage_id);
    }
  }

  gimp_displays_flush();
}

} // namespace GmicQtHost

#if !GIMP_CHECK_VERSION(3, 0, 0)
/*
 * 'Run' function, required by the GIMP plug-in API.
 */
void gmic_qt_run(const gchar * /* name */, gint /* nparams */, const GimpParam * param, gint * nreturn_vals, GimpParam ** return_vals)
{
  gegl_init(nullptr, nullptr);
  gimp_plugin_enable_precision();

  GmicQt::RunParameters pluginParameters;
  bool accepted = true;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  static GimpParam return_values[1];
  *return_vals = return_values;
  *nreturn_vals = 1;
  return_values[0].type = GIMP_PDB_STATUS;
  const GimpRunMode run_mode = (GimpRunMode)param[0].data.d_int32;
  switch (run_mode) {
  case GIMP_RUN_INTERACTIVE:
    gmic_qt_gimp_image_id = param[1].data.d_image;
    GmicQt::run(GmicQt::UserInterfaceMode::Full, //
                GmicQt::RunParameters(),         //
                std::list<GmicQt::InputMode>(),  //
                std::list<GmicQt::OutputMode>(), //
                &accepted);
    break;
  case GIMP_RUN_WITH_LAST_VALS:
    gmic_qt_gimp_image_id = param[1].data.d_image;
    GmicQt::run(GmicQt::UserInterfaceMode::ProgressDialog,                                                       //
                GmicQt::lastAppliedFilterRunParameters(GmicQt::ReturnedRunParametersFlag::AfterFilterExecution), //
                std::list<GmicQt::InputMode>(),                                                                  //
                std::list<GmicQt::OutputMode>(),                                                                 //
                &accepted);
    break;
  case GIMP_RUN_NONINTERACTIVE: {
    const gint32 inputMode = param[3].data.d_int32;
    const gint32 outputMode = param[4].data.d_int32;
    const gchar * command = param[5].data.d_string;
    if (inputMode < 0 || inputMode > 6 || outputMode < 0 || outputMode > 3) {
      status = GIMP_PDB_CALLING_ERROR;
      break;
    }
    gmic_qt_gimp_image_id = param[1].data.d_image;
    pluginParameters.command = command ? command : "";
    pluginParameters.inputMode = static_cast<GmicQt::InputMode>(inputMode + (int)GmicQt::InputMode::NoInput);
    pluginParameters.outputMode = static_cast<GmicQt::OutputMode>(outputMode + (int)GmicQt::OutputMode::InPlace);
    run(GmicQt::UserInterfaceMode::Silent, //
        pluginParameters,                  //
        std::list<GmicQt::InputMode>(),    //
        std::list<GmicQt::OutputMode>(),   //
        &accepted);
  } break;
  default:
    status = GIMP_PDB_CALLING_ERROR;
    break;
  }
  if (status == GIMP_PDB_SUCCESS && !accepted) {
    status = GIMP_PDB_CANCEL;
  }
  return_values[0].data.d_status = status;
}

void gmic_qt_query()
{
  static const GimpParamDef args[] = {{GIMP_PDB_INT32, (gchar *)"run_mode", (gchar *)"Interactive, non-interactive"},
                                      {GIMP_PDB_IMAGE, (gchar *)"image", (gchar *)"Input image"},
                                      {GIMP_PDB_DRAWABLE, (gchar *)"drawable", (gchar *)"Input drawable (unused)"},
                                      {GIMP_PDB_INT32, (gchar *)"input",
                                       (gchar *)"Input layers mode, when non-interactive"
                                                " (0=none, 1=active, 2=all, 3=active and below, 4=active and above, 5=all visible, 6=all invisible)"},
                                      {GIMP_PDB_INT32, (gchar *)"output",
                                       (gchar *)"Output mode, when non-interactive "
                                                "(0=in place,1=new layers,2=new active layers,3=new image)"},
                                      {GIMP_PDB_STRING, (gchar *)"command", (gchar *)"G'MIC command string, when non-interactive"}};

  const char name[] = "plug-in-gmic-qt";
  QByteArray blurb = QString("G'MIC-Qt (%1)").arg(GmicQt::gmicVersionString()).toLatin1();
  QByteArray path("G'MIC-Qt...");
  path.prepend("_");
  gimp_install_procedure(name,                      // name
                         blurb.constData(),         // blurb
                         blurb.constData(),         // help
                         "S\303\251bastien Fourey", // author
                         "S\303\251bastien Fourey", // copyright
                         "2017",                    // date
                         path.constData(),          // menu_path
                         "RGB*, GRAY*",             // image_types
                         GIMP_PLUGIN,               // type
                         G_N_ELEMENTS(args),        // nparams
                         0,                         // nreturn_vals
                         args,                      // params
                         nullptr);                  // return_vals
  gimp_plugin_menu_register(name, "<Image>/Filters");
}

GimpPlugInInfo PLUG_IN_INFO = {nullptr, nullptr, gmic_qt_query, gmic_qt_run};

MAIN()

#else

static GList * gmic_qt_query(GimpPlugIn * plug_in)
{
  unused(plug_in);
  return g_list_append(NULL, g_strdup(PLUG_IN_PROC));
}

/*
 * 'Run' function, required by the GIMP plug-in API.
 */

static GimpValueArray * gmic_qt_run(GimpProcedure * procedure, GimpRunMode run_mode, GimpImage * image, GimpDrawable ** drawables, GimpProcedureConfig * config, gpointer run_data)
{
  unused(drawables, run_data);
  gegl_init(NULL, NULL);
  GmicQt::RunParameters pluginParameters;
  bool accepted = true;
  GimpPDBStatusType status = GIMP_PDB_SUCCESS;
  switch (run_mode) {
  case GIMP_RUN_INTERACTIVE:
    gmic_qt_gimp_image_id = image;
    GmicQt::run(GmicQt::UserInterfaceMode::Full,
                GmicQt::RunParameters(),         //
                std::list<GmicQt::InputMode>(),  //
                std::list<GmicQt::OutputMode>(), //
                &accepted);
    break;
  case GIMP_RUN_WITH_LAST_VALS:
    gmic_qt_gimp_image_id = image;
    GmicQt::run(GmicQt::UserInterfaceMode::ProgressDialog,                                                       //
                GmicQt::lastAppliedFilterRunParameters(GmicQt::ReturnedRunParametersFlag::AfterFilterExecution), //
                std::list<GmicQt::InputMode>(),                                                                  //
                std::list<GmicQt::OutputMode>(),                                                                 //
                &accepted);
    break;
  case GIMP_RUN_NONINTERACTIVE: {
    char * command = nullptr;
    int inputMode = 0;
    int outputMode = 0;
    g_object_get(config, "command", &command, "input", &inputMode, "output", &outputMode, NULL);
    if (inputMode < 0 || inputMode > 6 || outputMode < 0 || outputMode > 3) {
      g_free(command);
      status = GIMP_PDB_CALLING_ERROR;
      break;
    }
    gmic_qt_gimp_image_id = image;
    pluginParameters.command = command ? command : "";
    pluginParameters.inputMode = (GmicQt::InputMode)inputMode;
    pluginParameters.outputMode = (GmicQt::OutputMode)outputMode;
    g_free(command);
    GmicQt::run(GmicQt::UserInterfaceMode::Silent, //
                pluginParameters,                  //
                std::list<GmicQt::InputMode>(),    //
                std::list<GmicQt::OutputMode>(),   //
                &accepted);
  } break;
  default:
    status = GIMP_PDB_CALLING_ERROR;
    break;
  }
  gegl_exit();
  if (status == GIMP_PDB_SUCCESS && !accepted) {
    status = GIMP_PDB_CANCEL;
  }
  return gimp_procedure_new_return_values(procedure, status, NULL);
}

static GimpProcedure * gmic_qt_create_procedure(GimpPlugIn * plug_in, const gchar * name)
{
  GimpProcedure * procedure = NULL;

  if (strcmp(name, PLUG_IN_PROC) == 0) {
    procedure = gimp_image_procedure_new(plug_in, name, GIMP_PDB_PROC_TYPE_PLUGIN, gmic_qt_run, NULL, NULL);

    gimp_procedure_set_image_types(procedure, "RGB*, GRAY*");

    QByteArray path("G'MIC-Qt...");
    path.prepend("_");
    gimp_procedure_set_menu_label(procedure, path.constData());
    gimp_procedure_add_menu_path(procedure, "<Image>/Filters");

    QByteArray blurb = QString("G'MIC-Qt (%1)").arg(GmicQt::gmicVersionString()).toLatin1();
    gimp_procedure_set_documentation(procedure,
                                     blurb.constData(), // blurb
                                     blurb.constData(), // help
                                     name);             // help_id

    gimp_procedure_set_attribution(procedure,
                                   "S\303\251bastien Fourey", // author
                                   "S\303\251bastien Fourey", // copyright
                                   "2017");                   // date

    gimp_procedure_add_int_argument(procedure,
                                    "input", // name
                                    "input", // nick
                                    "Input layers mode, when non-interactive (0=none, 1=active, 2=all, 3=active and below, 4=active and above, 5=all visible, 6=all invisible)", // blurb
                                    0,                   // min
                                    6,                   // max
                                    0,                   // default
                                    G_PARAM_READWRITE);  // flags

    gimp_procedure_add_int_argument(procedure,
                                    "output", // name
                                    "output", // nick
                                    "Output mode, when non-interactive (0=in place,1=new layers,2=new active layers,3=new image)", // blurb
                                    0,                   // min
                                    3,                   // max
                                    0,                   // default
                                    G_PARAM_READWRITE);  // flags

    gimp_procedure_add_string_argument(procedure,
                                       "command",                                    // name
                                       "command",                                    // nick
                                       "G'MIC command string, when non-interactive", // blurb
                                       "",                                           // default
                                       G_PARAM_READWRITE);                           // flags
  }

  return procedure;
}
#endif
