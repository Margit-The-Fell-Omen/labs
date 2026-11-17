#!/usr/bin/env python3
import http.server
import os
import ssl

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)

PORT = int(os.environ.get("PHONE_PORT", "8443"))


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, max-age=0")
        self.send_header("Pragma", "no-cache")
        super().end_headers()

    def do_GET(self):
        path = self.path.split("?", 1)[0]
        if path in ("/", "/index.html"):
            self.path = "/web-bluetooth.html"
        elif path != "/web-bluetooth.html":
            self.send_error(404)
            return
        super().do_GET()


httpd = http.server.ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain("cert.pem", "key.pem")
httpd.socket = ctx.wrap_socket(httpd.socket, server_side=True)
print(f"HTTPS server: 0.0.0.0:{PORT}", flush=True)
httpd.serve_forever()
