Import("env")
import os
import socket

# OTA環境の場合のみ実行
if env.get("UPLOAD_PROTOCOL") == "espota":
    
    def before_upload(source, target, env):
        upload_port = env.get("UPLOAD_PORT")
        
        # ホスト名（.local）の場合、IPアドレスを解決
        if upload_port and upload_port.endswith(".local"):
            hostname = upload_port.replace(".local", "")
            try:
                # mDNSを使用してIPアドレスを解決
                ip_address = socket.gethostbyname(upload_port)
                print(f"Resolved {upload_port} to {ip_address}")
                env["UPLOAD_PORT"] = ip_address
            except socket.gaierror:
                print(f"Warning: Could not resolve {upload_port}")
                print("Trying with hostname directly...")
        
        # デバイスの接続確認
        if upload_port:
            print(f"Uploading to: {env.get('UPLOAD_PORT')}")
            print(f"OTA Port: {env.get('UPLOAD_FLAGS', ['--port=3232'])[0]}")
            
            # Ping確認（オプション）
            import subprocess
            try:
                ip = env.get("UPLOAD_PORT")
                if not ip.endswith(".local"):
                    result = subprocess.run(["ping", "-n" if os.name == "nt" else "-c", "1", ip], 
                                          capture_output=True, text=True, timeout=2)
                    if result.returncode == 0:
                        print(f"✓ Device {ip} is reachable")
                    else:
                        print(f"✗ Device {ip} is not reachable")
            except Exception as e:
                print(f"Could not ping device: {e}")
    
    env.AddPreAction("upload", before_upload)

# ビルド前のチェック
def before_build(source, target, env):
    # OTA使用時の注意事項を表示
    if env.get("UPLOAD_PROTOCOL") == "espota":
        print("=" * 50)
        print("OTA Upload Mode")
        print("=" * 50)
        print("Ensure that:")
        print("1. Device is powered on and connected to WiFi")
        print("2. Same network as your computer")
        print("3. ArduinoOTA.handle() is called in loop()")
        print("4. No firewall blocking port 3232")
        print("=" * 50)

env.AddPreAction("buildprog", before_build)