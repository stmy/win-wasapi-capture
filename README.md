# obs-win-wasapi-capture

Plugin for the OBS-Studio to capture sound of specific application.

# Build

0. Clone [obs-studio repository](https://github.com/jp9000/obs-studio) first.
1. `git clone https://github.com/stmy/win-wasapi-capture.git` at the `obs-studio/plugins`.
2. Open `obs-studio/plugins/CMakeLists.txt`, then append a line `add_subdirectory(win-wasapi-capture)` just before the line `elseif(APPLE)`.
3. CMake obs-studio project (See <https://github.com/jp9000/obs-studio/wiki/Install-Instructions>).
4. Build generated solution.

# License

GPLv3