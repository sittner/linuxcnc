import 'package:flutter/material.dart';
import 'scope_screen.dart';

class HalscopeApp extends StatelessWidget {
  const HalscopeApp({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'HAL Oscilloscope',
      theme: ThemeData(
        colorScheme: ColorScheme.fromSeed(
          seedColor: Colors.blueGrey,
          brightness: Brightness.dark,
        ),
        useMaterial3: true,
      ),
      home: const ScopeScreen(),
    );
  }
}
