#!/usr/bin/env python3
"""Exercise the actual C++ transport against an isolated HTTPS server."""
from pathlib import Path
import http.server
from test_support import executable
import ssl
import subprocess
import tempfile
import threading
import time
import unittest



class TransportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='readest-https-')
        root = Path(cls.work.name)
        cls.binary, cls.cert = executable(), root / 'cert.pem'
        key = root / 'key.pem'
        config = root / 'openssl.cnf'
        config.write_text('[req]\ndistinguished_name=dn\nx509_extensions=ext\nprompt=no\n'
                          '[dn]\nCN=localhost\n[ext]\nsubjectAltName=DNS:localhost\n'
                          'basicConstraints=critical,CA:TRUE\n')
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                        '-days', '1', '-keyout', str(key), '-out', str(cls.cert),
                        '-config', str(config)], check=True, capture_output=True)
        cls.requests = []

        class Handler(http.server.BaseHTTPRequestHandler):
            def handle(self):
                try:
                    super().handle()
                except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                    # Cancellation and rejected TLS handshakes close the peer.
                    pass

            def log_message(self, *args):
                pass

            def do_GET(self):
                cls.requests.append((self.path, self.headers.get('Authorization'), b''))
                if self.path == '/slow':
                    self.send_response(200)
                    self.send_header('Content-Length', str(100 * 1024))
                    self.end_headers()
                    try:
                        for _ in range(100):
                            self.wfile.write(b'x' * 1024)
                            self.wfile.flush()
                            time.sleep(0.05)
                    except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                        pass
                elif self.path == '/redirect':
                    self.send_response(302)
                    self.send_header('Location', '/must-not-follow')
                    self.send_header('Content-Length', '0')
                    self.end_headers()
                else:
                    payload = b'x' * 8192 if self.path == '/large' else b'{"ok":true}'
                    self.send_response(200)
                    self.send_header('Content-Length', '1000' if self.path == '/truncated' else str(len(payload)))
                    self.end_headers()
                    self.wfile.write(payload)

            def do_PUT(self):
                if self.path == "/slow":
                    time.sleep(2)
                self.do_POST()

            def do_POST(self):
                body = self.rfile.read(int(self.headers['Content-Length']))
                cls.requests.append((self.path, self.headers.get('Authorization'), body))
                self.send_response(401 if self.path == '/unauthorized' else 200)
                self.send_header('Content-Length', str(len(b'{"ok":true}')))
                self.end_headers()
                self.wfile.write(b'{"ok":true}')

        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cls.cert, key)
        cls.server.socket = context.wrap_socket(cls.server.socket, server_side=True)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.url = f'https://localhost:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.work.cleanup()

    def run_request(self, path='/', method='GET', limit=4096, url=None, cert=None):
        return subprocess.run([str(self.binary), (url or self.url) + path,
                               str(cert or self.cert), method, str(limit)],
                              capture_output=True, text=True, timeout=10)

    def test_verified_tls_post_and_status(self):
        result = self.run_request('/unauthorized', 'POST')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith('401\n'))
        self.assertEqual(self.requests[-1], ('/unauthorized', 'Bearer fixture-token', b'{"fixture":true}'))

    def test_redirect_does_not_forward_token(self):
        before = len(self.requests)
        result = self.run_request('/redirect')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue(result.stdout.startswith('302\n'))
        self.assertEqual(len(self.requests), before + 1)
        self.assertEqual(self.requests[-1][0], '/redirect')

    def test_response_limit(self):
        result = self.run_request('/large', limit=64)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

    def test_download_stream_has_no_bearer(self):
        result = self.run_request('/large', 'DOWNLOAD', limit=8192)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '200\n8192\n' + 'x' * 8192)
        self.assertEqual(self.requests[-1], ('/large', None, b''))

    def test_upload_stream_has_no_bearer(self):
        result = self.run_request('/upload', 'UPLOAD')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[-1], ('/upload', None, b'u' * 8192))

    def test_cancelled_worker_aborts_transport(self):
        before = len(self.requests)
        result = self.run_request('/', 'CANCEL')
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')

        self.assertIn('Cancelled.', result.stderr)
        self.assertEqual(len(self.requests), before)

    def test_cancel_active_request_and_download_then_reuse_adapter(self):
        for mode in ['LATE_CANCEL_GET', 'LATE_CANCEL_DOWNLOAD', 'LATE_CANCEL_UPLOAD']:
            result = self.run_request('/slow', mode, limit=200000)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, 'Cancelled active transfer; next operation succeeded.')

    def test_download_limit_and_truncation_fail(self):
        for path, limit in [('/large', 64), ('/truncated', 4096)]:
            result = self.run_request(path, 'DOWNLOAD', limit=limit)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, '')

    def test_wrong_hostname_and_missing_ca_fail(self):
        result = self.run_request(url=self.url.replace('localhost', '127.0.0.1'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('curl 60', result.stderr)
        self.assertIn('resolver ', result.stderr)
        self.assertNotIn('fixture-token', result.stderr)
        self.assertNotIn(self.url, result.stderr)
        self.assertNotEqual(self.run_request(cert=Path(self.work.name) / 'absent.pem').returncode, 0)

    def test_plain_http_rejected(self):
        before = len(self.requests)
        result = self.run_request(url=self.url.replace('https:', 'http:'))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(len(self.requests), before)


if __name__ == '__main__':
    unittest.main()
