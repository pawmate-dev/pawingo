# Japanese plugin

Run `just run` to build Pawingo and the Japanese plugin and load the plugin from `build/plugins`.
Select `Japanese` on the home screen, then choose `五十音図`. Press Escape to return to the feature
list, and Escape again to return home. If a download confirmation is open, Escape dismisses it
first.

The chart uses five vowel rows and consonant columns arranged from right to left. Use the arrow keys
to select a sound, skipping empty cells. Hiragana and katakana are displayed together and share the
same pronunciation. The selected pair uses the shared terracotta accent; row and column labels
remain subdued. Press Enter to pronounce the selected sound; playback runs in the background so
navigation remains responsive.

Speech is optional. The plugin can use a locally installed eSpeak NG with Japanese voice data.
Existing Mei installations are detected when opening the panel and are reused. Press `d` to see the
installed state or, if missing, a download confirmation and press `d` again to download the
approximately 0.9 MB Mei voice from GitHub; press Escape to cancel. The voice is verified with
SHA-256 and saved to `$XDG_DATA_HOME/pawingo/tts/japanese/mei_normal.htsvoice`, or
`~/.local/share/pawingo/tts/japanese/mei_normal.htsvoice` if `XDG_DATA_HOME` is unset. Using Mei
also requires Open JTalk, its Japanese dictionary, and `aplay`. Set `PAWINGO_OPENJTALK_DICT` to the
dictionary directory. If Mei cannot be used, the plugin tries eSpeak NG.

Mei voice credits are in [ATTRIBUTION.txt](../plugins/japanese/license/ATTRIBUTION.txt).
