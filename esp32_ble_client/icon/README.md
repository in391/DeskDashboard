# Icon Assets

Icons in this folder are derived from Microsoft Fluent UI System Icons:
https://github.com/microsoft/fluentui-system-icons/tree/main

License information is available in this folder:
- LICENSE
- NOTICE

## Conversion Script

The following Python script was used to convert SVG icon assets into 1-bit C header data:

```python
from pathlib import Path
import cairosvg
from PIL import Image

root = Path('/Users/inho/Developer/HomeDisplay/esp32_ble_client')
svg_path = root / 'icon' / 'ic_fluent_weather_sunny_24_regular.svg'
out_header = root / 'src' / 'ic_fluent_weather_sunny_50x50.h'

png_bytes = cairosvg.svg2png(url=str(svg_path), output_width=50, output_height=50)
img = Image.open(__import__('io').BytesIO(png_bytes)).convert('RGBA')

# Composite on white background, then threshold to 1-bit.
bg = Image.new('RGBA', img.size, (255, 255, 255, 255))
bg.alpha_composite(img)
gray = bg.convert('L')
# Values below 200 become black pixels.
mono = gray.point(lambda p: 0 if p < 200 else 255, mode='1')

w, h = mono.size
row_bytes = (w + 7) // 8
pixels = mono.load()

# bit=1 means black pixel; bit=0 means white pixel.
data = []
for y in range(h):
    for xb in range(row_bytes):
        b = 0
        for bit in range(8):
            x = xb * 8 + bit
            if x >= w:
                continue
            is_black = pixels[x, y] == 0
            if is_black:
                b |= 1 << (7 - bit)
        data.append(b)

lines = []
for i in range(0, len(data), 12):
    chunk = data[i:i+12]
    lines.append('  ' + ', '.join(f'0x{v:02X}' for v in chunk) + ',')

content = '\n'.join([
    '#pragma once',
    '#include <Arduino.h>',
    '',
    'static const int IC_FLUENT_WEATHER_SUNNY_50X50_WIDTH = 50;',
    'static const int IC_FLUENT_WEATHER_SUNNY_50X50_HEIGHT = 50;',
    'static const int IC_FLUENT_WEATHER_SUNNY_50X50_ROW_BYTES = 7;',
    '',
    'static const uint8_t IC_FLUENT_WEATHER_SUNNY_50X50_BITS[] PROGMEM = {',
    *lines,
    '};',
    ''
])

out_header.write_text(content)
print(f'Wrote {out_header} with {len(data)} bytes')
```
