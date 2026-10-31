import Cocoa
import FlutterMacOS

// A narrow native hit area keeps dragging reliable without stealing Flutter controls.
private final class TitlebarDragView: NSView {
  override var mouseDownCanMoveWindow: Bool { true }

  override func mouseDown(with event: NSEvent) {
    window?.performDrag(with: event)
  }
}

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame

    // Retain a semantic title for accessibility and the Window menu, but draw no title bar.
    title = "SECL"
    titleVisibility = .hidden
    titlebarAppearsTransparent = true
    titlebarSeparatorStyle = .none
    styleMask.insert(.fullSizeContentView)
    contentViewController = flutterViewController
    setFrame(NSRect(x: windowFrame.minX, y: windowFrame.minY, width: 1080, height: 720), display: true)
    minSize = NSSize(width: 880, height: 640)
    center()

    if let content = contentView {
      let dragArea = TitlebarDragView()
      dragArea.translatesAutoresizingMaskIntoConstraints = false
      content.addSubview(dragArea)
      NSLayoutConstraint.activate([
        dragArea.topAnchor.constraint(equalTo: content.topAnchor),
        dragArea.leadingAnchor.constraint(equalTo: content.leadingAnchor, constant: 84),
        dragArea.trailingAnchor.constraint(equalTo: content.trailingAnchor),
        dragArea.heightAnchor.constraint(equalToConstant: 30),
      ])
    }

    RegisterGeneratedPlugins(registry: flutterViewController)
    super.awakeFromNib()
  }
}
