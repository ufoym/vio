# Tests of the Python package (bind/python/vio).  Run: pytest tests/python
import pathlib

import numpy as np
import pytest

import vio

IMAGES = pathlib.Path(__file__).resolve().parents[1] / "libjpeg-turbo" / "testimages"


def sample(channels, dtype):
    rng = np.random.default_rng(channels)
    hi = np.iinfo(dtype).max
    shape = (37, 53) if channels == 1 else (37, 53, channels)
    return rng.integers(0, hi + 1, size=shape, dtype=dtype)


@pytest.mark.parametrize("channels", [1, 2, 3, 4])
@pytest.mark.parametrize("dtype", [np.uint8, np.uint16])
@pytest.mark.parametrize("compression", [0, 6])
def test_png_roundtrip(channels, dtype, compression):
    img = sample(channels, dtype)
    data = vio.encode(img, "png", compression=compression)
    assert vio.detect(data) == "png"
    assert vio.info(data) == vio.Info("png", 53, 37, channels, img.itemsize * 8)
    out = vio.decode(data)
    assert out.dtype == dtype and np.array_equal(out, img)


@pytest.mark.parametrize("fmt", ["ppm", "bmp", "tga"])
@pytest.mark.parametrize("channels", [1, 3])
def test_lossless_roundtrip(fmt, channels):
    img = sample(channels, np.uint8)
    assert np.array_equal(vio.decode(vio.encode(img, fmt)), img)


def test_ppm_16bit():
    img = sample(3, np.uint16)
    out = vio.decode(vio.encode(img, "ppm"))
    assert out.dtype == np.uint16 and np.array_equal(out, img)


def test_alpha_dropped():
    img = sample(4, np.uint8)
    assert np.array_equal(vio.decode(vio.encode(img, "bmp")), img[:, :, :3])


def test_jpeg():
    img = vio.read(IMAGES / "testorig.jpg")
    assert img.shape == (149, 227, 3) and img.dtype == np.uint8
    for opts in [{}, {"quality": 50}, {"subsampling": 444, "progressive": True}]:
        out = vio.decode(vio.encode(img, "jpeg", **opts))
        assert out.shape == img.shape
        assert np.abs(out.astype(int) - img).mean() < 8
    assert len(vio.encode(img, "jpeg", quality=30)) < len(vio.encode(img, "jpeg", quality=95))


def test_layouts():
    rgb = vio.read(IMAGES / "testorig.jpg")
    rgba = vio.read(IMAGES / "testorig.jpg", channels=4)
    assert np.array_equal(rgba[:, :, :3], rgb) and (rgba[:, :, 3] == 255).all()
    assert vio.read(IMAGES / "testorig.ppm", channels=1).shape == (149, 227)
    wide = vio.read(IMAGES / "testorig.ppm", depth=np.uint16)
    assert np.array_equal(wide, vio.read(IMAGES / "testorig.ppm").astype(np.uint16) * 257)
    assert vio.read(IMAGES / "monkey12.jpg").dtype == np.uint16
    assert vio.read(IMAGES / "monkey16.png", depth=8).dtype == np.uint8


def test_files(tmp_path):
    img = sample(3, np.uint8)
    for name in ["a.png", "a.PPM", "a.bmp", "a.tga"]:
        vio.write(tmp_path / name, img)
        assert np.array_equal(vio.read(tmp_path / name), img)
    vio.write(tmp_path / "a.jpg", img, quality=95)
    assert vio.info(tmp_path / "a.jpg").format == "jpeg"
    vio.write(tmp_path / "noext", img, format="png")
    assert vio.info(tmp_path / "noext").format == "png"


def test_strided_input():
    view = sample(3, np.uint8)[::2, ::3]
    assert np.array_equal(vio.decode(vio.encode(view)), view)


def test_errors(tmp_path):
    with pytest.raises(vio.Error):
        vio.decode(b"not an image")
    with pytest.raises(vio.Error):
        vio.decode(b"\x89PNG\r\n\x1a\n\0\0\0\rIHDR")
    with pytest.raises(ValueError):
        vio.decode(b"", channels=5)
    with pytest.raises(TypeError):
        vio.encode(np.zeros((4, 4), np.float32))
    with pytest.raises(ValueError):
        vio.write(tmp_path / "a.xyz", sample(3, np.uint8))
    with pytest.raises(vio.Error):
        vio.encode(sample(3, np.uint8), "gif")
    with pytest.raises(FileNotFoundError):
        vio.read(tmp_path / "missing.png")
    assert vio.detect(b"hello") is None
