from flask import Flask, request, jsonify, make_response
import datetime
import hashlib

app = Flask(__name__)


etag = "123789"

now = datetime.datetime.utcnow()-datetime.timedelta(days=20)

@app.route('/')
def index():
    headers = {
        'Content-Type': 'application/json',
        'Cache-Control': 'no-cache',  
        'Etag': etag,
        'Last-Modified': now.strftime("%a, %d %b %Y %H:%M:%S GMT"),
    }
    
    etag_header = request.headers.get('If-None-Match')
    if_modified_since_header = request.headers.get('If-Modified-Since')

    print("received request: ", request.headers)
    print("etag_header: ", etag_header)
    print("if_modified_since_header: ", if_modified_since_header)
    # if etag_header == etag or if_modified_since_header == now.strftime('%a, %d %b %Y %H:%M:%S GMT'):
    #     # If the ETag or Last-Modified date matches, return a 304 Not Modified response
    #     return '', 304
    # else:
    body = 'Hello World'
    response=make_response(body,200)
    response.headers = headers # set headers
    return response
        # etag_header = etag
        # if_modified_since_header = now.strftime('%a, %d %b %Y %H:%M:%S GMT')

        # return jsonify({"content": content, "etag": etag_header, "last-modified": if_modified_since_header})

if __name__ == '__main__':
    app.run(host='0.0.0.0')