# ESP32-S3 and MAX98357
The code shows how to play music using ESP32-S3 and MAX98357 in IDF v5.0.
# Wiring
```c
ESP32S3_GPIO_1      MAX98357_BCLK
ESP32S3_GPIO_2      MAX98357_DIN
ESP32S3_GPIO_3      MAX98357_LRC
```
# music file convert
You can your own music file to o.pcm by this line.
```bash
ffmpeg -i music.mp3 -f u8 -ar 44100 -ac 1 o.pcm
```