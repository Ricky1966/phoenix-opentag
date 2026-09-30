from pathlib import Path
import stat
import sys
import socket
import tempfile
import threading
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import phoenix_opentag_write as writer

UID = 'E004010866073472'
OLD_MEMORY = bytes((7 * i + 3) % 251 for i in range(320))   # contenuto vecchio del tag, non nullo

class Fake:
    def __init__(self, uid=UID, blank=True, result=None, memory_hex='default', on_commit=None):
        self.uid, self.blank, self.commands, self.on_commit = uid, blank, [], on_commit
        self.memory_hex = OLD_MEMORY.hex() if memory_hex == 'default' else memory_hex
        self.result = result or {'status': 'verified', 'verified': True}
    def request(self, command):
        self.commands.append(command)
        if command == 'write-info':
            info = dict(status='ready', uid=self.uid, blank=self.blank, ticket='0123456789abcdef')
            if self.memory_hex is not None:
                info['memory_hex'] = self.memory_hex
            return info
        if command == 'write-cancel':
            return dict(status='cancelled')
        if self.on_commit:
            self.on_commit()
        return self.result

class WriterClientTests(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self._tmp.cleanup)
        self.backups = Path(self._tmp.name) / 'backups'

    def write(self, client, image=bytes(320), uid=UID, replace=False):
        return writer.write_image(client, image, uid, replace, self.backups)

    def test_wrong_uid_never_commits(self):
        c = Fake(uid='E0040108660715E3')
        with self.assertRaises(writer.WriterError): self.write(c)
        self.assertEqual(c.commands, ['write-info', 'write-cancel'])
    def test_existing_requires_replace(self):
        c = Fake(blank=False)
        with self.assertRaises(writer.WriterError): self.write(c)
        self.assertEqual(c.commands[-1], 'write-cancel')
        c = Fake(blank=False)
        self.write(c, replace=True)
        self.assertIn(' replace ', c.commands[-1])
    def test_failure_is_not_retried(self):
        c = Fake(result=dict(status='verify_failed', verified=False, may_have_changed=True))
        with self.assertRaises(writer.WriterError): self.write(c)
        self.assertEqual(len(c.commands), 2)
    def test_bad_input_before_io(self):
        c = Fake()
        for image, uid in [(bytes(321), UID), (bytes(320), 'arbitrary\ncommand')]:
            with self.assertRaises(writer.WriterError): self.write(c, image, uid)
        self.assertEqual(c.commands, [])

    # --- copia di sicurezza prima di sostituire un tag non vuoto ---
    def test_backup_saved_before_commit_with_exact_bytes(self):
        seen = {}
        def on_commit():
            files = list(self.backups.glob('*.bin'))
            seen['files'] = files
            seen['bytes'] = files[0].read_bytes() if files else None
        c = Fake(blank=False, on_commit=on_commit)
        result = self.write(c, replace=True)
        self.assertEqual(len(seen['files']), 1, 'il backup deve esistere gia al momento del commit')
        self.assertEqual(seen['bytes'], OLD_MEMORY)
        path = Path(result['backup'])
        self.assertEqual(path.read_bytes(), OLD_MEMORY)
        self.assertTrue(path.name.startswith(UID + '-') and path.suffix == '.bin')
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
        self.assertEqual(len(path.read_bytes()), 320)

    def test_blank_tag_needs_no_backup(self):
        result = self.write(Fake(blank=True))
        self.assertNotIn('backup', result)
        self.assertFalse(self.backups.exists())

    def test_backup_failure_aborts_before_commit(self):
        blocker = Path(self._tmp.name) / 'file'
        blocker.write_text('x')
        self.backups = blocker / 'sub'          # la cartella non si puo creare
        c = Fake(blank=False)
        with self.assertRaises(writer.WriterError) as ctx:
            self.write(c, replace=True)
        self.assertIn('tag non modificato', str(ctx.exception))
        self.assertEqual(c.commands, ['write-info', 'write-cancel'])

    def test_missing_or_bad_memory_aborts_before_commit(self):
        for bad in (None, '', 'zz' * 320, '00' * 319, '00' * 321):
            c = Fake(blank=False, memory_hex=bad)
            with self.assertRaises(writer.WriterError, msg=repr(bad)[:20]):
                self.write(c, replace=True)
            self.assertEqual(c.commands, ['write-info', 'write-cancel'])
        self.assertFalse(self.backups.exists())

    def test_failed_write_reports_backup_path_and_keeps_file(self):
        c = Fake(blank=False, result=dict(status='verify_failed', verified=False, may_have_changed=True))
        with self.assertRaises(writer.WriterError) as ctx:
            self.write(c, replace=True)
        files = list(self.backups.glob('*.bin'))
        self.assertEqual(len(files), 1)
        self.assertIn(str(files[0]), str(ctx.exception))
        self.assertEqual(files[0].read_bytes(), OLD_MEMORY)

    def test_existing_backups_are_never_overwritten(self):
        with mock.patch.object(writer.time, 'strftime', return_value='20260930-120000'):
            first = self.write(Fake(blank=False, memory_hex=(b'\x01' * 320).hex()), replace=True)['backup']
            second = self.write(Fake(blank=False, memory_hex=(b'\x02' * 320).hex()), replace=True)['backup']
        self.assertNotEqual(first, second)
        self.assertEqual(Path(first).read_bytes(), b'\x01' * 320)
        self.assertEqual(Path(second).read_bytes(), b'\x02' * 320)

    def test_wrong_uid_makes_no_backup(self):
        with self.assertRaises(writer.WriterError):
            self.write(Fake(uid='E0040108660715E3', blank=False), replace=True)
        self.assertFalse(self.backups.exists())


class SerialProtocolTests(unittest.TestCase):
    backups = Path(tempfile.gettempdir()) / 'phoenix-test-unused-backups'

    def test_stream_noise_and_full_commit(self):
        local, remote = socket.socketpair()
        commands = []
        def emulator():
            try:
                for reply in [b'{"status":"ready","uid":"E004010866073472","blank":true,"ticket":"0123456789abcdef"}', b'{"status":"verified","verified":true}']:
                    command = b''
                    while not command.endswith(b'\n'):
                        part = remote.recv(1024)
                        if not part: return
                        command += part
                    commands.append(command)
                    remote.sendall(b'boot log\nPHOENIX-TAG {}\nPHOENIX-')
                    remote.sendall(b'WRITE ' + reply + b'\n')
            finally:
                remote.close()
        thread = threading.Thread(target=emulator, daemon=True)
        thread.start()
        try:
            result = writer.write_image(writer.SerialWriter(local.fileno(), 1), bytes(320), UID, False, self.backups)
            self.assertTrue(result['verified'])
            self.assertEqual(len(commands), 2)
            self.assertTrue(commands[-1].endswith(b'0' * 640 + b'\n'))
        finally:
            local.close()
            thread.join(2)
        self.assertFalse(thread.is_alive())

    def test_timeout_has_no_retry(self):
        local, remote = socket.socketpair()
        try:
            with self.assertRaises(writer.WriterError):
                writer.SerialWriter(local.fileno(), 0.01).request('write-info')
            self.assertEqual(remote.recv(1024), b'write-info\n')
        finally:
            local.close(); remote.close()

if __name__ == '__main__': unittest.main()
