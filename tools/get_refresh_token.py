#!/usr/bin/env python3
"""Spotify PKCE 授权流程：获取 refresh_token 并保存。

用法:
    python3 tools/get_refresh_token.py <CLIENT_ID> [REDIRECT_URI]

REDIRECT_URI 默认 http://127.0.0.1:8888/callback
需在 Spotify Dashboard 中配置相同的 Redirect URI。

流程:
    1. 生成 PKCE code_verifier / code_challenge
    2. 打开浏览器让用户登录 Spotify 授权
    3. 本地起 HTTP 服务接收回调 code
    4. 用 code 换 access_token + refresh_token
    5. 保存到 tools/.spotify_token.json 和 tools/.spotify_client.json
"""

import base64
import hashlib
import http.server
import json
import os
import secrets
import sys
import threading
import urllib.parse
import webbrowser
from pathlib import Path

import requests

SCOPES = [
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing",
    "playlist-read-private",
]

TOKEN_FILE = Path(__file__).parent / ".spotify_token.json"
CLIENT_FILE = Path(__file__).parent / ".spotify_client.json"

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"


def generate_pkce():
    verifier = base64.urlsafe_b64encode(secrets.token_bytes(64)).decode().rstrip("=")
    challenge = base64.urlsafe_b64encode(
        hashlib.sha256(verifier.encode()).digest()
    ).decode().rstrip("=")
    return verifier, challenge


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    captured_code = None
    captured_error = None

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        params = urllib.parse.parse_qs(parsed.query)

        if "code" in params:
            CallbackHandler.captured_code = params["code"][0]
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(
                "<html><body><h2>OK</h2>"
                "<p>授权成功，可以关闭此页面回到终端。</p></body></html>".encode("utf-8")
            )
        elif "error" in params:
            CallbackHandler.captured_error = params["error"][0]
            self.send_response(400)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.end_headers()
            self.wfile.write(f"<p>Error: {CallbackHandler.captured_error}</p>".encode())
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, *args):
        pass


def main():
    if len(sys.argv) < 2:
        print(f"用法: python3 {sys.argv[0]} <CLIENT_ID> [REDIRECT_URI]")
        sys.exit(1)

    client_id = sys.argv[1]
    redirect_uri = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:8888/callback"
    port = urllib.parse.urlparse(redirect_uri).port or 8888

    verifier, challenge = generate_pkce()

    params = {
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri,
        "code_challenge_method": "S256",
        "code_challenge": challenge,
        "scope": " ".join(SCOPES),
    }
    auth_url = f"{AUTH_URL}?{urllib.parse.urlencode(params)}"

    print("\n=== Spotify PKCE 授权 ===")
    print(f"Redirect URI : {redirect_uri}")
    print(f"Scopes       : {' '.join(SCOPES)}")
    print(f"\n正在打开浏览器，请在浏览器中登录 Spotify 并授权...")
    print(f"如果浏览器没有自动打开，请手动访问:\n{auth_url}\n")

    webbrowser.open(auth_url)

    server = http.server.HTTPServer(("127.0.0.1", port), CallbackHandler)
    server.timeout = 300

    print(f"等待回调 (端口 {port}，超时 5 分钟)...")
    server.handle_request()

    if CallbackHandler.captured_error:
        print(f"授权失败: {CallbackHandler.captured_error}")
        sys.exit(1)
    if not CallbackHandler.captured_code:
        print("未收到授权码，超时或用户取消。")
        sys.exit(1)

    code = CallbackHandler.captured_code
    print("收到授权码，正在换取 token...")

    resp = requests.post(
        TOKEN_URL,
        data={
            "client_id": client_id,
            "grant_type": "authorization_code",
            "code": code,
            "redirect_uri": redirect_uri,
            "code_verifier": verifier,
        },
        headers={"Content-Type": "application/x-www-form-urlencoded"},
    )

    if resp.status_code != 200:
        print(f"换取 token 失败: {resp.status_code}")
        print(resp.text)
        sys.exit(1)

    token_data = resp.json()
    print("\n授权成功!")
    print(f"  access_token  : {token_data['access_token'][:30]}...")
    print(f"  refresh_token: {token_data['refresh_token'][:30]}...")
    print(f"  expires_in   : {token_data['expires_in']}s")

    with open(TOKEN_FILE, "w") as f:
        json.dump(
            {
                "access_token": token_data["access_token"],
                "refresh_token": token_data["refresh_token"],
                "expires_in": token_data["expires_in"],
            },
            f,
            indent=2,
        )
    os.chmod(TOKEN_FILE, 0o600)

    with open(CLIENT_FILE, "w") as f:
        json.dump({"client_id": client_id, "redirect_uri": redirect_uri}, f, indent=2)
    os.chmod(CLIENT_FILE, 0o600)

    print(f"\n已保存:")
    print(f"  {TOKEN_FILE}")
    print(f"  {CLIENT_FILE}")
    print("\n下一步: python3 tools/verify_playback.py")


if __name__ == "__main__":
    main()
