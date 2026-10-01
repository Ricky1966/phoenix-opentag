import argparse
import importlib.util
import json
from pathlib import Path
import threading
import os
import pty
import select
from unittest.mock import patch
import unittest
import urllib.request
import urllib.error
from http.server import ThreadingHTTPServer

spec = importlib.util.spec_from_file_location('daemon', Path(__file__).resolve().parents[1] / 'host/phoenix_opentag_daemon.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
A, B = 'E0040108660715E3', 'E004010866071917'

class Spoolman:
    def __init__(self): self.calls = []
    def call(self, method, path, body=None, **kwargs):
        self.calls.append((method, path, body))
        if path == '/tag/scan':
            return dict(uid=body['uid'], matched_spool_id=1 if body['uid']==A else 2, matched_filament_id=None)
        raise AssertionError((method, path))

class Moonraker:
    def __init__(self): self.active=None; self.state='standby'; self.idle='Ready'; self.offline=False; self.writes=[]
    def call(self, method, path, body=None, **kwargs):
        if self.offline: raise OSError('offline')
        if method=='GET': return {'result':{'status':{'print_stats':{'state':self.state},'idle_timeout':{'state':self.idle}}}}
        self.active=body['spool_id']; self.writes.append(self.active)

class Tests(unittest.TestCase):
    def setUp(self):
        self.d=m.Daemon(argparse.Namespace(spoolman='http://unused', moonraker='http://unused', no_create=False, no_activate=False, reader_id='test'))
        self.d.spoolman=Spoolman(); self.d.moonraker=Moonraker()
    def test_a_b_a(self):
        for uid in (A,B,A): self.d.handle({'uid':uid})
        self.assertEqual(self.d.moonraker.writes,[1,2,1])
    def test_consecutive_duplicate(self):
        self.d.handle({'uid':A})
        self.assertTrue(self.d.handle({'uid':A})['duplicate'])
        self.assertEqual(self.d.moonraker.writes,[1])
    def test_new_event_same_uid(self):
        for token in ('boot-1','boot-2'): self.d.handle({'uid':A,'event_id':token})
        self.assertEqual(self.d.moonraker.writes,[1,1])
    def test_printing_and_paused(self):
        for state in ('printing','paused'):
            self.d.moonraker.state=state
            self.assertFalse(self.d.handle({'uid':A,'event_id':state})['active'])
        self.assertEqual(self.d.moonraker.writes,[])
    def test_blank_tag_is_offered_to_spoolman(self):
        r=self.d.handle_blank({'uid':'E004010866073075','blank':True})
        self.assertTrue(r['offered'])
        scan=[c for c in self.d.spoolman.calls if c[1]=='/tag/scan'][-1][2]
        self.assertEqual((scan['uid'],scan['format']),('E004010866073075','openprinttag-blank'))
        # nothing is created or activated by a blank tag
        self.assertEqual(self.d.moonraker.writes,[])
        self.assertEqual([c for c in self.d.spoolman.calls if c[0]!='POST' or c[1]!='/tag/scan'],[])
    def test_blank_tag_ignored_while_printer_busy(self):
        for state,idle in (('printing','Printing'),('paused','Ready'),('standby','Printing')):
            self.d.moonraker.state,self.d.moonraker.idle=state,idle
            self.assertIn('ignored',self.d.handle_blank({'uid':'E004010866073075'}))
        self.assertEqual(self.d.spoolman.calls,[])
    def test_blank_tag_ignored_when_moonraker_offline(self):
        self.d.moonraker.offline=True
        self.assertIn('ignored',self.d.handle_blank({'uid':'E004010866073075'}))
        self.assertEqual(self.d.spoolman.calls,[])
    def test_blank_tag_ignored_during_write_session(self):
        self.d.writer=type('W',(),{'busy':True})()
        self.assertIn('ignored',self.d.handle_blank({'uid':'E004010866073075'}))
        self.assertEqual(self.d.spoolman.calls,[])
    def test_blank_tag_invalid_uid(self):
        with self.assertRaises(ValueError): self.d.handle_blank({'uid':'xyz'})
    def test_transient_not_cached(self):
        self.d.moonraker.offline=True
        self.assertIn('error',self.d.handle_logged({'uid':A},'test'))
        self.d.moonraker.offline=False
        self.assertTrue(self.d.handle({'uid':A})['active'])
    def test_v1_adapter(self):
        e=m.normalize_event({'uid':'E0:04:01:08:66:07:15:E3','schema_version':1,'material_name':'Test PLA','brand_name':'ODIN','color_rgba':'#C7BE5CFF','filament_diameter':1.75,'nominal_netto_full_weight':1000.5})
        self.assertEqual((e['uid'],e['color'],e['weight']),(A,'C7BE5C',1000.5))
    def test_invalid_payload(self):
        for extra in ({'uid':'garbage'},{'density':float('nan')},{'diameter':-1},{'event_id':'x\nreboot'},{'schema_version':2}):
            with self.assertRaises(ValueError): self.d.handle(dict({'uid':A},**extra))
        self.assertEqual(self.d.spoolman.calls,[])
    def test_http_retry(self):
        server=ThreadingHTTPServer(('127.0.0.1',0),m.make_handler(self.d))
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        req=urllib.request.Request('http://127.0.0.1:%d/tag'%server.server_port,data=json.dumps({'uid':A}).encode(),headers={'Content-Type':'application/json'})
        try:
            self.d.moonraker.offline=True
            with self.assertRaises(urllib.error.HTTPError) as error: urllib.request.urlopen(req)
            self.assertEqual(error.exception.code,502)
            self.d.moonraker.offline=False
            with urllib.request.urlopen(req) as response: self.assertTrue(json.load(response)['active'])
        finally: server.shutdown();server.server_close();thread.join()
    def test_usb_ack_only_after_success(self):
        master, slave = pty.openpty()
        path = os.ttyname(slave)
        real_open = m.open_serial
        opened = threading.Event()
        def notify_open(*args):
            fd = real_open(*args); opened.set(); return fd
        # Stop the serial worker deterministically when its FD is closed.
        with patch.object(m, 'open_serial', side_effect=notify_open):
            worker = threading.Thread(target=m.serial_loop, args=(self.d,path,115200), daemon=True)
            worker.start()
            self.assertTrue(opened.wait(2))
            payload = (m.LINE_PREFIX+json.dumps({'uid':A,'event_id':'session-1'})+'\n').encode()
            self.d.moonraker.offline=True
            os.write(master,payload)
            self.assertFalse(select.select([master],[],[],0.2)[0])
            self.d.moonraker.offline=False
            os.write(master,payload)
            self.assertTrue(select.select([master],[],[],2)[0])
            self.assertEqual(os.read(master,128),b'ack session-1\n')
        os.close(master); os.close(slave)

    def test_existing_spool_does_not_reset_weight(self):
        self.d.handle({'uid':A,'weight':1000,'density':1.24,'material':'PLA'})
        self.assertTrue(all(path=='/tag/scan' for _,path,_ in self.d.spoolman.calls))

if __name__=='__main__': unittest.main()
