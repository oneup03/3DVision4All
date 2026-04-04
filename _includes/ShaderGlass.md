## Hardware Requirements
- Nvidia GPU
- 3D Display (Can be AR Glasses, SR Display, etc)
- Choose a device:
  - EDID emulator/dummy plug 
  <img width="239" height="211" alt="image" src="https://github.com/user-attachments/assets/c1ab9338-8b95-498a-b988-ebf9c24c7af2" />
  
  - Secondary display that you can override the EDID of without causing issues




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