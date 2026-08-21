# YouFM

Control your desktop media (Spotify, YouTube, VLC, web browsers) directly through the in-game radio in *The Crew*.

YouFM adds a custom radio station called **YOU FM**. It hooks the game's UI buttons to your Windows media session, so you can change tracks and pause music without alt-tabbing.

## How It Works
When you tune into the **YOU FM** station, the in-game radio controls (Play, Pause, Skip, Previous) work like media keys on your keyboard:
* Controls any application that supports Windows Media Sessions (Spotify, Chrome, Firefox, VLC, etc.).
* Uses a silent dummy playlist so in-game music does not play over your external audio.
* Runs through a custom `dxgi.dll` hook that intercepts in-game UI commands in real time.

## Installation
1. **Download:** Get the latest `YOUFMRadio.zip` from [Releases](https://github.com/sorooshcraft/You-FM/releases).
2. **Install Mod:** Install the `.zip` file as a mod using [PitCrew](https://github.com/Telonof/PitCrew). If you use other custom radio mods, make sure this one is loaded first (right after Brokemogul).
3. **Install DLL:** Copy `dxgi.dll` from the download and paste it into your *The Crew* game folder (in the same directory as `TheCrew.exe`).

> [!NOTE]
> **Experimental Build:** The `/unstable` folder contains a proof-of-concept build that tries to render the current track and artist name directly in the game UI. It is currently unstable and crashes after two to fifteen minutes. Use the main release for a stable experience.

## Credits
* **TuneinCrew ([Telonof](https://github.com/Telonof/TuneinCrew)):** Playlist creation tool used to build the silent dummy radio station. Special thanks to Telonof for help with repacking the game's GUI files.

## License
Following the requirements of the tools used in this project, YouFM is licensed under **GPL-3.0**. See the `LICENSE` file for details.
