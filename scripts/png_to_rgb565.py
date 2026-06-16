from PIL import Image
import sys

def convert_to_rgb565(img_path, output_path, target_width=None, target_height=None):
    img = Image.open(img_path).convert('RGB')
    
    if target_width and target_height:
        # Preserve aspect ratio
        original_width, original_height = img.size
        ratio = min(target_width / original_width, target_height / original_height)
        new_width = int(original_width * ratio)
        new_height = int(original_height * ratio)
        img = img.resize((new_width, new_height), Image.LANCZOS)
    
    width, height = img.size
    with open(output_path, 'w') as f:
        f.write('#ifndef BASA_H\n#define BASA_H\n\n')
        f.write('#include <Arduino.h>\n\n')
        f.write('const uint16_t basa_width = {};\n'.format(width))
        f.write('const uint16_t basa_height = {};\n'.format(height))
        f.write('const uint16_t basa_img[] PROGMEM = {\n')
        
        for y in range(height):
            for x in range(width):
                r, g, b = img.getpixel((x, y))
                # Convert to RGB565 (Big Endian as used by Adafruit_GFX)
                rgb565 = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write('0x{:04X}, '.format(rgb565))
            f.write('\n')
        
        f.write('};\n\n#endif // BASA_H\n')

if __name__ == "__main__":
    if len(sys.argv) == 3:
        convert_to_rgb565(sys.argv[1], sys.argv[2])
    elif len(sys.argv) == 5:
        convert_to_rgb565(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]))
    else:
        print("Usage: python png_to_rgb565.py <input.png> <output.h> [target_width target_height]")
