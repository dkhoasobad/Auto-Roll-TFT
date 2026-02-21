#!/bin/bash

EXE_NAME="TFT_Bot.exe"
OUTPUT_DIR="TFT_Bot_Release"
ICON_FILE="icon.ico"
RC_FILE="resource.rc"
RES_OBJ="resource.o"

echo "[1/4] Compiling..."

if [ -f "$ICON_FILE" ]; then
    if [ ! -f "$RC_FILE" ]; then
        echo "1 ICON \"$ICON_FILE\"" > $RC_FILE
    fi
    echo "   -> Compiling Icon Resource..."
    windres $RC_FILE -o $RES_OBJ
    LINK_RES="$RES_OBJ"
else
    echo "   Icon file not found, building without icon."
    LINK_RES=""
fi

clang++ main.cpp \
  imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp imgui/imgui_demo.cpp \
  imgui/backends/imgui_impl_win32.cpp imgui/backends/imgui_impl_dx11.cpp \
  $LINK_RES \
  -o $EXE_NAME \
  -std=c++17 \
  -mwindows \
  -Wno-macro-redefined \
  -Wno-dangling-else \
  -Wno-return-type \
  -Wno-format-security \
  -Wno-unused-variable \
  -Wno-ignored-attributes \
  -Iimgui -Iimgui/backends \
  $(pkg-config --cflags --libs tesseract lept) \
  -ld3d11 -ld3dcompiler -lgdi32 -luser32 -lcomctl32 -ldwmapi -lole32

if [ $? -ne 0 ]; then
    echo "Build FAILED!"
    exit 1
fi

echo "Build OK!"
echo "[2/4] Creating output directory '$OUTPUT_DIR'..."
if [ -d "$OUTPUT_DIR" ]; then
    rm -rf "$OUTPUT_DIR"
fi
mkdir "$OUTPUT_DIR"
echo "[3/4] Moving executable..."
mv $EXE_NAME "$OUTPUT_DIR/"
echo "[4/4] Bundling DLLs..."
ldd "$OUTPUT_DIR/$EXE_NAME" | grep "/clang64/bin" | awk '{print $3}' | xargs -I {} cp -u {} "$OUTPUT_DIR/"

# Copy Tesseract data
if [ -d "tessdata" ]; then
    cp -r "tessdata" "$OUTPUT_DIR/"
    echo "   -> tessdata copied"
else
    echo "   WARNING: 'tessdata' folder not found!"
    echo "   -> You must copy it into '$OUTPUT_DIR' manually, or OCR will fail."
fi

echo ""
echo "Done! Output: $OUTPUT_DIR/"
echo "You can zip '$OUTPUT_DIR' and distribute it."
