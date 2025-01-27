#!/usr/bin/env bash
set -euo pipefail

declare -r REQUIRED_COMMANDS=("git" "svgexport" "mogrify" "convert" "pngcrush")
declare -r GIT_ROOT=$(git rev-parse --show-toplevel 2>/dev/null) || {
    echo "Error: This script must be run within a Git repository."
    exit 1
}
declare -r PIXMAPS_DIR="$GIT_ROOT/share/pixmaps"
declare -r RES_DIR="$GIT_ROOT/src/qt/res/icons"
declare -r SVG_DIR="$GIT_ROOT/src/qt/res/src"
declare -r TMP_DIR=$(mktemp -d -t patchcoin-images-XXXXXX)
trap "rm -rf $TMP_DIR" EXIT

check_commands() {
    for cmd in "${REQUIRED_COMMANDS[@]}"; do
        if ! command -v "$cmd" &>/dev/null; then
            echo "Error: '$cmd' command not found, please install it."
            exit 1
        fi
    done
}

optimize_png() {
    local png_file="$1"
    pngcrush -brute -ow -rem gAMA -rem cHRM -rem iCCP -rem sRGB \
             -rem alla -rem text "$png_file" &>/dev/null
}

convert_svg_once() {
    local svg_file="$1"
    local out_png="$2"
    local resolution="$3"
    svgexport "$svg_file" "$out_png" :"$resolution"
    mogrify -resize "${resolution}x${resolution}" -gravity center -background none \
            -extent "${resolution}x${resolution}" -strip "$out_png"
    optimize_png "$out_png"
}

add_image_border() {
    local input_image="$1"
    local output_image="$2"
    local resize_percent="$3"
    local border_size="$4"

    convert "$input_image" \
        -resize "$resize_percent" \
        -gravity center \
        -background none \
        -extent 1024x1024 \
        \( +clone -alpha extract -morphology dilate disk:"$border_size" -background black -alpha shape \) \
        +swap -compose over -composite \
        "$output_image"
}

generate_resized() {
    local base_image="$1"
    local out_dir="$2"
    local prefix="$3"
    local target_size

    for target_size in 128 256 32 64 16; do
        local png_file="$out_dir/${prefix}${target_size}.png"
        local xpm_file="$out_dir/${prefix}${target_size}.xpm"

        if [[ $target_size -eq 16 || $target_size -eq 32 ]]; then
            local bordered_image="$TMP_DIR/bordered_${target_size}.png"
            add_image_border "$base_image" "$bordered_image" 93% 30
            convert "$bordered_image" -filter Lanczos -resize "${target_size}x${target_size}" "$png_file"
        else
            convert "$base_image" -filter Lanczos -resize "${target_size}x${target_size}" "$png_file"
        fi

        optimize_png "$png_file"
        convert "$png_file" "$xpm_file"
    done
}

generate_tor_images() {
    local base_image="$1"
    local out_dir="$2"
    local prefix="$3"
    local sizes=(128 256 32)
    for size in "${sizes[@]}"; do
        local png_file="$out_dir/${prefix}${size}.png"
        convert "$base_image" -filter Lanczos -resize "${size}x${size}" \
            \( "$TOR_LOGO" -filter Lanczos -resize "${size}x${size}" \) \
            -gravity southeast -geometry +0+0 -composite \
            "$png_file"
        optimize_png "$png_file"
    done
}

generate_nsis_wizard_bmp() {
    local base_image="$1"
    local output_file="$2"
    local target_width=164
    local target_height=314
    local padding_top=0
    local padding_bottom=0
    local total_height=$((target_height + padding_top + padding_bottom))

    convert "$base_image" \
        -filter Lanczos \
        -resize "${target_width}x${target_height}^" \
        -gravity center \
        -crop "${target_width}x${target_height}+0+0" +repage \
        -background white \
        -gravity center \
        -extent "${target_width}x$total_height" \
        -colorspace RGB \
        -type TrueColor \
        -depth 24 \
        -define bmp:format=bmp3 \
        -compress none \
        "$output_file"
    echo "nsis-wizard.bmp generated: $output_file"
}

generate_nsis_header_bmp() {
    local base_image="$1"
    local output_file="$2"
    local total_width=150
    local total_height=57
    local padding_top=19
    local padding_right=7
    local padding_bottom=5
    local padding_left=10
    local inner_width=$((total_width - padding_left - padding_right))
    local inner_height=$((total_height - padding_top - padding_bottom))

    convert "$base_image" \
        -filter Lanczos \
        -resize "${inner_width}x${inner_height}^" \
        -gravity center \
        -crop "${inner_width}x${inner_height}+0+0" +repage \
        -background white \
        -gravity center \
        -extent "${total_width}x${total_height}" \
        -colorspace RGB \
        -type TrueColor \
        -depth 24 \
        -define bmp:format=bmp3 \
        -compress none \
        "$output_file"
    echo "nsis-header.bmp generated: $output_file"
}

generate_ico() {
    local input_image="$1"
    local output_icon="$2"
    local tmp_ico_dir
    tmp_ico_dir=$(mktemp -d -p "$TMP_DIR" ico-XXXXXX)

    declare -A SIZES=(
        [16x16]="4 8 32"
        [32x32]="4 8 32"
        [48x48]="4 8 32"
        [256x256]="32"
    )
    for size in "${!SIZES[@]}"; do
        for depth in ${SIZES[$size]}; do
            local output_file="$tmp_ico_dir/${size}_${depth}bpp.png"
            convert "$input_image" -resize "$size" -depth "$depth" "$output_file"
            optimize_png "$output_file"
        done
    done
    convert "$tmp_ico_dir"/*.png "$output_icon"
    echo "ICO file created: $output_icon"
    rm -rf "$tmp_ico_dir"
}

generate_logomask() {
    local svg_file="$SVG_DIR/patchcoin_white.svg"
    local out_png="$GIT_ROOT/src/qt/res/images/logomask.png"
    local temp_png="$TMP_DIR/logomask_temp.png"

    svgexport "$svg_file" "$temp_png" :465

    convert "$temp_png" \
        -gravity northeast \
        -background none \
        -extent 360x360 \
        -alpha set \
        -channel A \
        -evaluate multiply 0.05 \
        +channel \
        "$out_png"

    optimize_png "$out_png"
    echo "Generated $out_png (360x360, bottom-left partially out of frame, with 50% transparency)"
}

check_commands

declare -r LOGO_SVG="$SVG_DIR/patchcoin_full_white.svg"
declare -r LOGO_PNG="$GIT_ROOT/src/qt/res/images/logo.png"

mkdir -p "$(dirname "$LOGO_PNG")"

convert_svg_once "$LOGO_SVG" "$LOGO_PNG" 224x48
optimize_png "$LOGO_PNG"

echo "Generated $LOGO_PNG (224x48)"

mkdir -p "$PIXMAPS_DIR" "$RES_DIR"

declare -r PATCHCOIN_SVG="$SVG_DIR/patchcoin.svg"
declare -r PATCHCOIN_FULL_SVG="$SVG_DIR/patchcoin_full.svg"
declare -r PATCHCOIN_GRAYSCALE_SVG="$SVG_DIR/patchcoin_grayscale.svg"
declare -r PATCHCOIN_WHITE_SVG="$SVG_DIR/patchcoin_white.svg"
declare -r PATCHCOIN_PURPLE_SVG="$SVG_DIR/patchcoin_purple.svg"
declare -r TOR_SVG="$SVG_DIR/tor.svg"

declare -r BASE_PATCHCOIN="$TMP_DIR/patchcoin_1024.png"
convert_svg_once "$PATCHCOIN_SVG" "$BASE_PATCHCOIN" 1024

declare -r BASE_PATCHCOIN_WITH_BORDER="$TMP_DIR/patchcoin_1024_bordered.png"
add_image_border "$BASE_PATCHCOIN" "$BASE_PATCHCOIN_WITH_BORDER" 95% 30

declare -r BASE_PATCHCOIN_FULL="$TMP_DIR/patchcoin_full_1024.png"
convert_svg_once "$PATCHCOIN_FULL_SVG" "$BASE_PATCHCOIN_FULL" 1024

declare -r BASE_PATCHCOIN_PURPLE="$TMP_DIR/patchcoin_purple_1024.png"
convert_svg_once "$PATCHCOIN_PURPLE_SVG" "$BASE_PATCHCOIN_PURPLE" 1024

declare -r TOR_LOGO="$TMP_DIR/tor_512.png"
svgexport "$TOR_SVG" "$TOR_LOGO" :512
mogrify -trim +repage "$TOR_LOGO"

declare -r BASE_PATCHCOIN_GRAY="$TMP_DIR/patchcoin_gray_1024.png"
convert_svg_once "$PATCHCOIN_GRAYSCALE_SVG" "$BASE_PATCHCOIN_GRAY" 1024

declare -r BASE_PATCHCOIN_GRAY_WITH_BORDER="$TMP_DIR/patchcoin_gray_1024_bordered.png"
add_image_border "$BASE_PATCHCOIN_GRAY" "$BASE_PATCHCOIN_GRAY_WITH_BORDER" 95% 30

declare -r BASE_PATCHCOIN_WHITE="$TMP_DIR/patchcoin_white_1024.png"
convert_svg_once "$PATCHCOIN_WHITE_SVG" "$BASE_PATCHCOIN_WHITE" 1024

generate_resized "$BASE_PATCHCOIN" "$PIXMAPS_DIR" "patchcoin"
generate_resized "$BASE_PATCHCOIN_PURPLE" "$PIXMAPS_DIR" "patchcoin-testnet"

generate_tor_images "$BASE_PATCHCOIN_PURPLE" "$PIXMAPS_DIR" "patchcoin-testnet-tor"
generate_tor_images "$BASE_PATCHCOIN" "$PIXMAPS_DIR" "patchcoin-tor"

generate_nsis_wizard_bmp "$BASE_PATCHCOIN_WHITE" "$PIXMAPS_DIR/nsis-wizard.bmp"
generate_nsis_header_bmp "$BASE_PATCHCOIN_FULL" "$PIXMAPS_DIR/nsis-header.bmp"

generate_ico "$BASE_PATCHCOIN_WITH_BORDER" "$PIXMAPS_DIR/patchcoin.ico"

convert "$BASE_PATCHCOIN" -filter Lanczos -resize 841x841 "$RES_DIR/bitcoin.png"
optimize_png "$RES_DIR/bitcoin.png"
echo "Generated $RES_DIR/bitcoin.png (841x841)"

cp "$PIXMAPS_DIR/patchcoin.ico" "$RES_DIR/patchcoin.ico"
echo "Copied $RES_DIR/patchcoin.ico from $PIXMAPS_DIR/patchcoin.ico"

convert "$BASE_PATCHCOIN" -filter Lanczos -resize 512x512 "$RES_DIR/patchcoin.png"
optimize_png "$RES_DIR/patchcoin.png"
echo "Generated $RES_DIR/patchcoin.png (512x512)"

convert "$BASE_PATCHCOIN_GRAY" -filter Lanczos -resize 512x512 "$RES_DIR/patchcoin_testnet.png"
optimize_png "$RES_DIR/patchcoin_testnet.png"
echo "Generated $RES_DIR/patchcoin_testnet.png (512x512)"

generate_ico "$BASE_PATCHCOIN_GRAY_WITH_BORDER" "$RES_DIR/patchcoin_testnet.ico"
echo "Generated $RES_DIR/patchcoin_testnet.ico"

generate_logomask

echo
echo "All images have been generated:"
echo "  Pixmaps images: $PIXMAPS_DIR"
echo "  Res images: $RES_DIR"
echo "  logomask: $GIT_ROOT/src/qt/res/images/logomask.png"
echo "Temporary working directory: $TMP_DIR (will be removed upon script exit)"
