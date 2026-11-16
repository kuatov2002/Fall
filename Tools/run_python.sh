#!/bin/sh
# Runs an editor Python script headless: Tools/run_python.sh Tools/make_materials.py
export MSYS_NO_PATHCONV=1
"/c/Program Files/Epic Games/UE_5.9/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "C:/Users/chann/Fall/Fall.uproject" -run=pythonscript -script="C:/Users/chann/Fall/$1" -unattended -nosplash -AllowCommandletRendering 2>&1
