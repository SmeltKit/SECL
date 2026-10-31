// Original code-native Solar icon. Run from the repository root on macOS.
import AppKit
let root = URL(fileURLWithPath: FileManager.default.currentDirectoryPath)
let target = root.appendingPathComponent("app/macos/Runner/Assets.xcassets/AppIcon.appiconset")
let windows = root.appendingPathComponent("assets/branding/windows")
try FileManager.default.createDirectory(at: windows, withIntermediateDirectories: true)
for size in [16, 32, 48, 64, 128, 256, 512, 1024] {
  for framed in [true, false] {
    if framed && size == 48 { continue }
    let rep = NSBitmapImageRep(bitmapDataPlanes:nil,pixelsWide:size,pixelsHigh:size,bitsPerSample:8,samplesPerPixel:4,hasAlpha:true,isPlanar:false,colorSpaceName:.deviceRGB,bytesPerRow:0,bitsPerPixel:0)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep:rep)
    let c = NSGraphicsContext.current!.cgContext
    c.scaleBy(x:CGFloat(size)/256,y:CGFloat(size)/256)
    if framed { c.setFillColor(NSColor.white.cgColor); c.addPath(CGPath(roundedRect:CGRect(x:12,y:12,width:232,height:232),cornerWidth:49,cornerHeight:49,transform:nil)); c.fillPath(); c.translateBy(x:34,y:34);c.scaleBy(x:0.735,y:0.735) }
    c.setFillColor(NSColor(calibratedRed:0.96,green:0.76,blue:0.45,alpha:1).cgColor)
    c.fillEllipse(in:CGRect(x:85,y:85,width:86,height:86))
    c.setStrokeColor(NSColor(calibratedRed:0.21,green:0.37,blue:0.49,alpha:1).cgColor)
    c.setLineWidth(13);c.setLineCap(.round)
    for index in 0..<8 { let a=CGFloat(index) * .pi / 4; c.move(to:CGPoint(x:128+81*cos(a),y:128+81*sin(a)));c.addLine(to:CGPoint(x:128+104*cos(a),y:128+104*sin(a)));c.strokePath() }
    NSGraphicsContext.restoreGraphicsState()
    let data=rep.representation(using:.png,properties:[:])!
    let file=framed ? target.appendingPathComponent("app_icon_\(size).png") : windows.appendingPathComponent("solar-\(size).png")
    try data.write(to:file)
  }
}
