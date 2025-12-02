from http.server import HTTPServer, BaseHTTPRequestHandler

class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        length = int(self.headers.get('Content-Length', 0))
        body = self.rfile.read(length) if length > 0 else b''
        print("---- Received webhook POST ----")
        print("Path:", self.path)
        print("Headers:")
        for k, v in self.headers.items():
            print(f"  {k}: {v}")
        print("Body:")
        print(body.decode('utf-8', errors='replace'))
        print("---- End ----\n")
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b'OK')

if __name__ == '__main__':
    addr = ('', 8080)
    print("Starting mock webhook server on http://0.0.0.0:8080")
    HTTPServer(addr, Handler).serve_forever()