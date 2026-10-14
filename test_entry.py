"""Compatibility argv/exit contract, using a mock app and no Wine/Steam."""
import json, pathlib, shutil, subprocess, tempfile, unittest

class EntryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='wineforge-steam-entry-')
        self.root = pathlib.Path(self.tmp.name)
        self.entry = self.root/'run'
        shutil.copyfile(pathlib.Path(__file__).with_name('compat_run.sh'), self.entry)
        self.app = self.root/'App with spaces'
        self.receipt = self.root/'argv.json'
        self.app.write_text('#!/usr/bin/env python3\nimport json, pathlib, sys\npathlib.Path('+repr(str(self.receipt))+').write_text(json.dumps(sys.argv[1:]))\nraise SystemExit(73)\n')
        self.app.chmod(0o755)
        (self.root/'wineforge-app').write_text(str(self.app)+'\n')
    def tearDown(self): self.tmp.cleanup()
    def test_all_verbs_preserve_argv_and_exit(self):
        args = ['C:\\Games\\space name\\game.exe', '--option', "literal ' quote", '$(touch must-not-exist)', 'a;b', '', 'český text']
        for verb in ['run', 'waitforexitandrun', 'runinprefix', 'getcompatpath']:
            r = subprocess.run(['/bin/sh', str(self.entry), verb, *args], capture_output=True)
            self.assertEqual(r.returncode, 73)
            self.assertEqual(json.loads(self.receipt.read_text()), ['--steam-play', verb, '--', *args])
        self.assertFalse((self.root/'must-not-exist').exists())
    def test_invalid_verb_never_starts_app(self):
        r = subprocess.run(['/bin/sh', str(self.entry), 'invalid'], capture_output=True)
        self.assertEqual(r.returncode, 2)
        self.assertFalse(self.receipt.exists())
    def test_relative_application_registration_is_refused(self):
        (self.root/'wineforge-app').write_text('relative-app\n')
        r = subprocess.run(['/bin/sh', str(self.entry), 'run', 'game.exe'], capture_output=True)
        self.assertEqual(r.returncode, 2)
        self.assertFalse(self.receipt.exists())

if __name__ == '__main__': unittest.main()
