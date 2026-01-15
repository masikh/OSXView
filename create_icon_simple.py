#!/usr/bin/env python3
import sys
import os

def create_simple_icon():
    # Create a simple SVG icon that we can convert
    svg_content = '''<?xml version="1.0" encoding="UTF-8"?>
<svg width="512" height="512" viewBox="0 0 512 512" xmlns="http://www.w3.org/2000/svg">
  <!-- Background with gradient -->
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" style="stop-color:#2a2a3a;stop-opacity:1" />
      <stop offset="100%" style="stop-color:#1a1a2a;stop-opacity:1" />
    </linearGradient>
  </defs>
  
  <!-- Background -->
  <rect width="512" height="512" fill="url(#bg)" rx="64" ry="64"/>
  
  <!-- System monitoring bars -->
  <!-- CPU bars -->
  <rect x="60" y="352" width="40" height="100" fill="#ff6464" rx="4"/>
  <rect x="110" y="372" width="40" height="80" fill="#ffa864" rx="4"/>
  <rect x="160" y="332" width="40" height="120" fill="#646464" rx="4"/>
  
  <!-- Memory bar -->
  <rect x="210" y="312" width="40" height="140" fill="#64c864" rx="4"/>
  
  <!-- Network bars -->
  <rect x="260" y="342" width="40" height="110" fill="#6496ff" rx="4"/>
  <rect x="310" y="362" width="40" height="90" fill="#96c8ff" rx="4"/>
  
  <!-- Disk bar -->
  <rect x="360" y="322" width="40" height="130" fill="#c864ff" rx="4"/>
  
  <!-- OSX text -->
  <text x="256" y="120" font-family="Arial, sans-serif" font-size="72" font-weight="bold" text-anchor="middle" fill="#ffffff">OSX</text>
  <text x="256" y="180" font-family="Arial, sans-serif" font-size="36" text-anchor="middle" fill="#a0a0a0">View</text>
  
  <!-- Grid overlay for tech look -->
  <g stroke="#ffffff" stroke-width="0.5" opacity="0.1">
    <line x1="0" y1="128" x2="512" y2="128"/>
    <line x1="0" y1="256" x2="512" y2="256"/>
    <line x1="0" y1="384" x2="512" y2="384"/>
    <line x1="128" y1="0" x2="128" y2="512"/>
    <line x1="256" y1="0" x2="256" y2="512"/>
    <line x1="384" y1="0" x2="384" y2="512"/>
  </g>
</svg>'''
    
    # Save SVG
    svg_path = "OSXView.app/Contents/Resources/AppIcon.svg"
    os.makedirs(os.path.dirname(svg_path), exist_ok=True)
    with open(svg_path, 'w') as f:
        f.write(svg_content)
    print(f"SVG icon created at: {svg_path}")
    
    # Try to convert to PNG using system tools
    png_path = "OSXView.app/Contents/Resources/AppIcon.png"
    
    # Use sips (built into macOS) to convert SVG to PNG
    import subprocess
    try:
        result = subprocess.run(['sips', '-s', 'format', 'png', svg_path, '--out', png_path], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print(f"PNG icon created at: {png_path}")
            return png_path
        else:
            print(f"sips failed: {result.stderr}")
    except FileNotFoundError:
        print("sips not available")
    
    # Fallback: create a simple script to use iconutil if available
    iconset_path = "OSXView.app/Contents/Resources/AppIcon.iconset"
    os.makedirs(iconset_path, exist_ok=True)
    
    # Try using iconutil (built into macOS)
    try:
        result = subprocess.run(['iconutil', '-c', 'iconset', '-i', svg_path, iconset_path], 
                              capture_output=True, text=True)
        if result.returncode == 0:
            print(f"Iconset created at: {iconset_path}")
            return iconset_path
        else:
            print(f"iconutil failed: {result.stderr}")
    except FileNotFoundError:
        print("iconutil not available")
    
    return svg_path

if __name__ == "__main__":
    create_simple_icon()
