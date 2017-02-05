# win-wasapi-capture

Plugin for the OBS-Studio to capture sound of specific application.

## How to use

1. Copy `data` and `obs-plugins` folder into your `obs-studio` directory.
2. Launch the OBS, and add `WASAPICapture` source in your scene.
3. Select application you want to capture the sound.

## How to build

0. Clone [obs-studio repository](https://github.com/jp9000/obs-studio) first.
1. clone this repository in `obs-studio/plugins`.
2. Open `obs-studio/plugins/CMakeLists.txt`, then append a line `add_subdirectory(win-wasapi-capture)` just before the line `elseif(APPLE)`.
3. Generate obs-studio solution with cmake (See <https://github.com/jp9000/obs-studio/wiki/Install-Instructions>).
4. Build with generated solution.

## License

GPLv3
