// vio._vio - CPython extension over the C interface (bind/c/vio/vio.h).  The Python
// layer (vio/__init__.py) turns its results into NumPy arrays.
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <cstring>

#define VIO_IMPLEMENTATION
#include "vio.h"

namespace {

PyObject* g_error = nullptr;

PyObject* raise(vio_status s) {
  PyObject* args = Py_BuildValue("(si)", vio_last_error(), static_cast<int>(s));
  if (args) {
    PyErr_SetObject(g_error, args);
    Py_DECREF(args);
  }
  return nullptr;
}

vio_format parse_format(const char* name) {
  static const struct {
    const char* name;
    vio_format format;
  } kNames[] = {{"jpeg", VIO_FORMAT_JPEG}, {"jpg", VIO_FORMAT_JPEG}, {"png", VIO_FORMAT_PNG},
                {"bmp", VIO_FORMAT_BMP},   {"gif", VIO_FORMAT_GIF},  {"ppm", VIO_FORMAT_PPM},
                {"pgm", VIO_FORMAT_PPM},   {"pnm", VIO_FORMAT_PPM},  {"tga", VIO_FORMAT_TGA},
                {"targa", VIO_FORMAT_TGA}};
  for (const auto& n : kNames)
    if (std::strcmp(n.name, name) == 0) return n.format;
  return VIO_FORMAT_UNKNOWN;
}

// ---- Pixels: a decoded image, exported through the buffer protocol ---------

struct Pixels {
  PyObject_HEAD
  vio_image image;
  Py_ssize_t shape[3];
  Py_ssize_t strides[3];
};

void pixels_dealloc(PyObject* self) {
  vio_image_free(&reinterpret_cast<Pixels*>(self)->image);
  Py_TYPE(self)->tp_free(self);
}

int pixels_getbuffer(PyObject* self, Py_buffer* view, int flags) {
  Pixels* p = reinterpret_cast<Pixels*>(self);
  const vio_image& im = p->image;
  const Py_ssize_t item = im.depth / 8;
  view->obj = self;
  Py_INCREF(self);
  view->buf = im.data;
  view->len = static_cast<Py_ssize_t>(im.stride * im.height);
  view->readonly = 0;
  view->itemsize = item;
  view->format = (flags & PyBUF_FORMAT) ? const_cast<char*>(item == 1 ? "B" : "H") : nullptr;
  view->ndim = 3;
  view->shape = (flags & PyBUF_ND) ? p->shape : nullptr;
  view->strides = (flags & PyBUF_STRIDES) ? p->strides : nullptr;
  view->suboffsets = nullptr;
  view->internal = nullptr;
  return 0;
}

PyBufferProcs pixels_as_buffer = {pixels_getbuffer, nullptr};

PyTypeObject PixelsType = {PyVarObject_HEAD_INIT(nullptr, 0)};

PyObject* make_pixels(const vio_image& im) {
  Pixels* p = PyObject_New(Pixels, &PixelsType);
  if (!p) {
    vio_image tmp = im;
    vio_image_free(&tmp);
    return nullptr;
  }
  p->image = im;
  const Py_ssize_t item = im.depth / 8;
  p->shape[0] = im.height;
  p->shape[1] = im.width;
  p->shape[2] = im.channels;
  p->strides[0] = static_cast<Py_ssize_t>(im.stride);
  p->strides[1] = item * im.channels;
  p->strides[2] = item;
  return reinterpret_cast<PyObject*>(p);
}

// ---- functions -------------------------------------------------------------

// decode(data, channels=0, depth=0) -> Pixels
PyObject* py_decode(PyObject*, PyObject* args, PyObject* kw) {
  static const char* kwlist[] = {"data", "channels", "depth", nullptr};
  Py_buffer data;
  unsigned channels = 0, depth = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kw, "y*|II", const_cast<char**>(kwlist), &data, &channels, &depth))
    return nullptr;
  vio_image im;
  vio_status s;
  Py_BEGIN_ALLOW_THREADS
  s = vio_decode(data.buf, static_cast<size_t>(data.len), channels, depth, &im);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&data);
  if (s != VIO_OK) return raise(s);
  return make_pixels(im);
}

// probe(data) -> (format, width, height, channels, depth)
PyObject* py_probe(PyObject*, PyObject* args) {
  Py_buffer data;
  if (!PyArg_ParseTuple(args, "y*", &data)) return nullptr;
  vio_info info;
  const vio_status s = vio_probe(data.buf, static_cast<size_t>(data.len), &info);
  PyBuffer_Release(&data);
  if (s != VIO_OK) return raise(s);
  return Py_BuildValue("(sIIII)", vio_format_name(info.format), info.width, info.height, info.channels,
                       info.depth);
}

// encode(pixels, width, height, channels, depth, format, quality=0,
//        subsampling=0, progressive=False, compression=0) -> bytes
// `pixels`: a C-contiguous buffer of height x width x channels samples.
PyObject* py_encode(PyObject*, PyObject* args, PyObject* kw) {
  static const char* kwlist[] = {"pixels",  "width",       "height",      "channels",    "depth", "format",
                                 "quality", "subsampling", "progressive", "compression", nullptr};
  Py_buffer px;
  unsigned width, height, channels, depth;
  const char* format;
  int quality = 0, subsampling = 0, progressive = 0, compression = 0;
  if (!PyArg_ParseTupleAndKeywords(args, kw, "y*IIIIs|iipi", const_cast<char**>(kwlist), &px, &width, &height,
                                   &channels, &depth, &format, &quality, &subsampling, &progressive, &compression))
    return nullptr;
  const vio_format f = parse_format(format);
  if (f == VIO_FORMAT_UNKNOWN) {
    PyBuffer_Release(&px);
    return PyErr_Format(PyExc_ValueError, "unknown image format '%s'", format);
  }
  if (static_cast<unsigned long long>(px.len) != 1ull * width * height * channels * (depth / 8)) {
    PyBuffer_Release(&px);
    return PyErr_Format(PyExc_ValueError, "pixel buffer size does not match the image");
  }
  vio_image im = {px.buf, width, height, channels, depth, 0};
  vio_options opt = {quality, subsampling, progressive, compression};
  void* out = nullptr;
  size_t size = 0;
  vio_status s;
  Py_BEGIN_ALLOW_THREADS
  s = vio_encode(&im, f, &opt, &out, &size);
  Py_END_ALLOW_THREADS
  PyBuffer_Release(&px);
  if (s != VIO_OK) return raise(s);
  PyObject* r = PyBytes_FromStringAndSize(static_cast<const char*>(out), static_cast<Py_ssize_t>(size));
  vio_free(out);
  return r;
}

// detect(data) -> format name ("unknown" if not recognized)
PyObject* py_detect(PyObject*, PyObject* args) {
  Py_buffer data;
  if (!PyArg_ParseTuple(args, "y*", &data)) return nullptr;
  const vio_format f = vio_detect(data.buf, static_cast<size_t>(data.len));
  PyBuffer_Release(&data);
  return PyUnicode_FromString(vio_format_name(f));
}

PyMethodDef kMethods[] = {
    {"decode", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(py_decode)), METH_VARARGS | METH_KEYWORDS,
     "decode(data, channels=0, depth=0) -> buffer of shape (height, width, channels)"},
    {"encode", reinterpret_cast<PyCFunction>(reinterpret_cast<void (*)()>(py_encode)), METH_VARARGS | METH_KEYWORDS,
     "encode(pixels, width, height, channels, depth, format, ...) -> bytes"},
    {"probe", py_probe, METH_VARARGS, "probe(data) -> (format, width, height, channels, depth)"},
    {"detect", py_detect, METH_VARARGS, "detect(data) -> format name"},
    {nullptr, nullptr, 0, nullptr}};

PyModuleDef kModule = {PyModuleDef_HEAD_INIT, "_vio", "vio C interface", -1, kMethods};

}  // namespace

PyMODINIT_FUNC PyInit__vio(void) {
  PixelsType.tp_name = "vio._vio.Pixels";
  PixelsType.tp_basicsize = sizeof(Pixels);
  PixelsType.tp_dealloc = pixels_dealloc;
  PixelsType.tp_as_buffer = &pixels_as_buffer;
  PixelsType.tp_flags = Py_TPFLAGS_DEFAULT;
  PixelsType.tp_doc = "Decoded pixels (buffer protocol)";
  if (PyType_Ready(&PixelsType) < 0) return nullptr;
  PyObject* m = PyModule_Create(&kModule);
  if (!m) return nullptr;
  g_error = PyErr_NewException("vio.Error", nullptr, nullptr);
  if (!g_error || PyModule_AddObject(m, "Error", g_error) < 0) {
    Py_XDECREF(g_error);
    Py_DECREF(m);
    return nullptr;
  }
  Py_INCREF(g_error);
  PyModule_AddStringConstant(m, "__version__", vio_version());
  return m;
}
