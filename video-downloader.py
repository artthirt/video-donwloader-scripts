import sys
import subprocess
import re
import os
import shlex
import threading
import queue
from pathlib import Path
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QLineEdit, QPushButton, QProgressBar, QPlainTextEdit,
    QFileDialog, QMessageBox, QGroupBox, QCheckBox, QSpinBox,
    QComboBox, QStatusBar, QSizePolicy, QToolButton
)
from PyQt6.QtCore import QThread, pyqtSignal, Qt, QSettings
from PyQt6.QtGui import QFont, QPalette, QColor, QCloseEvent

class ComboWithPlaceholder(QComboBox):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setEditable(True)
    
    def setPlaceholderText(self, text: str):
        """Override to set placeholder on the internal line edit"""
        if self.lineEdit():
            self.lineEdit().setPlaceholderText(text)
        else:
            # Fallback for non-editable mode
            super().setPlaceholderText(text)

class FFmpegWorker(QThread):
    progress = pyqtSignal(int)
    status = pyqtSignal(str)
    log = pyqtSignal(str)
    finished = pyqtSignal(bool, str)
    
    def __init__(self, cmd_args, parent=None):
        super().__init__(parent)
        self.cmd_args = cmd_args
        self.process = None
        self._is_running = True
        self.duration_seconds = 0
        self.duration_found = False
        
    def time_to_seconds(self, time_str):
        try:
            parts = time_str.split(':')
            hours = float(parts[0])
            minutes = float(parts[1])
            seconds = float(parts[2])
            return hours * 3600 + minutes * 60 + seconds
        except:
            return 0
    
    def stop(self):
        self._is_running = False
        if self.process:
            try:
                self.process.terminate()
                self.process.wait(timeout=3)
            except:
                self.process.kill()
    
    def parse_line(self, line):
        """Parse a single line of FFmpeg output"""
        # Log the line
        self.log.emit(line + '\n')
        
        # Parse duration (only once)
        if not self.duration_found:
            duration_match = re.search(r'Duration: (\d{2}):(\d{2}):(\d{2}\.\d{2})', line)
            if duration_match:
                time_str = f"{duration_match.group(1)}:{duration_match.group(2)}:{duration_match.group(3)}"
                self.duration_seconds = self.time_to_seconds(time_str)
                self.duration_found = True
                self.status.emit(f"Duration: {time_str} ({int(self.duration_seconds)}s)")
        
        # Parse progress
        if self.duration_seconds > 0:
            time_match = re.search(r'time=(\d{2}):(\d{2}):(\d{2}\.\d{2})', line)
            if time_match:
                current_time_str = f"{time_match.group(1)}:{time_match.group(2)}:{time_match.group(3)}"
                current_seconds = self.time_to_seconds(current_time_str)
                percent = min(int((current_seconds / self.duration_seconds) * 100), 99)
                self.progress.emit(percent)
                self.status.emit(f"Downloading: {percent}% ({current_time_str})")
        else:
            # For live streams, show frame count or size
            frame_match = re.search(r'frame=\s*(\d+)', line)
            size_match = re.search(r'size=\s*(\d+)kB', line)
            fps_match = re.search(r'fps=\s*(\d+)', line)
            if frame_match or size_match:
                info = []
                if frame_match:
                    info.append(f"Frame {frame_match.group(1)}")
                if fps_match:
                    info.append(f"{fps_match.group(1)}fps")
                if size_match:
                    info.append(f"{int(size_match.group(1))/1024:.1f}MB")
                self.status.emit(f"Downloading... {' | '.join(info)}")
    
    def run(self):
        self.status.emit("Starting FFmpeg...")
        
        try:
            # CRITICAL FIX: Use subprocess with unbuffered output
            # -stderr=subprocess.STDOUT merges stderr into stdout
            # -bufsize=1 enables line buffering
            # -universal_newlines=True for text mode
            
            cmd = ['ffmpeg'] + self.cmd_args
            
            self.log.emit(f"Executing: {' '.join(cmd)}\n")
            self.log.emit("-" * 50 + "\n")
            
            # Use CREATE_NO_WINDOW on Windows to prevent console popup
            creationflags = 0
            if sys.platform == 'win32':
                creationflags = subprocess.CREATE_NO_WINDOW
            
            self.process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,  # Merge stderr into stdout
                bufsize=1,  # Line buffered
                universal_newlines=True,
                creationflags=creationflags
            )
            
            # Read output line by line
            for line in self.process.stdout:
                if not self._is_running:
                    self.process.terminate()
                    self.finished.emit(False, "Download cancelled by user")
                    return
                
                line = line.rstrip()
                if line:
                    self.parse_line(line)
            
            # Wait for process to complete
            self.process.wait()
            
            if self.process.returncode == 0:
                self.progress.emit(100)
                self.finished.emit(True, "Download completed successfully!")
            else:
                self.finished.emit(False, f"FFmpeg exited with code {self.process.returncode}")
                
        except Exception as e:
            self.finished.emit(False, f"Error: {str(e)}")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("M3U8 to MP4 Downloader")
        self.setMinimumSize(900, 700)
        self.worker = None
        
        self.setup_ui()
        self.check_ffmpeg()

    def closeEvent(self, event: QCloseEvent):
        self.saveSettings()
        event.accept()

    def loadSettings(self):
        settings = QSettings()
        listUrl = settings.value("list_url")
        self.url_input.addItems(listUrl)
        #if len(listUrl) > 0:
        #    self.url_input.setCurrentIndex(0)

    def saveSettings(self):
        settings = QSettings()
        listUrls = []
        for x in range(self.url_input.count()):
            listUrls.append(self.url_input.itemText(x))
        settings.setValue("list_url", listUrls)

    def addUrl(self, url):
        listUrls = []
        for x in range(self.url_input.count()):
            listUrls.append(self.url_input.itemText(x))
        if( url in listUrls):
            return
        listUrls.append(url)
        self.url_input.clear()
        self.url_input.addItems(listUrls)     

    def clear_url(self):
        self.url_input.setCurrentText("")   
        
    def setup_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(10)
        main_layout.setContentsMargins(15, 15, 15, 15)
       
        # Input Section
        input_group = QGroupBox("Source & Destination")
        input_layout = QVBoxLayout(input_group)
        
        url_layout = QHBoxLayout()
        url_layout.addWidget(QLabel("M3U8 URL:"))
        # self.url_input = QLineEdit()
        # self.url_input.setPlaceholderText("https://example.com/playlist.m3u8")
        # self.url_input.textChanged.connect(self.suggest_filename)
        self.url_input = ComboWithPlaceholder()
        self.url_input.setEditable(True)
        self.url_input.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed)
        self.url_input.setPlaceholderText("https://example.com/playlist.m3u8")
        self.url_input.currentTextChanged.connect(self.suggest_filename)
        url_layout.addWidget(self.url_input)
        
        self.clear_btn = QToolButton()
        self.clear_btn.setText("❌")
        self.clear_btn.setToolTip("Clear url")
        self.clear_btn.clicked.connect(self.clear_url)
        url_layout.addWidget(self.clear_btn)

        self.btn_paste = QPushButton("Paste & Auto-name")
        self.btn_paste.clicked.connect(self.paste_and_suggest)
        url_layout.addWidget(self.btn_paste)
        input_layout.addLayout(url_layout)
        
        out_layout = QHBoxLayout()
        out_layout.addWidget(QLabel("Output File:"))
        self.output_input = QLineEdit()
        self.output_input.setPlaceholderText("output.mp4")
        out_layout.addWidget(self.output_input)
        
        self.btn_browse = QPushButton("Browse...")
        self.btn_browse.clicked.connect(self.browse_output)
        out_layout.addWidget(self.btn_browse)
        input_layout.addLayout(out_layout)
        
        main_layout.addWidget(input_group)
        
        # Options Section
        options_group = QGroupBox("Encoding Options")
        options_layout = QVBoxLayout(options_group)
        
        codec_layout = QHBoxLayout()
        
        self.copy_checkbox = QCheckBox("Copy streams (-c copy) - Fast, no re-encoding")
        self.copy_checkbox.setChecked(True)
        self.copy_checkbox.stateChanged.connect(self.toggle_encoding_options)
        codec_layout.addWidget(self.copy_checkbox)
        
        codec_layout.addWidget(QLabel("Audio Filter:"))
        self.audio_filter = QComboBox()
        self.audio_filter.addItems(["aac_adtstoasc (default)", "none"])
        self.audio_filter.setEnabled(True)
        codec_layout.addWidget(self.audio_filter)
        
        codec_layout.addStretch()
        options_layout.addLayout(codec_layout)
        
        quality_layout = QHBoxLayout()
        quality_layout.addWidget(QLabel("Quality (CRF):"))
        self.crf_spinbox = QSpinBox()
        self.crf_spinbox.setRange(0, 51)
        self.crf_spinbox.setValue(23)
        self.crf_spinbox.setToolTip("0=lossless, 23=default, 51=worst")
        self.crf_spinbox.setEnabled(False)
        quality_layout.addWidget(self.crf_spinbox)
        
        quality_layout.addWidget(QLabel("Preset:"))
        self.preset_combo = QComboBox()
        self.preset_combo.addItems(["ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"])
        self.preset_combo.setCurrentText("medium")
        self.preset_combo.setEnabled(False)
        quality_layout.addWidget(self.preset_combo)
        
        quality_layout.addStretch()
        options_layout.addLayout(quality_layout)
        
        extra_layout = QHBoxLayout()
        extra_layout.addWidget(QLabel("Extra Args:"))
        self.extra_args = QLineEdit()
        self.extra_args.setPlaceholderText("-bsf:a aac_adtstoasc -vf scale=1920:1080")
        extra_layout.addWidget(self.extra_args)
        options_layout.addLayout(extra_layout)
        
        main_layout.addWidget(options_group)
        
        # Progress Section
        self.progress_bar = QProgressBar()
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setTextVisible(True)
        self.progress_bar.setValue(0)
        main_layout.addWidget(self.progress_bar)
        
        # Log Section
        self.log_output = QPlainTextEdit()
        self.log_output.setMaximumBlockCount(1000)
        self.log_output.setFont(QFont("Consolas", 9))
        self.log_output.setPlaceholderText("FFmpeg output will appear here...")
        main_layout.addWidget(self.log_output, stretch=1)
        
        # Controls
        control_layout = QHBoxLayout()
        
        self.btn_start = QPushButton("▶ Start Download")
        self.btn_start.setStyleSheet("background-color: #4CAF50; color: white; font-weight: bold; padding: 8px;")
        self.btn_start.clicked.connect(self.start_download)
        control_layout.addWidget(self.btn_start)
        
        self.btn_cancel = QPushButton("⏹ Cancel")
        self.btn_cancel.setEnabled(False)
        self.btn_cancel.setStyleSheet("background-color: #f44336; color: white; font-weight: bold; padding: 8px;")
        self.btn_cancel.clicked.connect(self.cancel_download)
        control_layout.addWidget(self.btn_cancel)
        
        self.btn_clear = QPushButton("Clear Log")
        self.btn_clear.clicked.connect(self.log_output.clear)
        control_layout.addWidget(self.btn_clear)
        
        control_layout.addStretch()
        main_layout.addLayout(control_layout)
        
        self.status_bar = QStatusBar()
        self.setStatusBar(self.status_bar)
        self.status_bar.showMessage("Ready")

        self.loadSettings()
        
    def toggle_encoding_options(self, state):
        copy_enabled = state == Qt.CheckState.Checked.value
        self.crf_spinbox.setEnabled(not copy_enabled)
        self.preset_combo.setEnabled(not copy_enabled)
        self.audio_filter.setEnabled(copy_enabled)
        
    def suggest_filename(self, text):
        url = self.url_input.currentText().strip()
        if not url:
            return
            
        try:
            clean_url = url.split('?')[0]
            path = Path(clean_url)
            name = path.stem
            
            if name and name not in ['index', 'playlist', 'master', 'stream']:
                suggested = f"{name}.mp4"
                if not self.output_input.text():
                    self.output_input.setText(suggested)
        except:
            pass
    
    def paste_and_suggest(self):
        clipboard = QApplication.clipboard()
        text = clipboard.text()
        if text:
            self.url_input.setText(text)
            self.suggest_filename()
            
    def browse_output(self):
        file_path, _ = QFileDialog.getSaveFileName(
            self, "Save MP4 File", "", "MP4 Files (*.mp4);;All Files (*)"
        )
        if file_path:
            if not file_path.endswith('.mp4'):
                file_path += '.mp4'
            self.output_input.setText(file_path)
    
    def check_ffmpeg(self):
        try:
            result = subprocess.run(['ffmpeg', '-version'], capture_output=True, text=True, creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == 'win32' else 0)
            version_line = result.stdout.split('\n')[0]
            self.status_bar.showMessage(f"FFmpeg detected: {version_line[:50]}...")
        except FileNotFoundError:
            QMessageBox.critical(
                self, "FFmpeg Not Found",
                "FFmpeg is not installed or not in PATH.\n\n"
                "Please install FFmpeg first:\n"
                "• Windows: Download from ffmpeg.org and add to PATH\n"
                "• macOS: brew install ffmpeg\n"
                "• Linux: sudo apt install ffmpeg"
            )
    
    def build_command(self):
        url = self.url_input.currentText().strip()
        output = self.output_input.text().strip() or "output.mp4"
        
        if not url:
            raise ValueError("Please enter a valid M3U8 URL")
        
        self.addUrl(url)
        self.saveSettings()
        
        # Build command
        cmd = ['-hide_banner', '-nostdin', '-stats']  # -stats forces progress output
        cmd.extend(['-i', url])
        
        if self.copy_checkbox.isChecked():
            cmd.extend(['-c', 'copy'])
            if self.audio_filter.currentText().startswith('aac_adtstoasc'):
                cmd.extend(['-bsf:a', 'aac_adtstoasc'])
        else:
            cmd.extend(['-c:v', 'libx264'])
            cmd.extend(['-crf', str(self.crf_spinbox.value())])
            cmd.extend(['-preset', self.preset_combo.currentText()])
            cmd.extend(['-c:a', 'aac', '-b:a', '192k'])
        
        # Add extra arguments
        extra = self.extra_args.text().strip()
        if extra:
            try:
                extra_list = shlex.split(extra)
                cmd.extend(extra_list)
            except ValueError as e:
                raise ValueError(f"Invalid extra arguments: {e}")
        
        cmd.extend(['-y', '-progress', 'pipe:1'])  # Output progress to stdout
        cmd.append(output)
        
        return cmd
    

    
    def start_download(self):
        try:
            cmd_args = self.build_command()
            self.log_output.clear()
            self.log_output.appendPlainText(f"Command: ffmpeg {' '.join(cmd_args)}\n")
            self.log_output.appendPlainText("Starting FFmpeg process...\n")
            
            self.progress_bar.setValue(0)
            self.btn_start.setEnabled(False)
            self.btn_cancel.setEnabled(True)
            self.url_input.setEnabled(False)
            self.output_input.setEnabled(False)
            
            self.worker = FFmpegWorker(cmd_args)
            self.worker.progress.connect(self.progress_bar.setValue)
            self.worker.status.connect(self.status_bar.showMessage)
            self.worker.log.connect(self.append_log)
            self.worker.finished.connect(self.download_finished)
            self.worker.start()
            
        except ValueError as e:
            QMessageBox.warning(self, "Input Error", str(e))
        except Exception as e:
            QMessageBox.critical(self, "Error", str(e))
    
    def append_log(self, text):
        self.log_output.insertPlainText(text)
        scrollbar = self.log_output.verticalScrollBar()
        scrollbar.setValue(scrollbar.maximum())
    
    def cancel_download(self):
        if self.worker:
            self.worker.stop()
            self.status_bar.showMessage("Cancelling...")
    
    def download_finished(self, success, message):
        self.btn_start.setEnabled(True)
        self.btn_cancel.setEnabled(False)
        self.url_input.setEnabled(True)
        self.output_input.setEnabled(True)
        
        if success:
            self.progress_bar.setValue(100)
            QMessageBox.information(self, "Success", message)
        else:
            self.progress_bar.setValue(0)
            QMessageBox.warning(self, "Download Status", message)
        
        self.status_bar.showMessage(message)


if __name__ == '__main__':
    app = QApplication(sys.argv)

    app.setOrganizationName("VideoTools")
    app.setApplicationName("MP4 Downloader")
    
    # Optional dark theme
    app.setStyle('Fusion')
    palette = QPalette()
    palette.setColor(QPalette.ColorRole.Window, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.WindowText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Base, QColor(25, 25, 25))
    palette.setColor(QPalette.ColorRole.AlternateBase, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.ToolTipBase, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.ToolTipText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Text, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.Button, QColor(53, 53, 53))
    palette.setColor(QPalette.ColorRole.ButtonText, Qt.GlobalColor.white)
    palette.setColor(QPalette.ColorRole.BrightText, Qt.GlobalColor.red)
    palette.setColor(QPalette.ColorRole.Highlight, QColor(142, 45, 197).lighter())
    palette.setColor(QPalette.ColorRole.HighlightedText, Qt.GlobalColor.black)
    app.setPalette(palette)
    
    window = MainWindow()
    window.show()
    sys.exit(app.exec())