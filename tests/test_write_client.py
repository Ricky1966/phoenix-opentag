from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'host'))
import phoenix_opentag_write as writer

UID = 'E004010866073472'
class Fake:
    def __init__(self, uid=UID, blank=True, result=None):
        self.uid, self.blank, self.commands = uid, blank, []
        self.result = result or {'status': 'verified', 'verified': True}
    def request(self, command):
        self.commands.append(command)
        if command == 'write-info':
            return dict(status='ready', uid=self.uid, blank=self.blank, ticket='0123456789abcdef')
        if command == 'write-cancel':
            return dict(status='cancelled')
        return self.result

class WriterClientTests(unittest.TestCase):
    def test_wrong_uid_never_commits(self):
        c = Fake(uid='E0040108660715E3')
        with self.assertRaises(writer.WriterError): writer.write_image(c, bytes(320), UID, False)
        self.assertEqual(c.commands, ['write-info', 'write-cancel'])
    def test_existing_requires_replace(self):
        c = Fake(blank=False)
        with self.assertRaises(writer.WriterError): writer.write_image(c, bytes(320), UID, False)
        self.assertEqual(c.commands[-1], 'write-cancel')
        c = Fake(blank=False)
        writer.write_image(c, bytes(320), UID, True)
        self.assertIn(' replace ', c.commands[-1])
    def test_failure_is_not_retried(self):
        c = Fake(result=dict(status='verify_failed', verified=False, may_have_changed=True))
        with self.assertRaises(writer.WriterError): writer.write_image(c, bytes(320), UID, False)
        self.assertEqual(len(c.commands), 2)
    def test_bad_input_before_io(self):
        c = Fake()
        for image, uid in [(bytes(321), UID), (bytes(320), 'arbitrary\ncommand')]:
            with self.assertRaises(writer.WriterError): writer.write_image(c, image, uid, False)
        self.assertEqual(c.commands, [])

if __name__ == '__main__': unittest.main()
