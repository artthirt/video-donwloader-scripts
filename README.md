## Downloading m3u8 to a mp4 container

###  build stand-alone app

```cmd
# put the stand-alone ffmpeg.exe to the directory
pip install nuitka
pip install pyside6
python -m nuitka --standalone --onefile --enable-plugin=pyside6 --windows-disable-console  --include-data-files=ffmpeg.exe=ffmpeg.exe --include-windows-runtime-dlls=yes --output-dir=build video-downloader.py
```
