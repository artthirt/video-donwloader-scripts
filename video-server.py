import os
import uuid
import subprocess
import shutil
from pathlib import Path
from flask import Flask, render_template_string, request, send_file, abort, jsonify

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 2 * 1024 * 1024 * 1024  # 2GB max file size

# Storage paths
UPLOAD_DIR = Path("uploads")
STREAM_DIR = Path("streams")
UPLOAD_DIR.mkdir(exist_ok=True)
STREAM_DIR.mkdir(exist_ok=True)

HTML_TEMPLATE = """
<!DOCTYPE html>
<html>
<head>
    <title>HLS Test Server</title>
    <style>
        body { font-family: Arial, sans-serif; max-width: 900px; margin: 50px auto; padding: 20px; background: #f5f5f5; }
        .container { background: white; padding: 30px; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
        .upload-form { margin: 20px 0; padding: 20px; background: #f9f9f9; border-radius: 5px; border: 2px dashed #ccc; }
        input[type="file"] { margin: 10px 0; }
        button { background: #4CAF50; color: white; padding: 10px 20px; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }
        button:hover { background: #45a049; }
        button:disabled { background: #ccc; cursor: not-allowed; }
        .stream-list { margin-top: 30px; }
        .stream-item { background: #f0f8ff; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #4CAF50; }
        .url-box { background: #222; color: #0f0; padding: 10px; border-radius: 4px; font-family: monospace; word-break: break-all; margin: 10px 0; }
        .copy-btn { background: #2196F3; padding: 5px 10px; font-size: 12px; margin-left: 10px; }
        .status { padding: 10px; margin: 10px 0; border-radius: 4px; display: none; }
        .status.success { background: #d4edda; color: #155724; display: block; }
        .status.error { background: #f8d7da; color: #721c24; display: block; }
        .progress { margin: 10px 0; padding: 10px; background: #e3f2fd; border-radius: 4px; display: none; }
        video { width: 100%; max-width: 600px; margin-top: 10px; border-radius: 4px; }
        .delete-btn { background: #f44336; padding: 5px 10px; font-size: 12px; float: right; }
    </style>
</head>
<body>
    <div class="container">
        <h1>🎥 HLS Test Server</h1>
        <p>Upload a video to generate an M3U8 stream for testing your downloader.</p>
        
        <div class="upload-form">
            <h3>Upload Video</h3>
            <form id="uploadForm" enctype="multipart/form-data">
                <input type="file" name="video" accept="video/*" required id="fileInput">
                <br>
                <label>
                    Segment duration: 
                    <select name="segment_time">
                        <option value="2">2 seconds (more segments)</option>
                        <option value="10" selected>10 seconds (default)</option>
                        <option value="30">30 seconds (fewer segments)</option>
                    </select>
                </label>
                <br><br>
                <button type="submit" id="uploadBtn">Upload & Convert to HLS</button>
            </form>
            <div id="progress" class="progress">Converting video... This may take a while.</div>
            <div id="status" class="status"></div>
        </div>

        <div class="stream-list">
            <h3>Available Streams</h3>
            <div id="streams">
                {{ streams_html|safe }}
            </div>
        </div>
    </div>

    <script>
        function copyToClipboard(text) {
            navigator.clipboard.writeText(text).then(() => {
                alert('URL copied to clipboard!');
            });
        }
        
        function deleteStream(streamId) {
            if (confirm('Delete this stream?')) {
                fetch('/delete/' + streamId, {method: 'POST'})
                    .then(() => location.reload());
            }
        }
        
        document.getElementById('uploadForm').onsubmit = async (e) => {
            e.preventDefault();
            const btn = document.getElementById('uploadBtn');
            const progress = document.getElementById('progress');
            const status = document.getElementById('status');
            const formData = new FormData(e.target);
            
            btn.disabled = true;
            progress.style.display = 'block';
            status.className = 'status';
            status.style.display = 'none';
            
            try {
                const response = await fetch('/upload', {
                    method: 'POST',
                    body: formData
                });
                const result = await response.json();
                
                progress.style.display = 'none';
                status.style.display = 'block';
                
                if (result.success) {
                    status.className = 'status success';
                    status.innerHTML = '✅ ' + result.message;
                    setTimeout(() => location.reload(), 1000);
                } else {
                    status.className = 'status error';
                    status.innerHTML = '❌ Error: ' + result.error;
                    btn.disabled = false;
                }
            } catch (err) {
                progress.style.display = 'none';
                status.style.display = 'block';
                status.className = 'status error';
                status.innerHTML = '❌ Error: ' + err.message;
                btn.disabled = false;
            }
        };
    </script>
</body>
</html>
"""

def get_streams_html():
    """Generate HTML list of available streams"""
    streams = []
    for stream_dir in STREAM_DIR.iterdir():
        if stream_dir.is_dir() and (stream_dir / "playlist.m3u8").exists():
            stream_id = stream_dir.name
            m3u8_url = f"http://localhost:5000/streams/{stream_id}/playlist.m3u8"
            
            # Count segments
            segments = list(stream_dir.glob("*.ts"))
            
            streams.append(f"""
            <div class="stream-item">
                <strong>Stream: {stream_id[:8]}...</strong> 
                <button class="delete-btn" onclick="deleteStream('{stream_id}')">Delete</button>
                <br>
                <small>{len(segments)} segments | M3U8 URL:</small>
                <div class="url-box">
                    {m3u8_url}
                    <button class="copy-btn" onclick="copyToClipboard('{m3u8_url}')">Copy</button>
                </div>
                <video controls>
                    <source src="{m3u8_url}" type="application/x-mpegURL">
                    Your browser does not support HLS.
                </video>
            </div>
            """)
    
    if not streams:
        return "<p>No active streams. Upload a video to create one.</p>"
    
    return "".join(streams)

@app.route('/')
def index():
    return render_template_string(HTML_TEMPLATE, streams_html=get_streams_html())

@app.route('/upload', methods=['POST'])
def upload():
    if 'video' not in request.files:
        return jsonify({"success": False, "error": "No file provided"}), 400
    
    file = request.files['video']
    if file.filename == '':
        return jsonify({"success": False, "error": "No file selected"}), 400
    
    # Generate unique ID
    stream_id = str(uuid.uuid4())
    stream_path = STREAM_DIR / stream_id
    stream_path.mkdir(exist_ok=True)
    
    # Save uploaded file
    input_path = UPLOAD_DIR / f"{stream_id}_{file.filename}"
    file.save(input_path)
    
    # Get segment duration
    segment_time = request.form.get('segment_time', '10')
    
    try:
        # Convert to HLS using ffmpeg
        output_m3u8 = stream_path / "playlist.m3u8"
        
        cmd = [
            'ffmpeg', '-y', '-i', str(input_path),
            '-codec:', 'copy',           # Copy streams (fast)
            '-start_number', '0',
            '-hls_time', segment_time,   # Segment duration
            '-hls_list_size', '0',       # Keep all segments
            '-f', 'hls',
            str(output_m3u8)
        ]
        
        # Alternative with re-encoding if codec copy fails:
        # cmd = [
        #     'ffmpeg', '-y', '-i', str(input_path),
        #     '-codec:v', 'libx264', '-preset', 'fast', '-crf', '23',
        #     '-codec:a', 'aac', '-b:a', '128k',
        #     '-start_number', '0',
        #     '-hls_time', segment_time,
        #     '-hls_list_size', '0',
        #     '-f', 'hls',
        #     str(output_m3u8)
        # ]
        
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=300  # 5 minutes timeout
        )
        
        if result.returncode != 0:
            # Cleanup on failure
            shutil.rmtree(stream_path, ignore_errors=True)
            return jsonify({
                "success": False, 
                "error": f"FFmpeg conversion failed: {result.stderr[-500:]}"
            }), 500
        
        # Cleanup original upload to save space (optional)
        # input_path.unlink()
        
        return jsonify({
            "success": True,
            "message": f"Stream created successfully with ID: {stream_id[:8]}...",
            "stream_id": stream_id,
            "url": f"http://localhost:5000/streams/{stream_id}/playlist.m3u8"
        })
        
    except subprocess.TimeoutExpired:
        shutil.rmtree(stream_path, ignore_errors=True)
        return jsonify({"success": False, "error": "Conversion timeout (5 minutes)"}), 500
    except Exception as e:
        shutil.rmtree(stream_path, ignore_errors=True)
        return jsonify({"success": False, "error": str(e)}), 500

@app.route('/streams/<stream_id>/playlist.m3u8')
def serve_m3u8(stream_id):
    """Serve the M3U8 playlist"""
    m3u8_path = STREAM_DIR / stream_id / "playlist.m3u8"
    if not m3u8_path.exists():
        abort(404)
    
    # Modify the M3U8 to use absolute URLs if needed, or ensure relative URLs work
    content = m3u8_path.read_text()
    
    return app.response_class(
        response=content,
        status=200,
        mimetype='application/vnd.apple.mpegurl'
    )

@app.route('/streams/<stream_id>/<segment>')
def serve_segment(stream_id, segment):
    """Serve TS segments"""
    segment_path = STREAM_DIR / stream_id / segment
    if not segment_path.exists():
        abort(404)
    
    return send_file(
        segment_path,
        mimetype='video/mp2t',  # Correct MIME type for TS files
        as_attachment=False
    )

@app.route('/delete/<stream_id>', methods=['POST'])
def delete_stream(stream_id):
    """Delete a stream"""
    stream_path = STREAM_DIR / stream_id
    if stream_path.exists():
        shutil.rmtree(stream_path)
    
    # Also clean up uploaded source if exists
    for f in UPLOAD_DIR.glob(f"{stream_id}_*"):
        f.unlink()
    
    return jsonify({"success": True})

if __name__ == '__main__':
    print("Starting HLS Test Server...")
    print("Open http://localhost:5000 in your browser")
    print("Make sure FFmpeg is installed and in PATH")
    app.run(host='0.0.0.0', port=5000, debug=True)