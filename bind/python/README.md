# vio for Python

Fast image decoding and encoding (JPEG, PNG, BMP, GIF, PPM, Targa) to and
from NumPy arrays, built on the [vio](https://github.com/ufoym/vio) C++
library.

```python
import vio

img = vio.read("photo.jpg")                  # (h, w, 3) uint8
vio.write("photo.png", img)                  # format from the extension
data = vio.encode(img, "jpeg", quality=85, subsampling=444, progressive=True)
rgba = vio.decode(data, channels=4)          # (h, w, 4) uint8
gray16 = vio.read("photo.png", channels=1, depth=16)  # (h, w) uint16
vio.info("photo.jpg")                        # Info(format='jpeg', width=..., ...)
```

Arrays are `(h, w)` for gray or `(h, w, c)` with 2 (gray + alpha), 3 (RGB) or
4 (RGBA) channels, `uint8` or `uint16`.  Errors raise `vio.Error`.
