# AR Glasses
Compatible with devices like:
- Xreal
- Viture
- RayNeo
- Rokid

{% include Injector.md %}

## Configuration
- Use the hardware or software hotkey on your glasses to switch to Full-SbS mode
- In `3dvision4all.ini`, set `mode = sbs`
- Make sure your glasses are set as the primary display in Windows so the injector overlay shows on them
- For true Full-SbS (each eye at the panel's per-eye native resolution), set `render_width` and `render_height` to half the panel's width by the full height
  - e.g. `render_width = 1920` / `render_height = 1080` for a 3840x1080 panel
  - The injector upscales each eye to fill the panel
  - Some games only support 16:9 resolutions and don't need this override to maintain aspect ratio in Full-SbS
- If left/right eyes appear reversed, set `swap_eyes = 1`

{% include InjectorPlay.md %}

<details markdown="1">
  <summary markdown="span">Show Shader Glass method (legacy)</summary>

{% include ShaderGlass.md %}

## AR Specific Settings
- Use the hardware or software hotkey on your glasses to switch to Full-SbS mode
- Run Shader Glass
- Set `Output - Aspect Ratio Correction` to `x0.5 (double wide)`
- Under `Processing` click `Set as default profile`
- Close Shader Glass

{% include ShaderGlassPlay.md %}

</details>


{% include Notes.md %}
