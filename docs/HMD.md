# VR HMDs with Virtual 3D Screens
VR HMDs have a few options to play 3DVision games on a virtual screen. The recommended path is the injector publishing to a VRScreenCap or Katanga viewer over IPC.

{% include 3DFM.md %}

{% include Injector.md %}

## Configuration
- In `3dvision4all.ini`, set `mode = katanga`
- The injector publishes the stereo image to a Katanga-compatible VR viewer — your desktop display can stay as your regular monitor
- The overlay window on your desktop also shows a SbS preview so you can verify capture even before the VR viewer attaches

## VR Viewer (VRScreenCap recommended)
- Download <a href="https://github.com/artumino/VRScreenCap" target="_blank" rel="noopener noreferrer">VRScreenCap</a> (or use Katanga from your `3DFM\Tools\Katanga` folder)
- For VRScreenCap, create a desktop shortcut to `vr-screen-cap.exe` and append these parameters to `Target` for a slightly-curved virtual screen at a comfortable distance:
  - `--x-curvature=0.1 --y-curvature=0.0 --distance=0.5 --scale=0.75`
- Start your VR runtime (SteamVR / Meta Link / etc.) and launch the VRScreenCap (or Katanga) shortcut FIRST so the IPC server is listening
- Then launch the game — the injector will publish frames to the viewer and you'll see the stereo image in your HMD

{% include InjectorPlay.md %}

<details markdown="1">
  <summary markdown="span">Show HelixVision/Katanga (legacy)</summary>

## HelixVision/Katanga
- Download <a href="https://bo3b.s3.us-east-1.amazonaws.com/fix_manager_1.87_katanga.7z" target="_blank" rel="noopener noreferrer">3D Fix Manager</a> for Katanga and to install 3DVision drivers

## Nvidia Control Panel (or run 3DFM and let it enable 3D)
- Select the `Set up stereoscopic 3D` menu item
- Select `3D Vision Discover`
- Set Depth to `100%`
- Set Keyboard Shortcuts -> `Enable advanced in-game settings`
- `Enable stereoscopic 3D`
- `Apply`
- Close Nvidia Control Panel

## Katanga Setup
- Run katanga.exe from your `3DFM\Tools\Katanga` folder
- VR should start and you should see a slideshow on a curved virtual monitor
- `Home` key will flatten the display
- `Arrow Keys` will move and resize the slideshow
- Close Katanga

## Game Setup
- Helixmod installation
    - Run the game at least once to configure graphics settings according to any requirements from the 3DVision driver or Helixmod
    - Install the 3D fix - can be done manually by downloading it from <a href="https://helixmod.blogspot.com/2013/10/game-list-automatically-updated.html" target="_blank" rel="noopener noreferrer">HelixMod</a> or through 3D Fix Manager
    - Run the game again and make sure you are seeing Anaglyph 3D
    - Exit the game
- Game Launch Shortcut (Can use HelixVision instead of this process, but it doesn't include all possible DX9 games)
    - Create a shortcut for katanga.exe on your desktop by `Right Click -> Send To -> Desktop`
        - Create a new shortcut for each game you want to run
    - Rename the shortcut to match the game you are going to run
    - Open the Properties of the shortcut `Right Click -> Properties`
    - Add but **DO NOT DELETE** from the end of the `Target` field:
        - `--game-path "c:\your\path\to\game.exe"` A game's exe location can be found using Steam's `Manage -> Browse local files` or in 3D Fix Manager under the title of the game
        - `--launch-type DX9` or `--launch-type DX9Ex` Try the regular DX9 first and then DX9Ex if it fails
    - Click `OK` to close the Shortcut Properties

## Running the Game
- Launch the shortcut on your desktop you created or launch a game from HelixVision
- Wait until the game reaches its main menu in Anaglyph 3D
- Put on your HMD and you should see a 3D window
- HelixMod in-game hotkeys:
    - `Ctrl + F3/F4` adjust depth
    - `Ctrl + F5/F6` adjust convergence
    - `Ctrl + F7` saves depth & convergence settings

## Notes
- It is possible to get stuck where you can't properly exit the game or Katanga. In these situations, use `Ctrl + Alt + Del` and select `Sign Out` and allow Windows to force close everything.

</details>
<details markdown="1">
  <summary markdown="span">Show Virtual Desktop (TaB via Shader Glass)</summary>

## Top and Bottom / Over Under 3D
- Install <a href="https://www.vrdesktop.net/" target="_blank" rel="noopener noreferrer">Virtual Desktop</a>
- Follow the guide below, and in the Shader Glass step `Shader - Parameters` set `Output Mode` to TaB
  - TaB will give you the most resolution for this Shader Glass method

{% include ShaderGlass.md %}

## VR Additional Setup
- In the Steam Library page for Shader Glass, Select the `Gear` icon and select `Properties`
- Set `Launch Options` to `-p` and close the Properties window

{% include ShaderGlassPlay.md %}

## VR Additional Steps
- After selecting the input window, select `Processing -> Start`
- Start Virtual Desktop
- Select the fullscreen Shader Glass Window as the input
- Set the 3D mode to TaB/OU

</details>


{% include Notes.md %}
