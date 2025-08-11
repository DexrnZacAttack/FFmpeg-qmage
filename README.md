FFmpeg README
=============

> [!IMPORTANT]
> This fork was made to add the patches for the Quram Qmage codec seen in Samsung devices
>
> It's based off of commit [`3528bfed450842a991df6e076fe72d4c2eee6432`](https://github.com/FFmpeg/FFmpeg/commit/3528bfed450842a991df6e076fe72d4c2eee6432) with the following patches applied:
>
> - [`[FFmpeg-devel] [PATCH 0/3] Quram Qmage image decoder`](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-November/336378.html)
> - [`[FFmpeg-devel] [PATCH 1/3] avcodec/qmagedec: Quram Qmage decoder`](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-November/336379.html)
> - [`[FFmpeg-devel] [PATCH 2/3] avformat/qmagedec: Quram Qmage demuxer`](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-November/336380.html)
> - [`[FFmpeg-devel] [PATCH 3/3] fate/qmage: add tests`](https://ffmpeg.org/pipermail/ffmpeg-devel/2024-November/336381.html)
>
> I do not own nor do I take responsibility for the above commits.
> 
> If you are an FFmpeg maintainer or the original author of these commits, you may contact me at `dexrn` on Discord.
> 
> If needed, I can also be reached at `dexrn@hotmail.com`, however I do not check my emails often.

FFmpeg is a collection of libraries and tools to process multimedia content
such as audio, video, subtitles and related metadata.

## Libraries

* `libavcodec` provides implementation of a wider range of codecs.
* `libavformat` implements streaming protocols, container formats and basic I/O access.
* `libavutil` includes hashers, decompressors and miscellaneous utility functions.
* `libavfilter` provides means to alter decoded audio and video through a directed graph of connected filters.
* `libavdevice` provides an abstraction to access capture and playback devices.
* `libswresample` implements audio mixing and resampling routines.
* `libswscale` implements color conversion and scaling routines.

## Tools

* [ffmpeg](https://ffmpeg.org/ffmpeg.html) is a command line toolbox to
  manipulate, convert and stream multimedia content.
* [ffplay](https://ffmpeg.org/ffplay.html) is a minimalistic multimedia player.
* [ffprobe](https://ffmpeg.org/ffprobe.html) is a simple analysis tool to inspect
  multimedia content.
* Additional small tools such as `aviocat`, `ismindex` and `qt-faststart`.

## Documentation

The offline documentation is available in the **doc/** directory.

The online documentation is available in the main [website](https://ffmpeg.org)
and in the [wiki](https://trac.ffmpeg.org).

### Examples

Coding examples are available in the **doc/examples** directory.

## License

FFmpeg codebase is mainly LGPL-licensed with optional components licensed under
GPL. Please refer to the LICENSE file for detailed information.

## Contributing

Patches should be submitted to the ffmpeg-devel mailing list using
`git format-patch` or `git send-email`. Github pull requests should be
avoided because they are not part of our review process and will be ignored.
