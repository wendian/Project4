CSci4061 S2016 Project 4

5/2/2016

Wendi An

4638209

#server.c

##1. Purpose
The purpose of this program is to use POSIX threads to create a multithreaded web server program that can concurrently handle any type of file and records requests in a log file. We have also implemented a cache to improve performance.

##2. Compile
To compile, use the Makefile included and simply type into the terminal:
```
make clean; make
```
##3. How to use from shell
To start the server, it must be given 6 or 7 arguments, depending if the user wants to implement caching. If 7 arguments are entered, caching will be implemented and the 7th argument will represent the number of cache entries desired; otherwise, the program will run without using the cache. The arguments for this program are port number (int), path (string), num_dispatcher (int), num_workers (int), qlen (int), and cacehe_size (int).

In a terminal window, type:
```
./web_server_http <port> <path> <num_dispatcher> <num_workers> <qlen> <cache_entries>
```
In the second window, type for a text file with URLs:
```
wget -i <path-to-urls> -O results
```
Or enter the following format into a browser:
```
http://127.0.0.1:<port>/<path_to_file>
```

##5. Assumptions
We are assuming file the <path-to-urls> argument leads to a file that is formatted in the form "http://127.0.0.1:<port>/<path_to_file>". We are also assuming the server will be running before the user wants to use the server.  The port numbers must match the port used with the program.

##6. Error handling
Any system call is checked to see if it returns an error value, if it does, a proper error is printed.  If the error will cause the program to stop functioning properly, the entire process will exit.

________________

