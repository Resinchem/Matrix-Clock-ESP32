## An ESP32 and WS2812b LED Matrix Clock (and more)

![Image](https://github.com/user-attachments/assets/c14741ac-3b36-426a-b996-b0b9679540eb)

This 400 WS2812b LED matrix display uses an ESP32 to produce the following features:

- Time and Temperature:
  - Time can be 12/24 (military) hour
  - Inside or outside temperature (or both) in °F or °C
  - Auto-syncing to NTP server or manually set the date/time
  - Optionally switch to showing the time as a "binary clock"

- Scoreboard:
  - Customizable team names
  - Easily increment each score or directly input a value.
  - Scores can be controlled via web app, API or by physical push buttons

- Countdown Timer:
  - Set a starting time as short as 1 second or up to 99 minutes, 59 seconds
  - Different customizable colors for paused, running and final minute
  - Onboard buzzer sounds when time expires (can be disable in settings)
  - Start, stop and reset via web app, API or by physical push buttons

- Text Display:
  - Display up to two lines of text
  - Multiple effects such as alternate flashing, fade-in/fade out, one letter at time, etc.
  - Can set text values via web app or API

- Other Features:
  - Brightness control
  - All display colors and modes are configurable
  - Different selectable fonts for large number displays
  - API permits integration/automation with external systems such as Home Assistant
  - Simply onboarding and setup.  All options configurable via web app (no coding required for standard build)

- Optional secondary WLED controller
  - Extend the functionality with all the effects and features of full WLED
  - Auto-switching between controllers.  Show WLED when WLED is on and show the clock when WLED is off
  - Microphone for WLED audio-reactive effects

### **Note**: _The firmware is specifically designed for WS2812b LED strips in a 25x16 matrix layout.  To use other types of LEDs or to use a matrix with other dimensions, substantial changes to the source code will be needed!_  This is up to you to complete, as I will not be creating compiled firmware for other types of LEDs or dimensions.

See the /docs folder for information on how letters/numbers are 'overlaid' on the matrix.  Different dimensions or layout of the LEDs will require that all those overlays be remapped and the source code changed for the updated layout.

This repo and the [wiki](https://github.com/Resinchem/Matrix-Clock-ESP32/wiki) cover installation, configuration and use of the firmware.  It does not cover the physical build of the controller(s), wiring or the enclosure/case.  For build and wiring details, please refer to the following source:

YouTube Video (overview): [ESP32 LED Matrix: Multi-Function with FULL WLED and more!](https://youtu.be/nE2EDA_VE_Y)

Written Blog Article (full details): [An ESP32 LED Matrix Clock, Scoreboard, Timer and more with FULL WLED](https://resinchemtech.blogspot.com/2025/01/matrix32.html)

It takes substantial time, effort and cost to develop and maintain this repository. If you find it helpful and would like to say 'thanks', please consider supporting this project and future development:

<a href="https://www.buymeacoffee.com/resinchemtech" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/v2/default-yellow.png" alt="Buy Me A Coffee" style="height: 60px !important;width: 217px !important;" ></a>

 
