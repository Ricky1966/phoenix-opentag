from pathlib import Path
import sys
import http.client
from http.server import ThreadingHTTPServer
from types import SimpleNamespace
import tempfile
import threading
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "host"))
from phoenix_writer_bridge import WriterBridge, BridgeError

UID = "E004010866073075"


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.bridge = WriterBridge(threading.Lock(), "t" * 40, self.temp.name)
        self.bridge.online = True
        self.commands = []
        self.blank = True
        self.io_fail = False
        self.stop = threading.Event()
        bridge = self.bridge
        outer = self

        class Wire:
            def __init__(self, fd):
                pass

            def request(self, cmd):
                outer.commands.append(cmd)
                if cmd == "write-info":
                    return dict(
                        status="ready",
                        uid=UID,
                        ticket="a" * 16,
                        blank=outer.blank,
                        memory_hex="01" * 320,
                    )
                if cmd.startswith("write-commit"):
                    if outer.io_fail:
                        raise OSError("disconnected")
                    return dict(status="verified", verified=True)
                return dict(status="cancelled")

        self.patch = mock.patch("phoenix_opentag_write.SerialWriter", Wire)
        self.patch.start()
        self.addCleanup(self.patch.stop)

        def worker():
            while not self.stop.wait(0.002):
                bridge.tick(-1)

        self.thread = threading.Thread(target=worker)
        self.thread.start()

        def cleanup():
            self.stop.set()
            self.thread.join(2)

        self.addCleanup(cleanup)

    def prepare(self):
        info = self.bridge.command("arm", {})
        self.assertTrue(self.bridge.busy)
        self.assertEqual(self.commands, [])
        return self.bridge.command("prepare", {"session": info["session"]})

    def test_session_blocks_processing_until_finish(self):
        info = self.prepare()
        self.assertTrue(self.bridge.busy)
        result = self.bridge.command(
            "commit", dict(session=info["session"], image_hex="00" * 320)
        )
        self.assertTrue(result["verified"])
        self.assertEqual(result["uid"], UID)
        with self.assertRaises(BridgeError):
            self.bridge.command(
                "commit", dict(session=info["session"], image_hex="00" * 320)
            )
        self.assertTrue(self.bridge.busy)
        self.bridge.command("finish", dict(session=info["session"]))
        self.assertFalse(self.bridge.busy)
        self.assertEqual(sum(x.startswith("write-commit") for x in self.commands), 1)

    def test_backup_before_replace(self):
        self.blank = False
        info = self.prepare()
        with self.assertRaises(BridgeError):
            self.bridge.command(
                "commit", dict(session=info["session"], image_hex="00" * 320)
            )
        result = self.bridge.command(
            "commit", dict(session=info["session"], image_hex="00" * 320, replace=True)
        )
        self.assertEqual(Path(result["backup"]).read_bytes(), b"\x01" * 320)

    def test_disconnection_consumes_session_without_retry(self):
        info = self.prepare()
        self.io_fail = True
        with self.assertRaises(BridgeError):
            self.bridge.command(
                "commit", dict(session=info["session"], image_hex="00" * 320)
            )
        with self.assertRaises(BridgeError):
            self.bridge.command(
                "commit", dict(session=info["session"], image_hex="00" * 320)
            )
        self.assertTrue(self.bridge.busy)
        self.bridge.command("reset", {})
        self.assertFalse(self.bridge.busy)

    def test_wrong_session_cannot_cancel_or_write(self):
        self.bridge.command("arm", {})
        with self.assertRaises(BridgeError):
            self.bridge.command("cancel", dict(session="wrong"))
        self.assertTrue(self.bridge.busy)
        self.assertEqual(self.commands, [])

    def test_offline_prepare_does_not_reserve(self):
        self.bridge.online = False
        with self.assertRaises(BridgeError):
            self.bridge.command("arm", {})
        self.assertFalse(self.bridge.busy)


class BridgeHTTPTests(unittest.TestCase):
    def test_authentication_and_browser_rejection_before_command(self):
        from phoenix_opentag_daemon import make_handler

        writer = mock.Mock(token="t" * 40)
        writer.command.return_value = {"session": "test"}
        server = ThreadingHTTPServer(
            ("127.0.0.1", 0), make_handler(SimpleNamespace(writer=writer))
        )
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            for headers, expected in [
                ({}, 403),
                ({"Authorization": "Bearer wrong"}, 403),
                (
                    {
                        "Authorization": "Bearer " + writer.token,
                        "Origin": "http://example.com",
                    },
                    403,
                ),
                ({"Authorization": "Bearer " + writer.token}, 200),
            ]:
                connection = http.client.HTTPConnection(*server.server_address)
                connection.request("POST", "/writer/arm", body="{}", headers=headers)
                response = connection.getresponse()
                self.assertEqual(response.status, expected)
                response.read()
                connection.close()
            writer.command.assert_called_once_with("arm", {})
        finally:
            server.shutdown()
            server.server_close()
            thread.join(2)


if __name__ == "__main__":
    unittest.main()
