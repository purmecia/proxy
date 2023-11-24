#simple test to set cache-control header
#can be used for testing cache-control header in request

from flask import Flask, request, jsonify, make_response
import datetime
import hashlib

app = Flask(__name__)

@app.route('/')
def index():

    headers = {
        'Content-Type': 'application/json',
        'Cache-Control': 'max-age=10',  
    }

    # else:
    body = 'Hello World'
    response=make_response(body,200)
    response.headers = headers # set headers
    return response


if __name__ == '__main__':
    app.run(host='0.0.0.0')