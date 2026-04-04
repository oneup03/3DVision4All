## Windows Monitor Driver EDID Override
- This overrides the display EDID at the Windows driver level. It's a bit more complicated, but might be preferred for real interlaced displays
- <a href="https://www.tenforums.com/tutorials/156602-how-enable-disable-driver-signature-enforcement-windows-10-a.html" target="_blank" rel="noopener noreferrer">Disable Driver Signature Enforcement in Windows</a> - this requires a special reboot
- Plug in your EDID emulator/dummy plug/secondary monitor that you're going to override if it's not already plugged in
- Download either the [4k EDID Override](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/4kNvidiaEDID.inf) or [1080p EDID Override]() depending on your display
- Open Device Manager as admin
  - Find your device under the `Monitors` section and `Right-Click` it and select `Update Driver`
  - Select `Browse my computer for drivers`
  - Select `Let me pick from a list of available drivers on my computer`
  - Select `Have Disk`
  - Select `Browse` and find the `4kNvidiaEDID.inf` or `1080pNvidiaEDID.inf` that you saved earlier
  - Choose the first `Acer HR274H (ACR02B9 EDID Override) Version: 1.0.0.0 [11/23/2017]` entry and click Next
  - Close out of the window and you should see something similar to this:
  
    <img width="260" height="155" alt="image" src="https://github.com/user-attachments/assets/e0cb5c8b-c5c3-407e-a0d4-92e70c2323c9" />
- Interlaced display hardware requires you to output games at the display native resolutions (4k or 1080p), but using ShaderGlass for non-interlaced hardware with the above instructions isn't limited to specific resolutions