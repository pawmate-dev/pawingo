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

Press `d` to open VOICEVOX configuration, or choose `音声設定` from the feature list. Escape returns
from the embedded configuration to the chart. See the VOICEVOX section below. When CORE is not
configured, existing Mei/Open JTalk installations and eSpeak NG remain available as legacy
fallbacks. Mei uses the existing file in `$XDG_DATA_HOME/pawingo/tts/japanese/mei_normal.htsvoice`
(or `~/.local/share/pawingo/tts/japanese/mei_normal.htsvoice`), with `PAWINGO_OPENJTALK_DICT`
pointing at the dictionary; the legacy path also requires `open_jtalk` and `aplay`.

Mei voice credits are in [ATTRIBUTION.txt](../plugins/japanese/license/ATTRIBUTION.txt).

## Review

Choose `復習` from the Japanese feature list to review saved Japanese words. The queue is scoped to
`ja`, including words saved with definitions in other target languages. It reuses shared FSRS
scheduling and persistence and supplies Japanese meaning, reading and listening questions. Kanji
words with a saved kana reading get three independent cards; kana-only words keep meaning cards.
Reading questions hide the pronunciation; listening questions hide spelling and reading and use the
meaning to distinguish homophones. Ctrl+P plays/replays listening audio through the configured
VOICEVOX voice. Successful playback is required before checking; missing voice resources leave the
card ungraded. Reading answers accept equivalent kana/width forms while retaining long-vowel, sokuon
and small-kana distinctions. Listening answers require the saved written form. Escape edits feedback
first, then returns to the Japanese feature list. See [review usage](review.md) for input and rating
keys.

## Vocabulary search

Choose `語彙検索` in the Japanese feature list. Type Japanese spelling, a kana reading, or meaning
text and press Enter. Exact word and reading matches rank before prefixes and meaning matches.
Ordinary katakana readings also match their hiragana forms. The panel displays up to 40 matches,
with five visible at a time; use the arrow keys to select a result. The complete selected meaning
and its JMdict/EDRDG source are shown below the list.

Press Enter again to open the selected result's detail page, with its spelling, reading, complete
definition, definition language and source. Up/Down scroll the details; Ctrl+S saves and Ctrl+P
speaks. Escape returns to the result list with its query and selection preserved. Editing the query
or target language makes the next Enter run a new search instead of opening an outdated result.
Editable fields stay inactive while details are open.

The dictionary importer retains entry IDs and original sense numbers for example linking. Rich
JMdict metadata such as part-of-speech tags, priority markers, dialects and cross-references is not
yet displayed. Examples come from a separate offline resource; changing to `JMdict_e_examp` is not
required and its embedded example elements are not imported.

Press Tab to edit the target definition language, then Enter to search again. The target accepts
codes such as `en`, `de`, `fr`, `nl`, `es`, `ru`, `hu`, `sl`, and `sv`, or JMdict's three-letter
language identifiers. Availability depends on the imported file and the individual entry. An
English-only file cannot provide German definitions. Missing target definitions produce an empty
result rather than silently substituting another language.

Use Ctrl+S to save the selected result for review, preserving its reading, meaning language, and
source. Ctrl+P pronounces the selected word through the same optional local speech setup as the kana
chart. VOICEVOX analyzes the original spelling to retain its predicted pitch accent, then compares
the analyzed moras with the selected reading. Matching hiragana and katakana are equivalent. If the
readings differ, it analyzes the selected reading instead, preserving that query's predicted accent
and mora pitches. Long-vowel spelling differences trigger this conservative fallback as well.
Predictions are not authoritative dictionary accent annotations; the current dictionary importer
does not supply accent entries or user accent overrides. Legacy speech adapters receive the selected
reading. Searches, imports, downloads, and speech run outside the input thread and are cancelled
when leaving the panel.

### Offline dictionary management

Ctrl+D opens the dictionary manager. It supports importing a local JMdict XML or gzip file and
explicitly downloading/updating the full multilingual JMdict from
<https://www.edrdg.org/pub/Nihongo/JMdict.gz>. Downloading requires confirmation and uses the linked
libcurl HTTPS client; normal searches never contact a remote service. Imports are parsed and
validated before replacing an existing managed dictionary. A failed or cancelled import preserves
the previous file.

The managed file is `$XDG_DATA_HOME/pawingo/dictionaries/japanese/JMdict`, falling back to
`~/.local/share/pawingo/dictionaries/japanese/JMdict`. The file can contain plain XML or gzip data.
It is reused across restarts. A shared in-memory snapshot is loaded on first search and reused for
subsequent searches; this initial load can take longer than cached lookups.

To use an existing dictionary without copying it, set `PAWINGO_JMDICT` to its path. The panel also
uses enabled host dictionary resources whose language is `ja` and whose location names a local
JMdict XML/gzip file. The backend's `LanguagePlugin::search` method uses the environment override
and the managed file; the panel additionally includes host-configured resources.

### Independent offline examples

Ctrl+D opens **Dictionaries & examples**. Its example actions are separate from JMdict installation:

- **Download / update indexed examples** asks before downloading the official
  [WWWJDIC export](https://downloads.tatoeba.org/exports/wwwjdic.csv). Despite its `.csv` name, this
  is a five-column, tab-delimited Japanese/English corpus with word indices. Successful installation
  returns to the originating details and refreshes its examples.
- **Import indexed examples** accepts that same plain TSV or gzip format from a local file.
- **Import translations for the current target** accepts local Tatoeba sentence pairs for the target
  selected in search. Select the intended target before importing. This replaces only that
  language's translations and preserves the dictionary and other translation languages.

For additional translations, use [Tatoeba's sentence-pair exports](https://tatoeba.org/en/downloads)
with Japanese as the sentence language, the desired translation language, and these four fields in
order: **sentence ID, sentence text, translation ID, translation text**. The default full TSV layout
matches this order. Plain/gzip, UTF-8 BOM and CRLF files are supported. Rows outside the installed
Japanese index are skipped. A conflicting Japanese text for an indexed ID, an invalid file, or an
import with no matching sentences leaves the installed data unchanged. For example:

```text
101<TAB>猫がいる。<TAB>301<TAB>Eine Katze ist da.
```

`<TAB>` above means a real tab character. Translation language is an explicit import choice, not
inferred from the filename. The starter index supplies English translations; German and other
targets require their own imported pairs. Missing target translations are labeled explicitly;
Japanese examples remain visible and English is never substituted silently.

The managed SQLite resource is `$XDG_DATA_HOME/pawingo/examples/japanese/examples.sqlite3`, or
`~/.local/share/pawingo/examples/japanese/examples.sqlite3`. It is reused across restarts and is
independent of the vocabulary/review database and the multilingual JMdict file. No corpus is bundled
or downloaded during lookup. Imports build a private candidate, validate it and atomically commit;
failure/cancellation before commit preserves the prior resource. Index updates retain independently
imported non-English translations. Concurrent imports from separate processes are unsupported.

Details show up to five linked examples, with checked usages first. Up/Down scrolls the bounded
detail region. Source and translation IDs link to their Tatoeba pages; contributor attribution and
the export's CC BY 2.0 FR notice remain visible. The four-column export does not contain author
names, so Pawingo retains attribution links rather than inventing individual author metadata.

Tab / Shift+Tab cycles between the word and its Japanese example sentences. A terracotta dot and
accent text identify the current speech target; selecting an example scrolls it into view. Ctrl+P
speaks that target using the configured voice. Sentences retain their complete Japanese text and
punctuation, without applying the word's reading to them. Selection remains responsive during
playback; a running request keeps its original target, and overlapping playback is ignored. Up/Down
still scrolls the detail region. Ctrl+S always saves the dictionary word, even when an example is
selected. Escape returns to the result list and cancels pending work; reopening a word selects the
word again.

Association follows
[EDRDG's index format](https://www.edrdg.org/wiki/Sentence-Dictionary_Linking.html): explicit JMdict
entry IDs take precedence; otherwise an exact indexed headword must be unambiguous at the available
spelling/reading specificity. Explicit readings and sense numbers must agree with the selected
result. Bare ambiguous headwords are excluded; unspecified senses are labeled **word-level**.
Surface inflections are not treated as dictionary identities. This does not perform substring
matching or claim that a checked usage marker guarantees a flawless translation.

### Dictionary attribution

JMdict is a project of the Electronic Dictionary Research and Development Group (EDRDG). The
Japanese and English components are copyright James William Breen and EDRDG and are available under
CC BY-SA 4.0. Other translations retain their contributors' respective copyright and terms. Pawingo
does not bundle a full dictionary and identifies JMdict/EDRDG in search results and saved
vocabulary. See the
[project documentation](https://www.edrdg.org/wiki/index.php/JMdict-EDICT_Dictionary_Project) and
[EDRDG license statement](https://www.edrdg.org/edrdg/licence.html) for the source, attribution, and
distribution conditions. Reimport a newer file or explicitly download an update through the same
manager to refresh the installed data.

## VOICEVOX CORE

Open `音声設定` to configure an offline VOICEVOX CORE 0.16.3 installation. Obtain CORE, VOICEVOX
ONNX Runtime, an Open JTalk dictionary, and a `.vvm` voice model using the
[official downloader](https://github.com/VOICEVOX/voicevox_core/releases/tag/0.16.3). The runtime is
the VOICEVOX-specific distribution, not a generic ONNX Runtime library.

Select **download & install** for an automatic CPU setup (CORE 0.16.3, VOICEVOX ONNX Runtime 1.17.3,
Open JTalk dictionary 1.11, and starter voice model `0.vvm` 0.16.0). Confirm network access with
Enter. Resources come from the official GitHub releases; downloads and extraction run on a worker.
Review the downloaded model and runtime terms using the arrow keys, then press Enter to accept and
install, or Escape to discard the candidate. Installed resources are reused; their paths are filled
in and saved automatically. The managed bundle lives under `$XDG_DATA_HOME/pawingo/tts/voicevox`,
`~/.local/share/pawingo/tts/voicevox`, or Windows `%LOCALAPPDATA%/pawingo/tts/voicevox`. Network or
extraction failures preserve existing settings.

For an existing installation, enter absolute paths to the CORE shared library, runtime shared
library, dictionary directory, and model file. Choose **load styles**, Tab to the styles list, use
arrows and Enter to choose an installed speaker/style, then **save**. Ctrl+P previews the current
selection. Voice model licenses and attribution requirements are separate from the CORE library's
MIT license.

The focused input row has a terracotta dot and label, including when empty. Tab and Shift+Tab move
between fields and actions.

Settings persist in `$XDG_CONFIG_HOME/pawingo/voicevox.yaml`, or `~/.config/pawingo/voicevox.yaml`
(Windows uses `%APPDATA%/pawingo/voicevox.yaml`). Kana and vocabulary pronunciation read the same
saved configuration. Downloads require the explicit confirmation described above. An empty CORE path
retains the legacy speech fallback.

Inference currently uses CPU with two threads. Synthesis runs on a worker; CORE's synchronous
inference cannot be interrupted mid-call. Cancellation skips subsequent work and stops playback;
leaving a panel waits for native inference to finish safely. Native runtime libraries remain loaded
until process exit because ONNX initialization is global. Restart Pawingo after changing runtime
versions or replacing loaded native libraries. Audio playback uses miniaudio rather than a shell
player. Windows/macOS builds of the complete host still require the existing platform-specific
plugin/process adapters to be ported; the new VOICEVOX adapter has platform-specific library
loading.

Existing `voicevox.json` preferences are read when the YAML file does not exist. Saving writes
`voicevox.yaml` while preserving the old JSON file. YAML preferences take precedence thereafter. The
plugin's bundled defaults, kana data, and ordinary interface copy live in its `resources/` YAML
files; updating those build-time resources requires rebuilding the plugin.

## Module linkage with the host

`just build` builds the `pawingo` and `japanese` targets. Both reuse public interfaces from
`pawingo-runtime`; `japanese-backend` additionally owns the `pawingo.japanese.*` interfaces,
implementations, and embedded resources. xmake propagates public `.cppm` interfaces and builds BMIs
in dependency order.

The host imports shared contracts and loads the Japanese shared library at runtime. It does not
import Japanese implementation modules. `plugins/japanese/entry.cpp` is the small C entry adapter;
`pawingo.japanese.plugin` owns plugin behavior. Unit tests import backend modules directly, while
native integration tests load the same built shared library as the host.

The module migration changes C++ ABI ownership. Rebuild existing native plugins against the module
SDK and matching toolchain; older header-based binaries fail the new `modules-1` ABI check.
