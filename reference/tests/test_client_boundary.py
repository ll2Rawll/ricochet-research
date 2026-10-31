from http.server import BaseHTTPRequestHandler, HTTPServer
import threading
import unittest

from ricochet_lab.__main__ import post


class ClientBoundaryTests(unittest.TestCase):
    def call_endpoint(self, status, body, extra_headers=None):
        class Handler(BaseHTTPRequestHandler):
            calls = 0
            def log_message(self, *args):
                pass
            def do_POST(self):
                type(self).calls += 1
                self.rfile.read(int(self.headers["Content-Length"]))
                self.send_response(status)
                self.send_header("Content-Length", str(len(body)))
                for key, value in (extra_headers or {}).items():
                    self.send_header(key, value)
                self.end_headers()
                self.wfile.write(body)
        with HTTPServer(("127.0.0.1", 0), Handler) as server:
            thread = threading.Thread(target=server.serve_forever, kwargs={"poll_interval": 0.01})
            thread.start()
            try:
                with self.assertRaises(ValueError):
                    post(server.server_port, "/v1/challenge", {"session_id": "test"})
                self.assertEqual(Handler.calls, 1)
            finally:
                server.shutdown()
                thread.join(timeout=2)

    def test_redirect_is_never_followed(self):
        self.call_endpoint(302, b"", {"Location": "https://example.test/never-requested"})

    def test_response_size_and_json_ambiguity(self):
        for body in [b"x" * 65537, b'{"x":1,"x":2}', b'{"x":NaN}', b'{"x":1e999}', b'[]']:
            with self.subTest(body=body[:30]):
                self.call_endpoint(200, body)


if __name__ == "__main__":
    unittest.main()
