#!/usr/bin/env python3
"""
OTA Firmware Server
Serves firmware binaries and generates manifest.json for OTA client devices.
"""

import os
import sys
import json
import hashlib
import threading
from flask import Flask, request, redirect, url_for, send_file, jsonify, render_template
from werkzeug.utils import secure_filename

# --- Configuration ---
app = Flask(__name__)

CONFIG_FILE = 'config.json'
DEFAULT_CONFIG = {
    "host": "0.0.0.0",
    "port": 8080,
    "firmware_dirs": ["./firmware"],
    "manifest_path": "/manifest.json"
}

# --- Load / Save config ---
def load_config():
    if os.path.exists(CONFIG_FILE):
        try:
            with open(CONFIG_FILE, 'r') as f:
                cfg = json.load(f)
                merged = DEFAULT_CONFIG.copy()
                merged.update(cfg)
                # Ensure firmware_dirs is a list
                if not isinstance(merged["firmware_dirs"], list):
                    merged["firmware_dirs"] = [merged["firmware_dirs"]]
                return merged
        except Exception as e:
            print(f"Error loading config: {e}")
            return DEFAULT_CONFIG.copy()
    return DEFAULT_CONFIG.copy()

def save_config(cfg):
    """Save config dict to file (only known keys)."""
    to_save = {
        "host": cfg.get("host", DEFAULT_CONFIG["host"]),
        "port": cfg.get("port", DEFAULT_CONFIG["port"]),
        "firmware_dirs": cfg.get("firmware_dirs", DEFAULT_CONFIG["firmware_dirs"]),
        "manifest_path": cfg.get("manifest_path", DEFAULT_CONFIG["manifest_path"])
    }
    try:
        with open(CONFIG_FILE, 'w') as f:
            json.dump(to_save, f, indent=4)
        return True
    except Exception as e:
        print(f"Error saving config: {e}")
        return False

config = load_config()

# Ensure all firmware dirs exist
for d in config["firmware_dirs"]:
    os.makedirs(d, exist_ok=True)

# --- Utility Functions ---

def calculate_md5(filepath):
    """Calculates the MD5 hash of a given file."""
    if not os.path.exists(filepath):
        return None
    md5 = hashlib.md5()
    try:
        with open(filepath, 'rb') as f:
            for chunk in iter(lambda: f.read(65536), b""):
                md5.update(chunk)
        return md5.hexdigest()
    except Exception as e:
        print(f"Error calculating MD5 for {filepath}: {e}")
        return None

def get_file_type(filename):
    """Determine file type from filename: 'firmware' or 'filesystem'."""
    if "_fs-" in filename:
        return "filesystem"
    return "firmware"

def scan_firmware_files():
    """Scan all firmware directories and return list of file entries."""
    entries = []
    for dir_path in config["firmware_dirs"]:
        if not os.path.exists(dir_path):
            continue
        for fname in os.listdir(dir_path):
            if not fname.endswith('.bin'):
                continue
            fpath = os.path.join(dir_path, fname)
            if not os.path.isfile(fpath):
                continue
            
            size = os.path.getsize(fpath)
            md5 = calculate_md5(fpath)
            
            entries.append({
                "name": fname,
                "type": get_file_type(fname),
                "size": size,
                "md5": md5 or "",
                "dir": os.path.abspath(dir_path)  # source directory for display
            })
    
    return entries

def get_file_status():
    """Get status info for admin interface."""
    entries = scan_firmware_files()
    total_files = len(entries)
    total_size = sum(e["size"] for e in entries)
    return {
        "total_files": total_files,
        "total_size": f"{total_size / 1024:.2f} KB" if total_size > 0 else "0 KB",
        "files": entries
    }

def find_file_in_dirs(filename):
    """Search for a file across all firmware dirs, return full path or None."""
    safe_name = secure_filename(filename)
    for dir_path in config["firmware_dirs"]:
        candidate = os.path.join(dir_path, safe_name)
        if os.path.exists(candidate):
            return candidate
    return None

# --- Flask Routes ---

@app.route('/', methods=['GET'])
def index():
    """Admin interface."""
    status = get_file_status()
    message = request.args.get('message')
    return render_template('index.html',
        status=status,
        firmware_dirs=config["firmware_dirs"],
        message=message
    )

@app.route('/upload', methods=['POST'])
def upload_firmware():
    """Handle file upload. Uploads to the first directory in firmware_dirs."""
    if 'file' not in request.files:
        return redirect(url_for('index', message='Error: No file part in the request.'))
    
    file = request.files['file']
    if file.filename == '':
        return redirect(url_for('index', message='Error: No selected file.'))
    
    if file:
        filename = secure_filename(file.filename)
        if not filename.endswith('.bin'):
            return redirect(url_for('index', message='Error: Only .bin files are allowed.'))
        
        # Upload to first dir in list
        target_dir = config["firmware_dirs"][0] if config["firmware_dirs"] else "./firmware"
        os.makedirs(target_dir, exist_ok=True)
        filepath = os.path.join(target_dir, filename)
        try:
            file.save(filepath)
            md5 = calculate_md5(filepath)
            return redirect(url_for('index',
                message=f"Success! Uploaded '{filename}' to {target_dir} (MD5: {md5})"))
        except Exception as e:
            return redirect(url_for('index', message=f"Upload Failed: {str(e)}"))

@app.route('/delete/<filename>', methods=['POST'])
def delete_firmware(filename):
    """Delete a firmware file from whichever directory it's in."""
    safe_name = secure_filename(filename)
    filepath = find_file_in_dirs(safe_name)
    try:
        if filepath and os.path.exists(filepath):
            os.remove(filepath)
            return redirect(url_for('index', message=f"Deleted '{safe_name}'"))
        return redirect(url_for('index', message=f"Error: File '{safe_name}' not found."))
    except Exception as e:
        return redirect(url_for('index', message=f"Delete Failed: {str(e)}"))

@app.route('/manifest.json')
def manifest():
    """Generate manifest.json for OTA client devices."""
    entries = scan_firmware_files()
    # Strip internal 'dir' field from manifest
    clean = []
    for e in entries:
        clean.append({
            "name": e["name"],
            "type": e["type"],
            "size": e["size"],
            "md5": e["md5"]
        })
    return jsonify({"files": clean})

@app.route('/firmware/<path:filename>')
def download_firmware(filename):
    """Download a firmware binary from any firmware dir."""
    safe_name = secure_filename(filename)
    filepath = find_file_in_dirs(safe_name)
    if not filepath:
        return jsonify({"error": "File not found"}), 404
    
    return send_file(
        filepath,
        mimetype='application/octet-stream',
        as_attachment=True,
        download_name=safe_name
    )

# --- Config editing routes ---

@app.route('/config', methods=['GET'])
def get_config():
    """Return current config as JSON."""
    return jsonify({
        "host": config["host"],
        "port": config["port"],
        "firmware_dirs": config["firmware_dirs"],
        "manifest_path": config["manifest_path"]
    })

@app.route('/config/dirs', methods=['POST'])
def update_dirs():
    """Update firmware_dirs list from JSON body."""
    try:
        data = request.get_json(force=True)
        if not data or "firmware_dirs" not in data:
            return jsonify({"success": False, "error": "Missing firmware_dirs"}), 400
        
        new_dirs = data["firmware_dirs"]
        if not isinstance(new_dirs, list) or len(new_dirs) == 0:
            return jsonify({"success": False, "error": "firmware_dirs must be a non-empty list"}), 400
        
        # Create dirs if needed
        for d in new_dirs:
            os.makedirs(d, exist_ok=True)
        
        config["firmware_dirs"] = new_dirs
        save_config(config)
        
        return jsonify({"success": True, "firmware_dirs": config["firmware_dirs"]})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

# --- Browse Folder (system dialog) ---

def _browse_folder_dialog():
    """
    Open system folder picker dialog.
    Uses tkinter if available.
    Returns selected folder path or None.
    """
    # Try tkinter first
    try:
        import tkinter as tk
        from tkinter import filedialog
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)
        folder = filedialog.askdirectory(title="Select Firmware Directory")
        root.destroy()
        if folder:
            return os.path.normpath(folder)
        return None
    except Exception as e:
        print(f"tkinter folder dialog failed: {e}")
        pass

    return None


@app.route('/browse-folder', methods=['POST'])
def browse_folder():
    """Open system folder picker dialog and return selected path."""
    try:
        folder = _browse_folder_dialog()
        if folder:
            return jsonify({"success": True, "path": folder})
        return jsonify({"success": False, "error": "No folder selected."})
    except Exception as e:
        return jsonify({"success": False, "error": str(e)}), 500

# --- Main ---
if __name__ == '__main__':
    print(f"--- OTA Firmware Server ---")
    print(f"Admin Interface: http://{config['host']}:{config['port']}/")
    print(f"Manifest:        http://{config['host']}:{config['port']}{config['manifest_path']}")
    print(f"Firmware dirs:   {config['firmware_dirs']}")
    print(f"Firmware files:  {len(scan_firmware_files())}")
    
    app.run(host=config['host'], port=config['port'], debug=True)
