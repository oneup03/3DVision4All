# 3DVision4All
Run 3DVision games on any 3D display with a Nvidia GPU
- Can be used to convert 3DVision to any other 3D format
- All DirectX versions are supported if using a RTX20 series GPU or older and driver <= 452.06
- DirectX9 is supported on all GPUs and driver versions
- Games can run at any resolution, but 4k is preferred for better quality

## Hardware Requirements
- Nvidia GPU
- 3D Display (Can be AR Glasses, SR Display, etc)
- Either
  - EDID emulator/dummy plug 
  <img width="239" height="211" alt="image" src="https://github.com/user-attachments/assets/c1ab9338-8b95-498a-b988-ebf9c24c7af2" />
  
  - Secondary display that you can override the EDID of without causing issues

## Installation
- If you run into issues with 3DVision stuck in Anaglyph mode or frame sequential modes, try using <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to completely wipe GPU drivers and redo the installation of this tool
- <a href="https://www.tenforums.com/tutorials/156602-how-enable-disable-driver-signature-enforcement-windows-10-a.html" target="_blank" rel="noopener noreferrer">Disable Driver Signature Enforcement in Windows</a> - this requires a special reboot
- Plug in your EDID emulator/dummy plug/secondary monitor that you're going to override if it's not already plugged in
- Download the [EDID Override](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/4kNvidiaEDID.inf)
- Open Device Manager as admin
  - Find your device under the `Monitors` section and `Right-Click` it and select `Update Driver`
  - Select `Browse my computer for drivers`
  - Select `Let me pick from a list of available drivers on my computer`
  - Select `Have Disk`
  - Select `Browse` and find the `4kNvidiaEDID.inf` that you saved earlier
  - Choose the first `Acer HR274H (ACR02B9 EDID Override) Version: 1.0.0.0 [11/23/2017]` entry and click Next
  - Close out of the window and you should see something similar to this:
  
    <img width="260" height="155" alt="image" src="https://github.com/user-attachments/assets/e0cb5c8b-c5c3-407e-a0d4-92e70c2323c9" />

- Use <a href="https://helixmod.blogspot.com/2017/05/3d-fix-manager.html" target="_blank" rel="noopener noreferrer">3D Fix Manager</a> to install 3DVision drivers
  - This can be used to install game fixes and launch them in 3D
  - Disable 3D with the toggle in the top right
- In Windows Display Settings, choose your device and then choose `Make this my main display` for 3DVision to work
- Set its resolution to `3840x2160`

  <img width="593" height="429" alt="image" src="https://github.com/user-attachments/assets/e3a9544c-b079-432e-8caa-7bedd7c37d17" />

- You may now need to select any application windows on the taskbar and use the Windows hotkeys to move Windows between displays: `Win + Shift + Left/Right`
- Open Nvidia Control Panel
  - Select the `Set up stereoscopic 3D` tab
  - Check the `Enable stereoscopic 3D` box and click `Apply`
  - You should see the `Stereoscopic 3D display type` as `Acer Passive 3D LCD`
    <img width="690" height="451" alt="image" src="https://github.com/user-attachments/assets/3a180f41-c5fe-415c-8426-b04df1ccdfb8" />

- Install `Shader Glass` on Steam or <a href="https://github.com/mausimus/ShaderGlass" target="_blank" rel="noopener noreferrer">from GitHub</a>
  - Download the [Interlaced2Else.slang](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/Interlaced2Else.slang) shader and drop it into the Shader Glass folder
  - Open Shader Glass
  - Set `Input - Pixel Size` to `x1`
  - Click `Shader - Import Custom` and select the `Interlaced2Else.slang`
  - If needed, go to `Shader - Parameters` and `Swap Eyes` or set `Output Mode` to TaB (SbS is default)
  - If using AR glasses or other Full-SbS displays, set `Output - Aspect Ratio Correction` to `x0.5 (double wide)`
  - Under `Processing` click `Set as default profile`
  - Close Shader Glass
 
- If using a SR Display (Acer Spatial Labs/Samsung Odyssey 3D)
  - Install the software package provided with your SR display (Samsung Odyssey 3D Hub or Acer TrueGame)
  - Download the latest <a href="https://reshade.me/#download" target="_blank" rel="noopener noreferrer">ReShade</a> with full add-on support
  - Run the ReShade installer
      - Browse to to your `ShaderGlass` folder
      - Select `ShaderGlass.exe` and click Next
      - Select `DirectX 11` and click Next
      - Click `Uncheck All` and click Next
      - Select `3DGameBridge by Janthony & DinnerBram` and click Next
      - Click Finish
  - Run Shader Glass
  - Press `Home` to open ReShade and click `Skip Tutorial`
  - Click on the `Add-Ons` tab
  - Select `srReshade` in the menu to enable it
      - Expand the srReshade dropdown and if you get a `Status: Inactive - Unable to load all SR DLLs` then you may need to do these additional steps:
          - Open Windows Run with `Win + R`
          - Paste this command: `cmd /k setx PATH "C:\Program Files\LeiaSR\Platform\bin;%PATH%"`
          - Exit the terminal and reboot
      - 3D can be toggled on and off by using srReshade's `Ctrl + 2` hotkey
  - Click on the `Home` tab
      - Enable ReShade's `Performance Mode` checkbox
  - If ReShade settings don't save and you keep getting prompted for the tutorial, you may have to manually edit `ShaderGlass\ReShade.ini` and disable Tutorial with `TutorialProgress=4`
  - Exit Shader Glass
 
## Running a Game
- Ensure your device is set as your primary display, 3840x2160 resolution, and 3D Vision is enabled as `Acer Passive 3D LCD`
- Install fixes either manually from <a href="https://helixmod.blogspot.com/" target="_blank" rel="noopener noreferrer">Helixmod</a> or using 3D Fix Manager
- Launch the game manually or from 3D Fix Manager and it should start on your device, not your 3D monitor, in interlaced 3D
- `Alt + Tab` out of the game
- Launch Shader Glass
- If necessary, activate the Interlaced2Else shader via selecting `Shader - Recent Imports - Interlaced2Else`
- Select the game window under `Input - Window`
- `Alt + Tab` back to the game and let it activate
- Press `Ctrl + Shift + G` to toggle fullscreen Shader Glass and you should be seeing 3D


## Thanks to:
- Mausimus for Shader Glass
- Joker18 for the 4k EDID override
- MarkAndGo's <a href="https://github.com/markandgo/obs-shaderfilter-mngo/" target="_blank" rel="noopener noreferrer">similar project for OBS</a> that inspired this one
