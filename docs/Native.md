# 3DVision Native Displays

## Hardware Options
- 3DVision Monitor + Glasses + Emitter
- 3DTV/Projector with Frame Packing/BluRay3D/HDMI3D Support
- Anaglyph Glasses

{% include 3DFM.md %}

## Device Specific

<details markdown="1">
  <summary markdown="span">Show 3DTV/Projector</summary>

## 3DTV/Projector with Frame Packing/BluRay3D/HDMI3D Support
- Nvidia DSR can be utilized to get SSAA on games
- Download <a href="https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU" target="_blank" rel="noopener noreferrer">CRU</a> and extract it to a folder
- Run `CRU.exe` as admin
- Delete every resolution above `1280x720` or `1280x1470`
- In Nvidia Control Panel under `Manage 3D Settings` set `DSR Factors` as desired
- Change Windows Display Settings resolution and scale as desired

</details>
<details markdown="1">
  <summary markdown="span">Show High Refresh Rate Display</summary>

## High Refresh Rate (non-3DVision) Displays
- You can attempt to get any high refresh rate monitor to work with 3DVision, but it's hard to get good timings and avoid crosstalk
- You still need Nvidia 3D Glasses + Emitter
- Download <a href="https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU" target="_blank" rel="noopener noreferrer">CRU</a> and extract it to a folder
- Run `CRU.exe` as admin
- Find your current display's EDID (should be 7 alphanumeric characters)
- Download <a href="https://github.com/rajkosto/NvTimingsEd" target="_blank" rel="noopener noreferrer">NvTimingsEd</a> and extract it to a folder
- Run `NvTimingsEd.exe` as admin
- Reference <a href="https://www.mtbs3d.com/forum/viewtopic.php?t=25545" target="_blank" rel="noopener noreferrer">this forum post</a> for examples of use

</details>
<details markdown="1">
  <summary markdown="span">Show LG OLED TV CX/C9</summary>

## LG OLED CX/C1

## Required Tools
- **3D Vision compatible emitter and shutter glasses**
- **NvTimingsEd**  
  https://github.com/rajkosto/NvTimingsEd
- **CRU (Custom Resolution Utility)**  
  https://www.monitortests.com/forum/Thread-Custom-Resolution-Utility-CRU
- **3D Fix Manager**  
  https://helixmod.blogspot.com/2017/05/3d-fix-manager.html

---

## Optional Preparation
### 0a) Backup Default Settings
Use **CRU → Export** from the main menu to make a backup of the C1’s default settings.

### 0b) Recommended TV Settings
For consistency, it is recommended to use the community-recommended settings from:
https://www.reddit.com/r/OLED_Gaming/comments/mbpiwy/lg_oled_gamingpc_monitor_recommended_settings/

---

## Setup Steps

### 1) Connect the 3D Vision Emitter
Plug in the **3D Vision emitter**.

### 2) Install 3D Vision Software
Open **3D Fix Manager** → **Drivers** → install **3D Vision software**

### 3) Select Base Monitor Profile
Open **NvTimingsEd** and choose:
`VSC_591E`

### 4) Remove Default Resolutions
Delete the default resolutions.

### 5a) LG C1 (1440p Recommended)
Paste into upper-left field:
`6FC20000A00A000A010AA000310A2000F505A005A1055500A4050500`

Decode and verify it matches:
`2560x1440 @ 119.997 Hz`

### 5b) LG CX
Manually match `2560x1440` timings from CRU.

### 5c) Optional 1080p Mode
Use:
`8A6F0000200880078107A000B107200078043804390440003C040500`

### 6) Configure Glasses Timing
Paste into upper-right field:
`A286010020B20000F80A000051C3000003040102`

Command order:
- RIGHT_OFF
- RIGHT_ON
- LEFT_OFF
- LEFT_ON

### 7) Export to Driver DLL
`To nvstres.dll → Open → OK → Open → OK → OK`

Do **not** restart yet.

### 8) Remove 4K 60Hz Entry
Delete:
`3840x2160 @ 60.000 Hz`

### 9) Rename Monitor ID
Change ID to:
`VSC591E`

### 10) Restart PC
Restart your computer.

### 11) Set Picture Mode
Set TV to:
`Game Optimizer`

### 12) Open Game Optimizer
`Cog button → Game Optimizer`

### 13) Enable BFI
Set:
`OLED Motion Pro → Medium / 120Hz`

If greyed out:
- `Prevent Input Lag → Standard`
- disable **VRR / FreeSync**

</details>


## Running a Game
- Ensure your device is set as your primary display and 3D Vision is enabled
- Install fixes either manually from <a href="https://helixmod.blogspot.com/2013/10/game-list-automatically-updated.html" target="_blank" rel="noopener noreferrer">Helixmod</a> or using 3D Fix Manager
- Launch the game manually or from 3D Fix Manager and it should start on your device in 3D
- 3DVision Monitors require the game to run at your 3D refresh rate (120Hz or 100Hz nominally)
- Frame Packed 3D devices require the game to run at 1280x720 resolution