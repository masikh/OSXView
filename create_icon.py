#!/usr/bin/env python3
import sys
from PIL import Image, ImageDraw
import os

def create_icon():
    # Create a 512x512 icon (macOS standard size)
    size = 512
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Background - dark blue gradient effect
    for i in range(size):
        color_value = int(40 + (60 * i / size))  # Dark to medium blue
        draw.line([(0, i), (size, i)], fill=(color_value, color_value, color_value + 20, 255))
    
    # Add some system monitoring visual elements
    margin = 60
    bar_width = 40
    bar_spacing = 50
    bar_height = 200
    
    # CPU bars (multi-colored)
    colors = [
        (255, 100, 100),  # Red - user
        (255, 200, 100),  # Orange - system  
        (100, 100, 100),  # Gray - idle
    ]
    
    for i, color in enumerate(colors):
        x = margin + i * bar_spacing
        y = size - margin - bar_height
        height = int(bar_height * (0.3 + i * 0.2))  # Varying heights
        draw.rectangle([x, y + bar_height - height, x + bar_width, y + bar_height], 
                      fill=color)
    
    # Memory bar (green)
    mem_x = margin + 3 * bar_spacing
    mem_height = int(bar_height * 0.6)
    draw.rectangle([mem_x, size - margin - bar_height, mem_x + bar_width, size - margin - bar_height + mem_height],
                  fill=(100, 200, 100))
    
    # Network bars (blue shades)
    net_colors = [(100, 150, 255), (150, 200, 255)]
    for i, color in enumerate(net_colors):
        x = margin + (4 + i) * bar_spacing
        height = int(bar_height * (0.4 + i * 0.1))
        draw.rectangle([x, size - margin - bar_height, x + bar_width, size - margin - bar_height + height],
                      fill=color)
    
    # Disk bar (purple)
    disk_x = margin + 6 * bar_spacing
    disk_height = int(bar_height * 0.5)
    draw.rectangle([disk_x, size - margin - bar_height, disk_x + bar_width, size - margin - bar_height + disk_height],
                  fill=(200, 100, 255))
    
    # Add "OSX" text
    try:
        # Try to use a system font
        font_size = 80
        font = None
        try:
            font = Image.load_path_font("/System/Library/Fonts/Arial.ttf")
            font = font.font_variant(size=font_size)
        except:
            pass
        
        if font:
            text = "OSX"
            bbox = draw.textbbox((0, 0), text, font=font)
            text_width = bbox[2] - bbox[0]
            text_height = bbox[3] - bbox[1]
            text_x = (size - text_width) // 2
            text_y = margin // 2
            draw.text((text_x, text_y), text, fill=(255, 255, 255), font=font)
    except:
        # If font loading fails, just skip the text
        pass
    
    return img

def main():
    icon_path = "/Users/robert/programming/C++/Xosview/OSXView.app/Contents/Resources/AppIcon.png"
    
    # Create the icon
    icon = create_icon()
    icon.save(icon_path, "PNG")
    print(f"Icon created at: {icon_path}")
    
    # Also create iconset for better macOS integration
    iconset_path = "/Users/robert/programming/C++/Xosview/OSXView.app/Contents/Resources/AppIcon.iconset"
    os.makedirs(iconset_path, exist_ok=True)
    
    # Generate multiple sizes for iconset
    sizes = [16, 32, 64, 128, 256, 512]
    for size in sizes:
        resized = icon.resize((size, size), Image.Resampling.LANCZOS)
        resized.save(f"{iconset_path}/icon_{size}x{size}.png")
        if size <= 256:
            # Also create @2x versions
            resized_2x = icon.resize((size*2, size*2), Image.Resampling.LANCZOS)
            resized_2x.save(f"{iconset_path}/icon_{size}x{size}@2x.png")
    
    print(f"Iconset created at: {iconset_path}")

if __name__ == "__main__":
    main()
