#!/usr/bin/env python3
"""Spotify 播放控制验证脚本。

读取 get_refresh_token.py 保存的 token，自动刷新 access_token，
然后提供交互式菜单验证 Player 端点：
    - 列出设备
    - 转移播放到指定设备
    - 播放/暂停
    - 下一首/上一首
    - 设置音量
    - 查看当前播放状态
    - 播放指定歌单/专辑/曲目

用法: python3 tools/verify_playback.py
"""

import json
import os
import sys
import time
from pathlib import Path

import requests

TOKEN_FILE = Path(__file__).parent / ".spotify_token.json"
CLIENT_FILE = Path(__file__).parent / ".spotify_client.json"

API_BASE = "https://api.spotify.com/v1"
TOKEN_URL = "https://accounts.spotify.com/api/token"


class SpotifyClient:
    def __init__(self):
        self._load_tokens()
        self._refresh_if_needed()

    def _load_tokens(self):
        if not TOKEN_FILE.exists() or not CLIENT_FILE.exists():
            print("未找到 token 文件。请先运行: python3 tools/get_refresh_token.py <CLIENT_ID>")
            sys.exit(1)
        with open(TOKEN_FILE) as f:
            self.tokens = json.load(f)
        with open(CLIENT_FILE) as f:
            self.client = json.load(f)
        self.access_token = self.tokens["access_token"]
        self.refresh_token = self.tokens["refresh_token"]
        self.client_id = self.client["client_id"]
        self.token_obtained_at = time.time()

    def _refresh_if_needed(self):
        elapsed = time.time() - self.token_obtained_at
        if elapsed > self.tokens.get("expires_in", 3600) - 60:
            print("access_token 已过期，正在刷新...")
            self._refresh()

    def _refresh(self):
        resp = requests.post(
            TOKEN_URL,
            data={
                "grant_type": "refresh_token",
                "refresh_token": self.refresh_token,
                "client_id": self.client_id,
            },
        )
        if resp.status_code != 200:
            print(f"刷新 token 失败: {resp.status_code} {resp.text}")
            sys.exit(1)
        data = resp.json()
        self.access_token = data["access_token"]
        self.token_obtained_at = time.time()
        if "refresh_token" in data:
            self.refresh_token = data["refresh_token"]
        self.tokens["access_token"] = self.access_token
        self.tokens["refresh_token"] = self.refresh_token
        with open(TOKEN_FILE, "w") as f:
            json.dump(self.tokens, f, indent=2)
        os.chmod(TOKEN_FILE, 0o600)
        print("token 已刷新。")

    def _headers(self):
        self._refresh_if_needed()
        return {
            "Authorization": f"Bearer {self.access_token}",
            "Content-Type": "application/json",
        }

    def _req(self, method, path, **kwargs):
        url = f"{API_BASE}{path}"
        resp = requests.request(method, url, headers=self._headers(), **kwargs)
        if resp.status_code == 429:
            retry = int(resp.headers.get("Retry-After", "1"))
            print(f"  限流，{retry}s 后重试...")
            time.sleep(retry + 1)
            resp = requests.request(method, url, headers=self._headers(), **kwargs)
        return resp

    # ---- Player endpoints ----

    def get_devices(self):
        resp = self._req("GET", "/me/player/devices")
        resp.raise_for_status()
        return resp.json()["devices"]

    def transfer_playback(self, device_id, play=False):
        resp = self._req(
            "PUT",
            "/me/player",
            json={"device_ids": [device_id], "play": play},
        )
        return resp.status_code

    def play(self, device_id=None, context_uri=None):
        body = {}
        if context_uri:
            body["context_uri"] = context_uri
        params = {"device_id": device_id} if device_id else {}
        resp = self._req("PUT", "/me/player/play", params=params, json=body if body else None)
        return resp.status_code

    def pause(self, device_id=None):
        params = {"device_id": device_id} if device_id else {}
        resp = self._req("PUT", "/me/player/pause", params=params)
        return resp.status_code

    def next_track(self, device_id=None):
        params = {"device_id": device_id} if device_id else {}
        resp = self._req("POST", "/me/player/next", params=params)
        return resp.status_code

    def previous_track(self, device_id=None):
        params = {"device_id": device_id} if device_id else {}
        resp = self._req("POST", "/me/player/previous", params=params)
        return resp.status_code

    def get_state(self):
        resp = self._req("GET", "/me/player")
        if resp.status_code == 204:
            return None
        resp.raise_for_status()
        return resp.json()

    def get_playlists(self):
        resp = self._req("GET", "/me/playlists", params={"limit": 20})
        resp.raise_for_status()
        return resp.json()["items"]


def fmt_time(ms):
    if not ms:
        return "0:00"
    s = ms // 1000
    return f"{s // 60}:{s % 60:02d}"


def print_devices(devices):
    if not devices:
        print("  没有可用设备。请确保 iPhone 上 Spotify App 已登录并联网。")
        return
    for i, d in enumerate(devices):
        active = " [活跃]" if d.get("is_active") else ""
        restricted = " [受限-不可控]" if d.get("is_restricted") else ""
        print(f"  [{i}] {d['name']} ({d['type']}){active}{restricted}")
        print(f"       id: {d['id']}")


def print_state(state):
    if not state:
        print("  当前无播放。")
        return
    item = state.get("item")
    if item:
        artists = ", ".join(a["name"] for a in item.get("artists", []))
        print(f"  曲目: {item['name']} - {artists}")
        print(f"  专辑: {item['album']['name']}")
        print(
            f"  进度: {fmt_time(state.get('progress_ms'))}/{fmt_time(item['duration_ms'])}"
        )
    dev = state.get("device")
    if dev:
        print(f"  设备: {dev['name']} ({dev['type']})")
    print(
        f"  状态: {'播放中' if state.get('is_playing') else '已暂停'}"
        f"  循环:{state.get('repeat_state', 'off')}"
        f"  随机:{'on' if state.get('shuffle_state') else 'off'}"
    )


def main():
    sp = SpotifyClient()
    current_device_id = None

    print("\n=== Spotify 播放控制验证 ===")

    while True:
        print("\n--- 菜单 ---")
        print("  1) 列出设备")
        print("  2) 转移播放到设备")
        print("  3) 播放/恢复")
        print("  4) 暂停")
        print("  5) 下一首")
        print("  6) 上一首")
        print("  7) 查看当前播放状态")
        print("  8) 列出我的歌单并播放")
        print("  0) 退出")
        choice = input("选择> ").strip()

        try:
            if choice == "1":
                devices = sp.get_devices()
                print_devices(devices)

            elif choice == "2":
                devices = sp.get_devices()
                print_devices(devices)
                if not devices:
                    continue
                idx = input("输入设备序号: ").strip()
                d = devices[int(idx)]
                if d.get("is_restricted"):
                    print(f"  警告: 设备 {d['name']} 受限，Web API 无法控制。")
                code = sp.transfer_playback(d["id"], play=False)
                print(f"  转移结果: HTTP {code}")
                current_device_id = d["id"]
                print(f"  当前设备: {d['name']}")

            elif choice == "3":
                code = sp.play(device_id=current_device_id)
                print(f"  播放结果: HTTP {code}")

            elif choice == "4":
                code = sp.pause(device_id=current_device_id)
                print(f"  暂停结果: HTTP {code}")

            elif choice == "5":
                code = sp.next_track(device_id=current_device_id)
                print(f"  下一首结果: HTTP {code}")
                time.sleep(1)
                print_state(sp.get_state())

            elif choice == "6":
                code = sp.previous_track(device_id=current_device_id)
                print(f"  上一首结果: HTTP {code}")
                time.sleep(1)
                print_state(sp.get_state())

            elif choice == "7":
                print_state(sp.get_state())

            elif choice == "8":
                playlists = sp.get_playlists()
                if not playlists:
                    print("  没有歌单。")
                    continue
                for i, p in enumerate(playlists):
                    print(f"  [{i}] {p['name']}  ({p.get('tracks', {}).get('total', '?')} 首)")
                idx = input("输入歌单序号播放 (回车取消): ").strip()
                if not idx:
                    continue
                uri = playlists[int(idx)]["uri"]
                code = sp.play(device_id=current_device_id, context_uri=uri)
                print(f"  播放歌单结果: HTTP {code}")
                time.sleep(1)
                print_state(sp.get_state())

            elif choice == "0":
                print("退出。")
                break
            else:
                print("无效选择。")
        except (ValueError, IndexError) as e:
            print(f"  输入错误: {e}")
        except requests.HTTPError as e:
            print(f"  API 错误: {e.response.status_code} {e.response.text}")
        except Exception as e:
            print(f"  错误: {e}")


if __name__ == "__main__":
    main()
