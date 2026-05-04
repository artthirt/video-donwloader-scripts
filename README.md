## Downloading m3u8 to a mp4 container

###  build stand-alone app

```cmd
python -m nuitka --standalone --onefile --enable-plugin=pyside6 --windows-disable-console  --include-data-files=ffmpeg.exe=ffmpeg.exe --include-windows-runtime-dlls=yes --output-dir=build video-downloader.py
```
