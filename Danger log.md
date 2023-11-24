# Danger log

## External failure
- If we enter a wrong website address, then it can't be parsed. In our code, the problem will be caught, and throw an client expection, return 400.

-  Data stream may get truncated when receiving the response from the origin server. We can parse the content length from the response header so we are able to determine whether the response body is completely received

- There may exist Denial-of-service (DoS) attacks. Proxies can be targeted by attackers looking to overwhelm them with traffic, causing them to crash or become unresponsive. We may use load balancing to handle it.

## Exception guarantees 
### Client-side

If our proxy receives a invalid request from the client, we would send the client directly with a 400 Bad Response. 

These conditions include: 
- request doesn’t have the first line
- request doesn’t have header part
- request header lack value

To test it, we generated a malformed request and used netcat and got a 400 response. See README.md for details.



### Server-side

If our proxy receives an invalid response from an origin server, we would send the client a 502 Bad Gateway response.

These conditions include: 
- response doesn’t have the first line
- response doesn’t have header part
- response header lack value

To test it, we generate a response that lack of header value. See README.md for details.


## Other possible problem
- Proxies may be used to spread malfare infections, we may use a filter to handle it. 
- In our proxy design, we use thread instead of process, and we use a shared memory to store cache, there may be attack against these.