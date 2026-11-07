#!/usr/bin/env python3
"""Regenerate SECL platform icons from the canonical vector mark.

Run from any directory: python3 tools/render_icon.py
Requires Pillow and either CairoSVG (portable) or Swift/AppKit (macOS).
Normal application builds use the committed exports and need neither renderer.
"""

import argparse
import copy
import platform
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    raise SystemExit("Icon generation requires Pillow: python3 -m pip install Pillow")

SVG_NAMESPACE = "http://www.w3.org/2000/svg"
WINDOWS_SIZES = (16, 32, 48, 64, 128, 256, 512, 1024)
MACOS_SIZES = (16, 32, 64, 128, 256, 512, 1024)
ICO_SIZES = (16, 32, 48, 64, 128, 256)
ET.register_namespace("", SVG_NAMESPACE)

APPKIT_RENDERER = r'''
import AppKit

let arguments = Array(CommandLine.arguments.dropFirst())
guard arguments.count % 3 == 0 else {
    fatalError("Expected input.svg output.png size triples")
}
var images: [String: NSImage] = [:]
for offset in stride(from: 0, to: arguments.count, by: 3) {
    let input = arguments[offset]
    let output = arguments[offset + 1]
    guard let size = Int(arguments[offset + 2]), size > 0 else {
        fatalError("Invalid output size")
    }
    if images[input] == nil {
        guard let image = NSImage(contentsOfFile: input) else {
            fatalError("Cannot load SVG: \(input)")
        }
        images[input] = image
    }
    let bitmap = NSBitmapImageRep(
        bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
        isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: 0, bitsPerPixel: 0
    )!
    let context = NSGraphicsContext(bitmapImageRep: bitmap)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = context
    context.imageInterpolation = .high
    context.shouldAntialias = true
    let bounds = CGRect(x: 0, y: 0, width: size, height: size)
    context.cgContext.clear(bounds)
    images[input]!.draw(
        in: bounds, from: .zero, operation: .copy, fraction: 1,
        respectFlipped: true, hints: nil
    )
    NSGraphicsContext.restoreGraphicsState()
    guard let data = bitmap.representation(using: .png, properties: [:]) else {
        fatalError("Cannot encode PNG: \(output)")
    }
    try data.write(to: URL(fileURLWithPath: output))
}
'''


def make_macos_svg(source, destination):
    """Add the platform tile around a copy of the canonical SVG element."""
    mark = ET.parse(source).getroot()
    if mark.tag != "{%s}svg" % SVG_NAMESPACE:
        raise ValueError("The source must be a namespaced SVG document")
    if mark.get("viewBox") != "0 0 512 512":
        raise ValueError("The canonical icon must use viewBox='0 0 512 512'")
    wrapper = ET.Element("{%s}svg" % SVG_NAMESPACE, {
        "width": "512", "height": "512", "viewBox": "0 0 512 512",
    })
    ET.SubElement(wrapper, "{%s}rect" % SVG_NAMESPACE, {
        "x": "24", "y": "24", "width": "464", "height": "464",
        "rx": "104", "fill": "#ffffff",
    })
    ET.SubElement(wrapper, "{%s}rect" % SVG_NAMESPACE, {
        "x": "24.5", "y": "24.5", "width": "463", "height": "463",
        "rx": "103.5", "fill": "none", "stroke": "#dce2dc",
        "stroke-opacity": "0.7", "stroke-width": "1",
    })
    artwork = ET.SubElement(wrapper, "{%s}g" % SVG_NAMESPACE, {
        "transform": "translate(40 40) scale(.84)",
    })
    artwork.append(copy.deepcopy(mark))
    destination.parent.mkdir(parents=True, exist_ok=True)
    ET.indent(wrapper, space="  ")
    ET.ElementTree(wrapper).write(destination, encoding="utf-8", xml_declaration=True)


def render(jobs, renderer):
    if renderer in ("auto", "cairosvg"):
        try:
            import cairosvg
        except (ImportError, OSError):
            if renderer == "cairosvg":
                raise SystemExit("CairoSVG is unavailable: python3 -m pip install CairoSVG")
        else:
            for source, destination, size in jobs:
                cairosvg.svg2png(url=str(source), write_to=str(destination),
                                output_width=size, output_height=size)
            return "CairoSVG"
    if platform.system() != "Darwin" or shutil.which("swift") is None:
        raise SystemExit("Install CairoSVG for vector rendering: python3 -m pip install CairoSVG")
    with tempfile.TemporaryDirectory(prefix="secl-icon-render-") as temporary:
        temporary = Path(temporary)
        program = temporary / "render.swift"
        program.write_text(APPKIT_RENDERER, encoding="utf-8")
        arguments = ["swift", "-module-cache-path", str(temporary / "module-cache"), str(program)]
        for source, destination, size in jobs:
            arguments.extend((str(source), str(destination), str(size)))
        subprocess.run(arguments, check=True)
    return "Swift/AppKit"


def check_png(path, size):
    with Image.open(path) as image:
        if image.size != (size, size) or image.mode != "RGBA":
            raise ValueError("Expected a %dx%d RGBA PNG: %s" % (size, size, path))
        if image.getpixel((0, 0))[3] != 0 or image.getextrema()[3][1] != 255:
            raise ValueError("Expected transparent corners and an opaque mark: %s" % path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "assets/branding/secl.svg")
    parser.add_argument("--output-root", type=Path, default=Path(__file__).resolve().parents[1],
                        help="Write generated files under this directory (defaults to the repository)")
    parser.add_argument("--renderer", choices=("auto", "cairosvg", "appkit"), default="auto")
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    root = args.output_root.resolve()
    branding = root / "assets/branding"
    macos_source = branding / "secl-macos.svg"
    make_macos_svg(source, macos_source)
    windows = branding / "windows"
    macos = root / "app/macos/Runner/Assets.xcassets/AppIcon.appiconset"
    sidebar = root / "app/assets/branding/secl-128.png"
    ico = root / "app/windows/runner/resources/app_icon.ico"
    for folder in (windows, macos, sidebar.parent, ico.parent):
        folder.mkdir(parents=True, exist_ok=True)
    jobs = [(source, windows / ("secl-%d.png" % size), size) for size in WINDOWS_SIZES]
    jobs.extend((macos_source, macos / ("app_icon_%d.png" % size), size) for size in MACOS_SIZES)
    backend = render(jobs, args.renderer)
    for _, destination, size in jobs:
        check_png(destination, size)
    shutil.copyfile(windows / "secl-128.png", sidebar)
    images = []
    try:
        for size in ICO_SIZES:
            with Image.open(windows / ("secl-%d.png" % size)) as image:
                images.append(image.copy())
        images[-1].save(ico, format="ICO", sizes=[(size, size) for size in ICO_SIZES],
                        append_images=images[:-1])
    finally:
        for image in images:
            image.close()
    with Image.open(ico) as image:
        if image.ico.sizes() != {(size, size) for size in ICO_SIZES}:
            raise ValueError("ICO does not contain every required size")
    print("Generated macOS, Windows and sidebar icons from %s using %s." % (source.name, backend))


if __name__ == "__main__":
    main()
