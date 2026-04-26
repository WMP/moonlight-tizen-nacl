#!/usr/bin/env python3
from http.server import HTTPServer, BaseHTTPRequestHandler
from datetime import datetime
import argparse
import sys

class Handler(BaseHTTPRequestHandler):
    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        if self.path != "/health":
            self.send_response(404)
            self.end_headers()
            return

        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        self.wfile.write(f"ok log_file={self.server.log_file}\n".encode("utf-8"))

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8", errors="replace")

        line = f"{datetime.now().isoformat()} {body}\n"

        with open(self.server.log_file, "a", encoding="utf-8") as f:
            f.write(line)

        print(line, end="")
        sys.stdout.flush()

        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

    def log_message(self, fmt, *args):
        return

def parse_args():
    parser = argparse.ArgumentParser(description="Moonlight TV pad debug log server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--log-file", default="tv_pad_debug.log")
    return parser.parse_args()

if __name__ == "__main__":
    args = parse_args()
    host = args.host
    port = args.port
    log_file = args.log_file
    print(f"Listening on http://{host}:{port}")
    print(f"Writing to {log_file}")
    server = HTTPServer((host, port), Handler)
    server.log_file = log_file
    server.serve_forever()
