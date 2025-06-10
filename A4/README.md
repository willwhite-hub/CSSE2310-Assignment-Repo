# CSSE2310 - Assignment 4
The uqfaceclient program provides a command line interface that allows you to interact with the server
(uqfacedetect) as a client – connecting, sending an image to detect faces, sending an (optional) image to
replace faces with, receiving the modified image back from the server and saving it to a file.
It is able to construct a request based on the command line arguments, connect to the server, 
send the request, await a response, and then save the response to a file.

uqfacedetect is a networked, multithreaded image processing server allowing clients to connect, send an image 
for manipulation (and an optional image to replace faces with), and then return a manipulated image to the 
client. All communication between clients and the server is over TCP.
