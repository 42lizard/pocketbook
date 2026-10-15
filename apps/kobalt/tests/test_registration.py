import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('register_reader', Path(__file__).resolve().parents[1] / 'tools/register_reader.py')
registration = importlib.util.module_from_spec(spec)
spec.loader.exec_module(registration)


class RegistrationTest(unittest.TestCase):
    def test_registration_preserves_default_other_readers_and_comments(self):
        before = '# personal\r\nepub:@EPUB_file:1:custom.app,eink-reader.app:ICON_EPUB\r\nfb2:Book:1:other.app:ICON_FB2\r\n'
        after = registration.register(before)
        self.assertEqual(after, before.replace('custom.app,eink-reader.app', 'custom.app,eink-reader.app,kobalt.app'))
        self.assertEqual(registration.register(after), after)

    def test_empty_config_uses_exact_target_firmware_defaults(self):
        self.assertEqual(registration.register(''), registration.DEFAULT_EPUB.replace(':ICON_EPUB', ',kobalt.app:ICON_EPUB') + '\n')

    def test_ambiguous_config_is_rejected_without_guessing(self):
        with self.assertRaises(ValueError):
            registration.register(registration.DEFAULT_EPUB + '\n' + registration.DEFAULT_EPUB)
        with self.assertRaises(ValueError):
            registration.register('epub:malformed\n')

    def test_install_keeps_original_backup_and_later_associations(self):
        with tempfile.TemporaryDirectory() as directory:
            config = Path(directory) / 'extensions.cfg'
            original = (registration.DEFAULT_EPUB + '\n').encode()
            config.write_bytes(original)
            registration.install(config)
            backup = config.with_name('extensions.cfg.before-kobalt')
            self.assertEqual(backup.read_bytes(), original)
            edited = config.read_bytes() + b'pdf:Book:1:custom.app:ICON_PDF\n'
            config.write_bytes(edited)
            registration.install(config)
            self.assertEqual(config.read_bytes(), edited)
            self.assertEqual(backup.read_bytes(), original)


if __name__ == '__main__':
    unittest.main()
