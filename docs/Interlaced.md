# Interlaced/Interleaved 3D
Compatible with devices like:
- LG/Sony 4k OLED/LCD 3D TVs
- LG/Acer/Zalman 3D Monitors

{% include 3DFM.md %}

## Windows Monitor Driver EDID Override
- This overrides the display EDID at the Windows driver level. It's a bit more complicated, but might be preferred for real interlaced displays
- <a href="https://www.tenforums.com/tutorials/156602-how-enable-disable-driver-signature-enforcement-windows-10-a.html" target="_blank" rel="noopener noreferrer">Disable Driver Signature Enforcement in Windows</a> - this requires a special reboot
- Download either the [4k EDID Override](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/4kNvidiaEDID.inf) or [1080p EDID Override](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/1080pNvidiaEDID.inf) depending on your display
- Open Device Manager as admin
  - Find your device under the `Monitors` section and `Right-Click` it and select `Update Driver`
  - Select `Browse my computer for drivers`
  - Select `Let me pick from a list of available drivers on my computer`
  - Select `Have Disk`
  - Select `Browse` and find the `4kNvidiaEDID.inf` or `1080pNvidiaEDID.inf` that you saved earlier
  - Choose the first `Acer HR274H (ACR02B9 EDID Override) Version: 1.0.0.0 [11/23/2017]` entry and click Next
  - Close out of the window and you should see something similar to this:
  
    <img width="260" height="155" alt="image" src="https://github.com/user-attachments/assets/e0cb5c8b-c5c3-407e-a0d4-92e70c2323c9" />
- Interlaced display hardware requires you to output games at the display native resolutions (4k or 1080p)

## Windows & Nvidia Settings
- In Windows Display Settings, choose your device and then choose `Make this my main display` for 3DVision to work
- Set its resolution to your native resolution: `3840x2160` or `1920x1080`
- Open Nvidia Control Panel
  - Select the `Set up stereoscopic 3D` tab
  - Check the `Enable stereoscopic 3D` box and click `Apply`
  - You should see the `Stereoscopic 3D display type` as `Acer Passive 3D LCD`
    <img width="690" height="451" alt="image" src="https://github.com/user-attachments/assets/3a180f41-c5fe-415c-8426-b04df1ccdfb8" />


## Device Specific Settings
<details markdown="1">
  <summary markdown="span">Show LG 4K TVs</summary>

</details>


## Running a Game
- Ensure your device is set as your primary display, 3840x2160 or 1920x1080 resolution, and 3D Vision is enabled as `Acer Passive 3D LCD`
- Install fixes either manually from <a href="https://helixmod.blogspot.com/2013/10/game-list-automatically-updated.html" target="_blank" rel="noopener noreferrer">Helixmod</a> or using 3D Fix Manager
- If eyes need to be swapped
  - Close 3D Fix Manager completely from taskbar
  - <img width="231" height="127" alt="image" src="https://github.com/user-attachments/assets/14a4869b-355d-4f06-ace8-a273a83c8222" />
  - Use <a href="https://helixmod.blogspot.com/2015/10/advanced-3d-vision-configuration.html" target="_blank" rel="noopener noreferrer">Advanced 3DVision Configuration</a> (run as admin) to set `Swap Interleave Pattern` and `Lock Registry Key` and click `Save Settings`
  - <img width="291" height="288" alt="image" src="https://github.com/user-attachments/assets/893676b3-c9f9-4717-a746-d8a29c202876" />
  - Unfortunately 3D Fix Manager resets eye swap, so it can't be used to launch games
- Launch the game manually and it should start on your device in interlaced 3D
- Configure the game resolution to match your native resolution, otherwise you won't get 3D
