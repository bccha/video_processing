# Linux Frame Loader and Video Streamer

This directory contains utility programs to load images and stream videos directly from the Linux user-space to the FPGA physical memory (SDRAM), which is then picked up by the Video DMA and displayed via HDMI.

## 1. Frame Loader (Single Image)

Loads a static, raw RGB image into the framebuffer.

### Compilation
```bash
gcc frame_loader.c -o frame_loader -O2
```

### Usage
Generate a raw image (e.g., using ffmpeg on PC):
```bash
ffmpeg -i my_image.jpg -s 1280x720 -pix_fmt bgra -f rawvideo my_image.raw
```

Load it to the FPGA memory (0x20000000 by default):
```bash
sudo ./frame_loader my_image.raw
```

---

## 2. Stream Player (Video Pipeline via FFmpeg)

Takes a continuous stream of raw RGB frames from standard input (`stdin`) and writes them directly to the framebuffer memory for real-time video playback. Wait! We already have an advanced version of this built into `linux_software/video_player/video_player.c`. 

### Compilation
```bash
cd ../video_player
gcc video_player.c -o video_player -O2
```

### Usage (FFmpeg Pipe)
You can use `ffmpeg` to decode any video file and pipe the raw RGB frames directly to the `video_player` by passing `-` as the target file.

```bash
ffmpeg -i video.mp4 -vf scale=960:540 -pix_fmt bgra -f rawvideo - | sudo ./video_player - 60
```

**Command Breakdown:**
*   `-i video.mp4`: The input video file to play.
*   `-vf scale=960:540`: Resizes the video to match the 540p FPGA DMA resolution.
*   `-pix_fmt bgra`: Converts the color space to 32-bit ARGB (the format expected by the RTL logic).
*   `-f rawvideo -`: Outputs the raw, uncompressed pixel data to standard output (the pipe).
*   `| sudo ./video_player - 60`: `video_player` reads the piped pixel data `-` and continuously writes it to the SDRAM physical via `/dev/mem` at a target rate of 60 FPS.

**Advanced Controls:** The `video_player` supports interactive keyboard controls (1-9, d, m, s) to toggle RTL image filters and color matrices on the fly while the video is playing!
