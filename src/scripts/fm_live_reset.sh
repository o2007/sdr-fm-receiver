#!/usr/bin/env bash
set -euo pipefail

FREQ_HZ="${1:-95700000}"
ALSA_DEV="${2:-plughw:0,0}"
RTL_INDEX="${3:-0}"
GAIN_DB="${4:-auto}"   # numeric dB (e.g. 28.0) or "auto"
PPM="${5:-0}"
SDR_RATE="${6:-170k}"  # rtl_fm -s
AUDIO_RATE="${7:-48000}" # rtl_fm -r + aplay -r
ATAN_MODE="${8:-fast}" # fast|std|lut

rtl_cmd=(
  rtl_fm
  -d "${RTL_INDEX}"
  -f "${FREQ_HZ}"
  -M wbfm
  -s "${SDR_RATE}"
  -r "${AUDIO_RATE}"
  -A "${ATAN_MODE}"
  -F 9
  -E deemp
  -E dc
  -l 0
  -p "${PPM}"
)

if [[ "${GAIN_DB}" != "auto" ]]; then
  rtl_cmd+=( -g "${GAIN_DB}" )
fi

echo "Starting robust live chain:"
echo "  freq: ${FREQ_HZ} Hz"
echo "  alsa: ${ALSA_DEV}"
echo "  rtl index: ${RTL_INDEX}"
echo "  gain: ${GAIN_DB}"
echo "  ppm: ${PPM}"
echo "  sdr rate: ${SDR_RATE}"
echo "  audio rate: ${AUDIO_RATE}"
echo "  atan mode: ${ATAN_MODE}"
echo "Press Ctrl+C to stop."

"${rtl_cmd[@]}" \
  | aplay -q -D "${ALSA_DEV}" -r "${AUDIO_RATE}" -f S16_LE -c 1 -t raw
