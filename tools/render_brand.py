"""Rasterize RETSAR's small SVG symbol into application icon assets.

Development-only helper: Python + Pillow. The original SVG remains canonical.
Supports exactly the rect and line-path vocabulary used by retsar-mark.svg;
unknown geometry is rejected instead of silently approximated.
"""
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from PIL import Image, ImageDraw, ImageChops

ROOT = Path(__file__).resolve().parents[1]
SCALE = 4
SIZE = 256 * SCALE
NS = '{http://www.w3.org/2000/svg}'

def rectangle(element, color, width=None):
    image = Image.new('RGBA', (SIZE, SIZE))
    x,y,w,h,r = (float(element.get(k, '0')) * SCALE for k in ('x','y','width','height','rx'))
    draw = ImageDraw.Draw(image)
    if width is None:
        draw.rounded_rectangle((x,y,x+w,y+h), r, fill=color)
    else:
        half = width * SCALE / 2
        draw.rounded_rectangle((x-half,y-half,x+w+half,y+h+half),r+half,fill=color)
        draw.rounded_rectangle((x+half,y+half,x+w-half,y+h-half),max(0,r-half),fill=(0,0,0,0))
    return image

def path(element, color, width):
    data = element.get('d', '')
    if re.sub(r'[MmHhVvLl0-9+.,\s-]', '', data):
        raise ValueError('Unsupported SVG path command')
    tokens = re.findall(r'[MmHhVvLl]|[-+]?\d+(?:\.\d+)?', data)
    image = Image.new('RGBA', (SIZE, SIZE)); draw = ImageDraw.Draw(image)
    index = 0; x = y = 0.0; half = width*SCALE/2
    while index < len(tokens):
        command = tokens[index]; index += 1
        if command not in 'MmHhVvLl': raise ValueError('Explicit path command required')
        first = float(tokens[index]); index += 1
        nx,ny = x,y
        if command in 'MmLl':
            second = float(tokens[index]); index += 1
            nx = first + (x if command.islower() else 0)
            ny = second + (y if command.islower() else 0)
        elif command in 'Hh': nx = first + (x if command.islower() else 0)
        else: ny = first + (y if command.islower() else 0)
        if command not in 'Mm':
            draw.line((x*SCALE,y*SCALE,nx*SCALE,ny*SCALE),fill=color,width=round(width*SCALE))
            for px,py in ((x*SCALE,y*SCALE),(nx*SCALE,ny*SCALE)):
                draw.ellipse((px-half,py-half,px+half,py+half),fill=color)
        x,y = nx,ny
    return image

def main():
    svg = ET.parse(ROOT/'assets/retsar-mark.svg').getroot()
    if svg.get('viewBox') != '0 0 256 256': raise ValueError('Unexpected canonical viewBox')
    output = Image.new('RGBA', (SIZE,SIZE))
    for group in svg.findall(NS+'g'):
        layer = Image.new('RGBA',(SIZE,SIZE)); color = group.attrib['stroke']; width = float(group.attrib['stroke-width'])
        for element in group:
            if element.tag == NS+'rect': shape = rectangle(element,color,width)
            elif element.tag == NS+'path': shape = path(element,color,width)
            else: raise ValueError('Unsupported SVG shape')
            layer.alpha_composite(shape)
        if 'mask' in group.attrib:
            if group.attrib['mask'] != 'url(#separation)': raise ValueError('Unknown mask')
            mask = Image.new('L',(SIZE,SIZE),0)
            for element in svg.find(NS+'defs').find(NS+'mask'):
                if element.tag != NS+'rect': raise ValueError('Unsupported mask')
                shape=rectangle(element,'white')
                mask.paste(255 if element.get('fill') == 'white' else 0,mask=shape.getchannel('A'))
            layer.putalpha(ImageChops.multiply(layer.getchannel('A'),mask))
        output.alpha_composite(layer)
    output=output.resize((256,256),Image.Resampling.LANCZOS)
    output.save(ROOT/'assets/retsar-icon.png')
    output.save(ROOT/'assets/retsar.ico',sizes=[(s,s) for s in (16,24,32,48,64,128,256)])
    print('Rendered canonical SVG to PNG and seven-size Windows ICO.')

if __name__ == '__main__': main()