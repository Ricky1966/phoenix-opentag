"""Authenticated writer commands executed by the existing serial owner, never a second opener."""

from __future__ import annotations

import concurrent.futures
import queue
import re
import secrets
import threading
import time
from pathlib import Path


class BridgeError(ValueError):
    pass


class WriterBridge:
    def __init__(self, daemon_lock, token, backup_dir):
        self.daemon_lock = daemon_lock
        self.token = token
        self.backup_dir = Path(backup_dir)
        self.busy = False
        self.online = False
        self.session = None
        self.guard = threading.Lock()
        self.jobs = queue.Queue(maxsize=1)

    def _io(self, operation):
        future = concurrent.futures.Future()
        try:
            self.jobs.put_nowait((future, operation))
        except queue.Full as exc:
            raise BridgeError("Reader queue busy; wait before another request") from exc
        try:
            return future.result(timeout=40)
        except concurrent.futures.TimeoutError as exc:
            future.cancel()  # queued but unstarted commits must never run later
            raise BridgeError(
                "Reader timeout; outcome unknown. Do not retry the commit."
            ) from exc

    def tick(self, fd):
        """Only called from the serial reader thread; holds exclusive ownership of fd."""
        try:
            future, operation = self.jobs.get_nowait()
        except queue.Empty:
            return False
        if future.set_running_or_notify_cancel():
            try:
                from phoenix_opentag_write import SerialWriter

                future.set_result(operation(SerialWriter(fd)))
            except Exception as exc:
                future.set_exception(exc)
        return True

    def command(self, action, body):
        if not self.guard.acquire(blocking=False):
            raise BridgeError("Writer busy")
        try:
            if action == "reset":
                self._io(lambda wire: wire.request("write-cancel"))
                self.session = None
                with self.daemon_lock:
                    self.busy = False
                return {"status": "cancelled", "remove_tag": True}
            if action == "arm":
                if self.busy:
                    raise BridgeError(
                        "A writing session is already open; cancel it first"
                    )
                if not self.online:
                    raise BridgeError("USB reader offline")
                with self.daemon_lock:
                    self.busy = True
                sid = secrets.token_hex(24)
                self.session = {"id": sid, "used": False, "expires": 0}
                return {"session": sid}
            session = self.session
            if not session or not secrets.compare_digest(
                str(body.get("session", "")), session["id"]
            ):
                raise BridgeError("Unknown writer session")
            if action == "prepare":
                if session["used"]:
                    raise BridgeError("Session consumed")
                info = self._io(lambda wire: wire.request("write-info"))
                if info.get("status") != "ready":
                    raise BridgeError("Reader not ready: " + str(info.get("status")))
                if not re.fullmatch(
                    r"E00401[0-9A-F]{10}", info.get("uid", "")
                ) or not re.fullmatch(r"[0-9a-f]{16}", info.get("ticket", "")):
                    raise BridgeError("Invalid reader response")
                session.update(info=info, expires=time.monotonic() + 55)
                return {
                    "session": session["id"],
                    "uid": info["uid"],
                    "blank": info["blank"],
                    "expires_seconds": 55,
                }
            if action in ("cancel", "finish"):
                def close(wire):
                    wire.request("write-cancel")
                    if action == "finish":
                        # Verified and linked: read the tag again so it is looked up and activated.
                        wire.request("write-resume")

                self._io(close)
                self.session = None
                with self.daemon_lock:
                    self.busy = False
                return {"status": "cancelled", "remove_tag": True}
            if action != "commit":
                raise BridgeError("Unknown operation")
            if session["used"] or time.monotonic() > session["expires"]:
                raise BridgeError(
                    "Session consumed or expired; cancel and inspect again"
                )
            image = body.get("image_hex", "")
            if (
                not isinstance(image, str)
                or not re.fullmatch(r"[0-9a-fA-F]{20,632}", image)
                or len(image) % 2
            ):
                raise BridgeError("Invalid image")
            info = session["info"]
            replace = body.get("replace") is True
            if not info["blank"] and not replace:
                raise BridgeError("Replacement requires explicit confirmation")
            session["used"] = True  # consume before queuing or writing: never replay

            def commit(wire):
                from phoenix_opentag_write import save_backup

                backup = None
                if not info["blank"]:
                    backup = save_backup(info, self.backup_dir)
                try:
                    result = wire.request(
                        "write-commit "
                        + info["ticket"]
                        + (" replace " if replace else " new ")
                        + image
                    )
                except Exception as exc:
                    raise BridgeError(
                        f"Outcome unknown; backup: {backup}. {exc}"
                    ) from exc
                return dict(
                    result, uid=info["uid"], backup=str(backup) if backup else None
                )

            return self._io(commit)
        finally:
            self.guard.release()
