## 3DVision Installation
- If you run into issues with 3DVision stuck in Anaglyph mode or frame sequential modes, try using <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to completely wipe GPU drivers and redo the installation of this tool. This may wipe software EDID overrides
- Download <a href="https://helixmod.blogspot.com/2017/05/3d-fix-manager.html" target="_blank" rel="noopener noreferrer">3D Fix Manager</a> and extract it somewhere
  - This can be used to install game fixes and launch them in 3D
  - You can disable 3D with the toggle in the top right
- Launch `3DFixManager.exe`
- Select the `Drivers` tab and click `Install 3D Driver`. The Video Driver is not necessary for DX9 3DVision, so ignore it
- <img width="709" height="773" alt="image" src="https://github.com/user-attachments/assets/73952332-b8a5-4081-ba13-1d6c222de33a" />
- A successful installation should appear like this:
- <img width="709" height="773" alt="image" src="https://github.com/user-attachments/assets/2f8a0f85-a34d-4645-9105-1d33e66481be" />
- Select the `3D` toggle in the top right to turn off 3D

<details markdown="1">
  <summary markdown="span">Show Optional: Old Driver for DX10/DX11/DX12</summary>

## Optional: Old Driver for DX10/DX11/DX12
- RTX20xx or older and driver 452.06 and older support 3DVision on all DirectX versions
- DX12 3DVision doesn't really have any fixes for it and it's not stable with many modern games
- Download either <a href="https://www.nvidia.com/en-us/drivers/details/162980/" target="_blank" rel="noopener noreferrer">452.06</a> or <a href="https://www.nvidia.com/en-us/drivers/details/145872/" target="_blank" rel="noopener noreferrer">425.31</a> somewhere
- Unplug/Disable your Internet connection
- Run <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to wipe the GPU driver
- Run the Nvidia installer you downloaded and be sure to deselect `Geforce Experience`
- Follow <a href="https://www.tenforums.com/tutorials/146562-prevent-windows-update-updating-specific-device-driver.html" target="_blank" rel="noopener noreferrer">these instructions</a> to block Nvidia driver updates
- If using 452.06, use 3D Fix Manager to install the 3DVision driver and Enable Driver Hack as seen here:
- <img width="755" height="466" alt="image" src="https://github.com/user-attachments/assets/cd78dd24-126b-4511-a66f-ebc1e2e3d19d" />
- You should now get this result (no emitter for some displays):
- <img width="797" height="986" alt="image" src="https://github.com/user-attachments/assets/98a043c8-1d6f-4583-b7b5-b11c6774cdc1" />

</details>
