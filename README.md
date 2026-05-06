# 📻 YouFM
**Control your Windows Media (Spotify, YouTube, etc.) through The Crew's in-game radio.**

YouFM adds a custom radio station called **YOU FM** to the radio. It hooks the game's native UI buttons directly to your Windows media session, letting you control your music without leaving the driver's seat.

## 🔍 What does it do?
When you select the **YOU FM** station, the in-game radio controls (Play, Pause, Skip, Backtrack) act as media keys for your PC.
*   **Integrated Control:** Works with any app that supports Windows Media Sessions (Spotify, Web Browsers, VLC, etc.).
*   **No Alt-Tabbing:** Keep your focus on the road while switching tracks.
*   **Silent Sync:** Uses a dummy playlist logic to ensure game music doesn't overlap with your external audio.

## 📦 Instructions
1.  **Download:** Get the latest `YOUFMRadio.zip` from [Releases](https://github.com/sorooshcraft/You-FM/releases).
2.  **PitCrew:** Install the `.zip` as a mod using [PitCrew](https://github.com/Telonof/PitCrew). If you have installed other custom radios, ensure this is the first (after Brokemogul)
3.  **DLL Install:** Copy the `dxgi.dll` from the download and paste it into your **The Crew** game folder (next to `TheCrew.exe`).

> [!NOTE]
> **Experimental POC:** There is a version of `dxgi.dll` in the `/unstable` folder. This version is a **Proof of Concept** that attempts to show the Track/Artist name in the game GUI. It is currently unstable and will crash the game within 2–15 minutes. **Use the main release for a stable experience.**

## 💎 Credits
*   **TuneinCrew ([Telonof](https://github.com/Telonof/TuneinCrew)):** The playlist creator tool! YouFM uses a silent dummy playlist created with this tool to enable the in-game radio GUI logic. Also appreciate the help on repacking the gui files. 🤝

## ⚖️ License
Following the requirements of the tools used in this project, YouFM is licensed under **GPL-3.0**. See the `LICENSE` file for details.
