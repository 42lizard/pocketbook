#!/usr/bin/env python3
"""Verify real transport routing against local TLS, without an online account."""
import http.server
import json
import os
from pathlib import Path
import ssl
import subprocess
import sys
import tempfile
import threading
import time

with tempfile.TemporaryDirectory(prefix='pocketbook-live-https-') as folder:
    root = Path(folder)
    cert, key = root / 'cert.pem', root / 'key.pem'
    subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes',
                    '-days', '1', '-keyout', str(key), '-out', str(cert),
                    '-subj', '/CN=localhost', '-addext', 'subjectAltName=DNS:localhost'],
                   check=True, capture_output=True)
    requests = []

    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def respond(self, body):
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
            requests.append((self.path, body))
            self.respond(json.dumps({'access_token': 'local-test-access',
                'refresh_token': 'local-test-refresh', 'expires_at': int(time.time()) + 3600,
                'user': {'id': 'local-test-user'}}).encode())

        def do_GET(self):
            requests.append((self.path, self.headers.get('Authorization')))
            self.respond(b'local TLS download' if self.path == '/download' else b'{"books":[]}')

    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(cert, key)
    server.socket = context.wrap_socket(server.socket, server_side=True)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        env = dict(os.environ, POCKETBOOK_SIM_CLOUD='real', READEST_SIM_CA=str(cert),
                   SIM_TEST_URL=f'https://localhost:{server.server_port}', SIM_TEST_CA=str(cert))
        subprocess.run([sys.argv[1]], env=env, check=True)
        assert requests[0] == ('/auth/v1/token?grant_type=password',
                              {'email': 'demo+ä@example.test', 'password': 'Test@+ü!#'})
        assert any(path.startswith('/api/sync?') and auth == 'Bearer local-test-access'
                   for path, auth in requests[1:])
        assert ('/download', None) in requests
    finally:
        server.shutdown()
        server.server_close()
        thread.join()
