@echo off
REM Clean previous builds
if exist dist rmdir /s /q dist
if exist build rmdir /s /q build
if exist run_neurosync.spec del /f /q run_neurosync.spec

REM Build the executable
pyinstaller --onedir --noconsole --clean --add-data "livelink/animations/default_anim/default.csv;default_anim" --add-data "livelink/animations/Angry;Angry" --add-data "livelink/animations/Disgusted;Disgusted" --add-data "livelink/animations/Fearful;Fearful" --add-data "livelink/animations/Happy;Happy" --add-data "livelink/animations/Neutral;Neutral" --add-data "livelink/animations/Sad;Sad" --add-data "livelink/animations/Surprised;Surprised" run_neurosync.py

pause