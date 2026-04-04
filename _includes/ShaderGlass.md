## Hardware Requirements
- Nvidia GPU
- 3D Display (Can be AR Glasses, SR Display, etc)
- Choose a device:
  - EDID emulator/dummy plug 
  <img width="239" height="211" alt="image" src="https://github.com/user-attachments/assets/c1ab9338-8b95-498a-b988-ebf9c24c7af2" />
  
  - Secondary display, but you will be limited to its supported resolutions/colors


## EDID Override Options

### 1a. EDID ID Override
- Download <a href="https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU" target="_blank" rel="noopener noreferrer">CRU</a> and extract it to a folder
- Only the `Device ID` has to be changed to enable 3DVision, but it will be limited to the resolutions/colors of the original EDID
- This only changes the EDID ID on your current PC
- Set the Device ID to `ACR02B9`
- If using an EDID emulator/dummy plug, continue on to 1b

### 1b. EDID Resolution Override (maybe optional)
- If you are using a physical secondary display, you probably don't want to do this as it could mess it up
- This should be used for EDID emulators/dummy plugs to ensure you have 4k, 60Hz, 8Bit RGB
- This only changes the EDID on your current PC
- Download []() to the CRU folder

### 2. EDID Firmware Flash (requires writable hardware)
- If you are using a physical secondary display, you probably don't want to do this as it could mess it up
- This will change the device to always identify to any PC/Console as 3DVision and Frame-Packed 3D capable, as well as supporting the needed 4k, 60Hz, 8Bit RGB
- Download NTM's [NTM3D_edid.bin](https://github.com/NTM-3D/3DConsoleBridge/raw/refs/heads/main/NTM3D_edid.bin)
- Download ToastyX's <a href="https://www.monitortests.com/forum/Thread-EDID-DisplayID-Writer" target="_blank" rel="noopener noreferrer">EDID/DisplayID Writer</a>
- Run `EDWriter.exe` 
- Select your EDID emulators/dummy plug in the `Display` drop down
- You may want to backup your current EDID with `Read EDID` and then `Save File`
- Select `Load File` and choose the `NTM3D_edid.bin`
- Select `Write EDID`
- If you get errors, you will have to try a different EDID Override method

### 3. EDID Windows Driver (least recommended)
- Follow the [Interlaced Windows Driver](https://oneup03.github.io/3DVision4All/docs/Interlaced#windows-monitor-driver-edid-override) instructions


{% include 3DFM.md %}


## Windows & Nvidia Settings
- In Windows Display Settings, choose your device and then choose `Make this my main display` for 3DVision to work
- Set its resolution to `3840x2160`

  <img width="593" height="429" alt="image" src="https://github.com/user-attachments/assets/e3a9544c-b079-432e-8caa-7bedd7c37d17" />

- You may now need to select any application windows on the taskbar and use the Windows hotkeys to move Windows between displays: `Win + Shift + Left/Right`
- Open Nvidia Control Panel
  - Select the `Set up stereoscopic 3D` tab
  - Check the `Enable stereoscopic 3D` box and click `Apply`
  - You should see the `Stereoscopic 3D display type` as `Acer Passive 3D LCD`
    <img width="690" height="451" alt="image" src="https://github.com/user-attachments/assets/3a180f41-c5fe-415c-8426-b04df1ccdfb8" />


## Shader Glass Installation
- Install `Shader Glass` on Steam or <a href="https://github.com/mausimus/ShaderGlass" target="_blank" rel="noopener noreferrer">from GitHub</a>
- Download the [Interlaced2Else.slang](https://github.com/oneup03/3DVision4All/raw/refs/heads/main/Interlaced2Else.slang) shader and drop it into the Shader Glass folder
- Open Shader Glass
- Set `Input - Pixel Size` to `x1`
- Click `Shader - Import Custom` and select the `Interlaced2Else.slang`
- If needed, go to `Shader - Parameters` and `Swap Eyes` or set `Output Mode` to TaB (SbS is default)
- Under `Processing` click `Set as default profile`
- Close Shader Glass
- This ShaderGlass Interlaced2Else shader can work for any resolution, not just 4k/1080p