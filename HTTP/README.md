## Network Programming

### HTTP Server

**USAGE**
```
make
make test
```
```
cd testfolder
./http_server.cpp localhost 8080
```

**Custom Routes**
Custom routes can be defiend in [defRoutes.cpp](./defRoutes.cpp) file.
```
<METHOD>.makeRoute("<route>", method);
```

Methods can be defined in the same file. This is only for demo, not serious http server programming.

Custom HTTP methods can also be defined by editing the same file.

