"""vio - fast image decoding and encoding: JPEG, PNG, BMP, GIF, PPM and Targa.

Images are NumPy arrays of shape (height, width) for gray or
(height, width, channels) with 2 (gray + alpha), 3 (RGB) or 4 (RGBA)
channels, of dtype uint8 or uint16.

    import vio
    img = vio.read("photo.jpg")                  # (h, w, 3) uint8
    vio.write("photo.png", img)
    data = vio.encode(img, "jpeg", quality=85)   # bytes
    img = vio.decode(data, channels=4)           # RGBA
    vio.info("photo.jpg")                        # Info(format='jpeg', ...)
"""

from __future__ import annotations

import os
from typing import NamedTuple, Optional, Union

import numpy as np

from . import _vio
from ._vio import Error

__all__ = ["Error", "Info", "read", "write", "decode", "encode", "info", "detect"]
__version__ = _vio.__version__

PathLike = Union[str, bytes, os.PathLike]
BytesLike = Union[bytes, bytearray, memoryview]

_EXTENSIONS = {
    ".jpg": "jpeg", ".jpeg": "jpeg", ".jpe": "jpeg", ".jfif": "jpeg",
    ".png": "png", ".apng": "png", ".bmp": "bmp", ".dib": "bmp", ".gif": "gif",
    ".ppm": "ppm", ".pgm": "ppm", ".pnm": "ppm", ".tga": "tga", ".targa": "tga",
}


class Info(NamedTuple):
    format: str
    width: int
    height: int
    channels: int
    depth: int  # bits per sample: 8 or 16


def _layout(channels: Optional[int], depth: Optional[int]):
    if channels is None:
        channels = 0
    if depth is None:
        depth = 0
    elif isinstance(depth, type) or isinstance(depth, np.dtype):
        depth = np.dtype(depth).itemsize * 8
    if channels not in (0, 1, 2, 3, 4):
        raise ValueError("channels must be 1, 2, 3 or 4")
    if depth not in (0, 8, 16):
        raise ValueError("depth must be 8 or 16 (or np.uint8 / np.uint16)")
    return channels, depth


def decode(data: BytesLike, channels: Optional[int] = None, depth=None) -> np.ndarray:
    """Decodes an image file held in memory.

    channels: 1 (gray), 2 (gray + alpha), 3 (RGB) or 4 (RGBA); default: the
    file's own.  depth: 8 / np.uint8 or 16 / np.uint16; default: the file's.
    """
    channels, depth = _layout(channels, depth)
    a = np.asarray(_vio.decode(data, channels, depth))
    return a[:, :, 0] if a.shape[2] == 1 else a


def read(path: PathLike, channels: Optional[int] = None, depth=None) -> np.ndarray:
    """Reads an image file; see decode()."""
    with open(path, "rb") as f:
        return decode(f.read(), channels, depth)


def _format_from_path(path: PathLike) -> str:
    ext = os.path.splitext(os.fsdecode(path))[1].lower()
    if ext not in _EXTENSIONS:
        raise ValueError(f"cannot tell the image format from the extension of {os.fsdecode(path)!r}")
    return _EXTENSIONS[ext]


def encode(image, format: str = "png", *, quality: int = 90, subsampling: int = 420,
           progressive: bool = False, compression: int = 0) -> bytes:
    """Encodes an image array into a file in memory.

    format: "jpeg", "png", "bmp", "ppm" or "tga".
    JPEG: quality 1..100, subsampling 444, 422 or 420, progressive.
    PNG: compression 0 (fast single-pass) or 1..9 (zlib level).
    Formats without alpha drop it; JPEG, BMP and Targa store 8 bits.
    """
    a = np.asarray(image)
    if a.dtype == np.bool_:
        a = a.astype(np.uint8) * 255
    if a.dtype not in (np.uint8, np.uint16):
        raise TypeError(f"image dtype must be uint8 or uint16, not {a.dtype}")
    if a.ndim == 2:
        h, w, c = a.shape[0], a.shape[1], 1
    elif a.ndim == 3 and 1 <= a.shape[2] <= 4:
        h, w, c = a.shape
    else:
        raise ValueError(f"image shape must be (h, w) or (h, w, 1..4), not {a.shape}")
    a = np.ascontiguousarray(a)
    return _vio.encode(a, w, h, c, a.itemsize * 8, format.lower(), quality=quality,
                       subsampling=subsampling, progressive=progressive, compression=compression)


def write(path: PathLike, image, format: Optional[str] = None, **options) -> None:
    """Writes an image file, in the format of the path's extension unless
    `format` is given; options as for encode()."""
    data = encode(image, format or _format_from_path(path), **options)
    with open(path, "wb") as f:
        f.write(data)


def info(source: Union[PathLike, BytesLike]) -> Info:
    """The format and dimensions of an image file (a path or its bytes),
    reading only the header where the format allows."""
    if isinstance(source, (bytes, bytearray, memoryview)):
        return Info(*_vio.probe(source))
    with open(source, "rb") as f:
        return Info(*_vio.probe(f.read()))


def detect(data: BytesLike) -> Optional[str]:
    """The format of an image file from its first bytes, or None."""
    name = _vio.detect(data)
    return None if name == "unknown" else name
