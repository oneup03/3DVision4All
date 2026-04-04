## 3DVision Installation
- If you run into issues with 3DVision stuck in Anaglyph mode or frame sequential modes, try using <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to completely wipe GPU drivers and redo the installation of this tool
- Use <a href="https://helixmod.blogspot.com/2017/05/3d-fix-manager.html" target="_blank" rel="noopener noreferrer">3D Fix Manager</a> to install 3DVision drivers
  - This can be used to install game fixes and launch them in 3D
  - Disable 3D with the toggle in the top right

## Old Driver for DX10/DX11/DX12
- RTX20xx or older and driver 452.06 and older support 3DVision on all DirectX versions
- DX12 3DVision doesn't really have any fixes for it and it's not stable with many modern games
- Download either <a href="https://www.nvidia.com/en-us/drivers/details/162980/" target="_blank" rel="noopener noreferrer">452.06</a> or <a href="https://www.nvidia.com/en-us/drivers/details/145872/" target="_blank" rel="noopener noreferrer">425.31</a> somewhere
- Unplug/Disable your Internet connection
- Run <a href="https://www.guru3d.com/download/display-driver-uninstaller-download/" target="_blank" rel="noopener noreferrer">DDU</a> in safe mode to wipe the GPU driver
- Run the Nvidia installer you downloaded and be sure to deselect `Geforce Experience`
- Follow <a href="https://www.tenforums.com/tutorials/146562-prevent-windows-update-updating-specific-device-driver.html" target="_blank" rel="noopener noreferrer">these instructions</a> to block Nvidia driver updates
- If using 452.06, use 3D Fix Manager to install the 3DVision driver