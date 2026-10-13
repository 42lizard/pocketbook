import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

SOURCE = Path(__file__).resolve().parents[1] / 'tools/package.py'


class PackageTest(unittest.TestCase):
    def test_package_contains_only_launcher_binary_assets_and_notices(self):
        spec = importlib.util.spec_from_file_location('kobalt_package', SOURCE)
        package = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(package)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = root / 'kobalt'
            binary.write_bytes(b'\x7fELFfixture')
            assets = root / 'assets'
            assets.mkdir()
            (assets / 'manifest.json').write_text('{}')
            notice = root / 'LICENSE'
            notice.write_text('fixture notice')
            output = root / 'package.zip'
            package.create(binary, assets, [notice], output)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(set(archive.namelist()), {
                    'applications/kobalt.app', 'system/kobalt/bin/kobalt',
                    'system/kobalt/assets/manifest.json', 'system/kobalt/notices/LICENSE'})
                self.assertEqual(archive.read('system/kobalt/bin/kobalt'), binary.read_bytes())
                self.assertIn(b'"$@"', archive.read('applications/kobalt.app'))
            with self.assertRaises(FileExistsError):
                package.create(binary, assets, [notice], output)


if __name__ == '__main__':
    unittest.main()
