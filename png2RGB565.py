from PIL import Image
import numpy as np
import os

def convert_png_to_rgb565(input_file='lutino_cropped_brightlight.png', output_file='lutino_brightlight_rgb565.raw'):
    try:
        if not os.path.exists(input_file):
            print(f"Error: {input_file} not found!")
            return False
            
        img = Image.open(input_file)
        img_rgb = img.convert('RGB')
        pixels = np.array(img_rgb) # convert to a numpy array

        # Direct RGB565 conversion 
        r = (pixels[:,:,0] >> 3).astype(np.uint16)
        g = (pixels[:,:,1] >> 2).astype(np.uint16)
        b = (pixels[:,:,2] >> 3).astype(np.uint16)

        rgb565 = (r << 11) | (g << 5) | b

        # Save as raw 16-bit little-endian
        rgb565.astype('<u2').tofile(output_file)
        print(f"Successfully converted {input_file} to {output_file}")
        print(f"Image dimensions: {img.width} x {img.height}")
        return True
        
    except Exception as e:
        print(f"Conversion failed: {e}")
        return False
