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

---

## 3. High-Performance Playback (HEVC / AV1 / 4K)

The ARM Cortex-A9 on the DE10-Nano is too slow to decode modern formats like H.265 (HEVC) or AV1. If you try to pipe these formats using FFmpeg on the board, playback will be extremely slow or fail entirely.

The solution is to **decode the video on your PC first**, and then play the completely uncompressed RAW file on the board.

### Step A: Decode on PC (Windows/WSL)
Use FFmpeg on your powerful PC to decode the heavy format into a pure RAW RGB32 file. Use `-frames:v` to limit the duration, otherwise the output file will be massive!

```bash
# Decode the first 600 frames (20 seconds @ 30fps) into a 960x540 RAW file
ffmpeg -i heavy_video_hevc.mp4 -frames:v 600 -s 960x540 -pix_fmt bgra -f rawvideo movie_decoded.raw
```

### Step B: Transfer to Board
Copy `movie_decoded.raw` to the DE10-Nano's SD card (via SSH, SCP, or directly plugging it in).

### Step C: Play directly on the Board (Zero CPU usage)
Run the `video_player` pointing directly to the loaded `.raw` file.

```bash
cd linux_software/video_player
sudo ./video_player /path/to/movie_decoded.raw 60
```
Because the file is already uncompressed pixels, the CPU just copies memory and the FPGA Video DMA handles the rest, resulting in silky smooth 60fps playback!

---

## 4. SSH Real-Time Pipeline (PC Decode -> Board Playback over Network)

If you don't want to save a massive `.raw` file on your SD card, you can use your powerful PC to decode the video in real-time and stream the raw pixels directly into the DE10-Nano's RAM over a Gigabit Ethernet SSH connection.

From your **PC (WSL/Linux/Mac) terminal**:

```bash
# Decode video on PC (ffmpeg) -> Stream RAW bytes over network (|) -> Play directly on board via SSH
ffmpeg -i heavy_video.mp4 -vf scale=960:540 -pix_fmt bgra -f rawvideo - | ssh root@<BOARD_IP_ADDRESS> "cd ~/test/linux_software/video_player && ./video_player - 60"
```

*   Replace `<BOARD_IP_ADDRESS>` with the IP of your DE10-Nano.
*   Ensure the path `~/test/linux_software/video_player` exactly matches where your `video_player` binary is located on the board.
*   A Gigabit Ethernet connection is heavily recommended, as raw 960x540@60fps pushes roughly 995 Mbps of network traffic.

---

## Appendix: Setting a Permanent Static IP on the DE10-Nano

To ensure you can always SSH into your board without checking the IP on a monitor every time, you should configure a static IP address.

1.  Open the network interfaces file on the board:
    ```bash
    nano /etc/network/interfaces
    ```
2.  Add or modify the `eth0` interface settings at the bottom of the file (replace the IP addresses to match your local router's subnet):
    ```text
    auto eth0
    iface eth0 inet static
        address 192.168.0.100  # Desired Static IP for the Board
        netmask 255.255.255.0
        gateway 192.168.0.1    # Your Router IP
    ```
    *(Note: If `iface eth0 inet dhcp` exists, comment it out with `#` or delete it).*
3.  Save the file (`Ctrl+O`, `Enter`) and exit (`Ctrl+X`).
4.  Restart the networking service or reboot the board to apply changes:
    ```bash
    /etc/init.d/networking restart
    # or
    reboot
    ```
