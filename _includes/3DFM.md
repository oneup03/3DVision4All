## 3DVision Installation
- If you run into issues with 3DVision stuck in Anaglyph mode or frame sequential modes, try using <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to completely wipe GPU drivers and redo the installation of this tool. This may wipe software EDID overrides
- Download <a href="https://helixmod.blogspot.com/2017/05/3d-fix-manager.html" target="_blank" rel="noopener noreferrer">3D Fix Manager</a> and extract it somewhere
  - This can be used to install game fixes and launch them in 3D
  - You can disable 3D with the toggle in the top right
- Launch `3DFixManager.exe`
- Select the `Drivers` tab and click `Install 3D Driver`. The Video Driver is not necessary for DX9 3DVision, so ignore it
- <img width="709" height="773" alt="image" src="https://github.com/user-attachments/assets/73952332-b8a5-4081-ba13-1d6c222de33a" />
- If prompted, choose `Quick Setup`
- <img width="441" height="218" alt="image" src="https://github.com/user-attachments/assets/9d548df4-35df-4e2d-bbc1-496478c7d8c5" />
- A successful installation should appear like this:
- <img width="709" height="773" alt="image" src="https://github.com/user-attachments/assets/2f8a0f85-a34d-4645-9105-1d33e66481be" />
- On Some drivers, you may need to use the `Complete 3D Setup` button, as the Nvidia 3DVision wizard will fail to run
- <img width="391" height="302" alt="image" src="https://github.com/user-attachments/assets/828f9f5c-a3c4-4406-9f16-3ca9794e0fc7" />
- You can also adjust the 3D hotkeys and view the defaults on this tab:
- <img width="693" height="740" alt="image" src="https://github.com/user-attachments/assets/71d9c5b9-0de5-46dc-9ef6-4548d06414ba" />
- Select the `3D` toggle in the top right to turn off 3D
- ***NOTE: A game needs a pre-existing Nvidia profile with the `StereoProfile` flag set appropriately for any 3D settings to save with `Ctrl + F7`.*** Reference <a href="https://wiki.bo3b.net/index.php?title=Driver_Profile_Settings" target="_blank" rel="noopener noreferrer">Bo3b's Profile Guide</a> for how to use Nvidia Inspector to set this and tweak 3D Profile settings.
- If 3D Fix Manager doesn't work for you, you can try <a href="https://github.com/bo3b/3DV_Installer" target="_blank" rel="noopener noreferrer">Bo3b's 3DVision Installer</a>

<details markdown="1">
  <summary markdown="span">Show Optional: Old Driver for DX10/DX11/DX12</summary>

## Optional: Old Driver for DX10/DX11/DX12
- DX10-12 may be less compatible/stable with the EDID ShaderGlass method, but should work for native 3DVision or Interlaced displays
- RTX20xx or older is required
  - RTX20xx Super series can only go back to driver 452.06
- DX10 and DX12 need driver 425.31
- DX11 Works on any driver <= 452.06
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
