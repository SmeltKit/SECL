import 'package:flutter/material.dart';

class AppAppearance extends ChangeNotifier with WidgetsBindingObserver {
  static final instance = AppAppearance();
  String mode = 'system', tone = 'blue';
  bool get dark =>
      mode == 'dark' ||
      mode == 'system' &&
          WidgetsBinding.instance.platformDispatcher.platformBrightness ==
              Brightness.dark;
  void select(String next, String color) {
    if (mode == next && tone == color) return;
    mode = next;
    tone = color;
    notifyListeners();
  }

  @override
  void didChangePlatformBrightness() {
    if (mode == 'system') notifyListeners();
  }
}

Color get panelSurface =>
    AppAppearance.instance.dark ? const Color(0xff202a36) : Colors.white;
Color seclColor(int value) {
  final c = Color(value);
  if (!AppAppearance.instance.dark) return c;
  if (c.red > 205 && c.green > 205 && c.blue > 205) {
    return Color.lerp(const Color(0xff202a36), c, .07)!;
  }
  if(c.green>c.red*1.2 && c.green>c.blue && c.green<170) return Color.lerp(c,const Color(0xffa9efcc),.55)!;
  return c;
}
