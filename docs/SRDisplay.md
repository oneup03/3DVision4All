# SR Displays (Acer Spatial Labs/Samsung Odyssey 3D)
Compatible with devices like:
- Acer Spatial Labs
- Samsung Odyssey 3D

{% include ShaderGlass.md %}

## 3D Game Bridge Installation
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

{% include ShaderGlassPlay.md %}


{% include Notes.md %}
