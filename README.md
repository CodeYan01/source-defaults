# Source Defaults for OBS Studio

An OBS Studio Plugin that lets you set a source as a "default source". Created
sources of the same type will get the settings from the configured default
source.

## Installation

Binaries for Windows, MacOS, and Linux are available in the [Releases](https://github.com/CodeYan01/source-defaults/releases) section.

## Usage

1. Create a source that will contain the default settings. It is recommended that
you put all default sources in a "Defaults" scene for organization.
2. Configure the new source with all the settings you want to be applied to
new sources of the same type.
3. Add a Source Defaults filter to the source. This will copy the source's
settings to new sources of the same type (i.e. if you added it to a Media
Source, only new Media Sources will be affected).
4. Configure the filter. By default, all the options are on, but you may turn
off some of the options if you do not want all settings to be copied over.
5. Repeat steps 1-4 if you want to set the defaults for other source types.

## Features

The Source Defaults filter can copy the following settings:
- Properties
- Filters
- Audio Monitoring Type
- Volume
- Muted/Unmuted
- Stereo Balance
- Sync Offset
- Audio Tracks
- Scene item settings
    - Parent Scene Name
    - Transform
    - Show/Hide (Visibility)
    - Show/Hide Transitions
- Source name settings
    - Prefix
    - Option to apply prefix only if not yet applied

For sources without audio, only Properties, Filters, Scene item settings, and
Source name settings are available.

To be able to copy scene item settings, you need to select the parent scene of
the default source (because a source can be in multiple scenes). If there are
duplicates of the defaults source, the bottommost one is used.

Source name settings are only applied after the source is created.

## FAQ
*Q:* What if I want to use the normal defaults instead of the one I configured?

*A1:* You can click the Defaults button in the Properties window of the new source
to get the usual defaults. For filters, you could delete the added ones.

*A2:* If you want to temporarily turn off the Source Defaults plugin,
you can go to Settings > Hotkeys in OBS and assign hotkeys to the "Show/Hide
Source Defaults" on the source it is added to.


*Q:* Help, when I add a video capture device, the video capture sources freeze!

*A:* Most cameras can only be accessed by one app/process/source. You'll have to
change the camera, or just copy the source and "Paste (Reference)" the source.
If you add a Source Defaults filter on a video capture device, you can also
deactivate it, if you won't use the default video capture device source, so new
sources that get the same camera as the default will still work.


*Q1:* Why isn't the Source Defaults filter copied over to new sources?

*Q2:* Can I add two Source Defaults filters on the same source type?

*A:* Only one Source Defaults filter *per source type* will work, so it does not
make sense that new sources will also get the Source Defaults filter.


*Q:* Why are all options enabled by default?

*A:* Even if they are enabled by default, you could simply not change settings
of the configured "default" source. For example, an audio source's default
volume is already 100%, so even if the Source Defaults filter copy the volume,
it effectively does not change the volume if the "default" source has 100%
volume. So you can leave the "Volume" option of the filter enabled. The options
are simply there if you want more customized functionality (and tell you which
settings are copied).

## Why?

Here are a few sample use cases.

1. Media sources default to Monitoring off, which is unintuitive and requires
many clicks to enable monitoring. This is especially more important when you
are streaming to video conferencing apps using the monitoring device set to a
virtual cable.

2. You can use the Audio Monitor filter to simulate multiple audio buses, which
OBS currently lacks. Source Defaults can make it easier for you by copying
Audio Monitor filters to new sources.

3. Sometimes you just want sources to have other default properties. You would
usually want to have "Close file when inactive" enabled on Media Sources so OBS
does not use a lot of memory keeping all your media in memory.

4. You might also want to have browser sources have "Control audio via OBS" by
default.

5. Maybe you are getting tired of having to press Ctrl+F whenever you add a new
media source. You can use this plugin to make it so Fit to Screen is applied to
new ones. Remember to first set the Parent Scene in the filter settings.

6. You want to prefix sources of the same type, so they appear in dropdown lists
next to each other (e.g. "MS - Example Video") for Media Sources.

## Contact Me
Although there is a Discussion tab in these forums, I would see your message
faster if you ping me (@CodeYan) in the [OBS Discord server](https://discord.gg/obsproject),
in #plugins-and-tools. Please do report bugs or if there are features you'd like
to be added.

## Donations
You can donate to me through [Paypal](https://www.paypal.com/donate/?hosted_button_id=S9WJDUDB8CK5S) to support my development. Thank you!
