/// Ví dụ sử dụng bmc_video_windows plugin.
///
/// Chạy trong Flutter Windows app.
library;

import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:bmc_video_windows/bmc_video_windows.dart' as bmc_video;

void main() {
  runApp(const MaterialApp(home: CameraExamplePage()));
}

class CameraExamplePage extends StatefulWidget {
  const CameraExamplePage({super.key});

  @override
  State<CameraExamplePage> createState() => _CameraExamplePageState();
}

class _CameraExamplePageState extends State<CameraExamplePage> {
  List<bmc_video.BmcVideoDevice> _devices = [];
  bmc_video.BmcVideoDevice? _selectedDevice;
  bool _capturing = false;
  Uint8List? _frame;
  Timer? _timer;
  int _frameCount = 0;

  @override
  void initState() {
    super.initState();
    _loadDevices();
  }

  @override
  void dispose() {
    _stopCapture();
    bmc_video.disposeFrameBuffer();
    super.dispose();
  }

  void _loadDevices() {
    final devices = bmc_video.listCameraDevices();
    setState(() {
      _devices = devices;
      if (devices.isNotEmpty) _selectedDevice = devices.first;
    });
  }

  void _startCapture() {
    if (_selectedDevice == null) return;

    final result = bmc_video.startCapture(
      deviceIndex: _selectedDevice!.index,
      width: 640,
      height: 480,
      fps: 15,
      jpegQuality: 60,
    );

    if (result != 0) {
      debugPrint('startCapture failed: $result');
      return;
    }

    setState(() => _capturing = true);

    // Đợi camera warm-up rồi bắt đầu poll
    Future.delayed(const Duration(milliseconds: 800), () {
      _timer = Timer.periodic(const Duration(milliseconds: 66), (_) {
        final frame = bmc_video.getLatestJpegFrame();
        if (frame != null && mounted) {
          _frameCount++;
          setState(() => _frame = frame);
        }
      });
    });
  }

  void _stopCapture() {
    _timer?.cancel();
    _timer = null;
    if (_capturing) {
      bmc_video.stopCapture();
      setState(() {
        _capturing = false;
        _frame = null;
        _frameCount = 0;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Camera Example')),
      body: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          children: [
            // Device selector
            if (_devices.isNotEmpty)
              DropdownButton<bmc_video.BmcVideoDevice>(
                isExpanded: true,
                value: _selectedDevice,
                items: _devices
                    .map((d) => DropdownMenuItem(value: d, child: Text(d.name)))
                    .toList(),
                onChanged: _capturing ? null : (d) => setState(() => _selectedDevice = d),
              ),

            const SizedBox(height: 16),

            // Preview
            Expanded(
              child: Container(
                color: Colors.black,
                child: _frame != null
                    ? Image.memory(_frame!, gaplessPlayback: true, fit: BoxFit.contain)
                    : const Center(
                        child: Text('No preview', style: TextStyle(color: Colors.white54)),
                      ),
              ),
            ),

            const SizedBox(height: 8),

            // Info
            Text(
              _capturing
                  ? '${bmc_video.getFrameWidth()}x${bmc_video.getFrameHeight()} • Frame: $_frameCount'
                  : 'Stopped',
            ),

            const SizedBox(height: 16),

            // Controls
            Row(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                ElevatedButton(
                  onPressed: _capturing ? null : _startCapture,
                  child: const Text('Start'),
                ),
                const SizedBox(width: 16),
                ElevatedButton(
                  onPressed: _capturing ? _stopCapture : null,
                  child: const Text('Stop'),
                ),
              ],
            ),
          ],
        ),
      ),
    );
  }
}
