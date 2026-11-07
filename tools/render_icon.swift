// Compatibility entry point. The canonical artwork is assets/branding/secl.svg.
// Run from the repository root: swift tools/render_icon.swift
// Requires Python 3, Pillow, and Swift/AppKit on macOS or CairoSVG elsewhere.
import Foundation

let script = URL(fileURLWithPath: #filePath)
    .deletingLastPathComponent().appendingPathComponent("render_icon.py")
let process = Process()
process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
process.arguments = ["python3", script.path] + CommandLine.arguments.dropFirst()
try process.run()
process.waitUntilExit()
exit(process.terminationStatus)
