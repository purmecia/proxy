from http.server import BaseHTTPRequestHandler, HTTPServer

class MyHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        # Create a response that is incomplete
        self.send_response(200)
        self.send_header('aaa', '')
        self.end_headers()
        # self.wfile.write(b'')

if __name__ == '__main__':
    server = HTTPServer(('152.3.77.44', 5000), MyHandler)
    server.serve_forever()