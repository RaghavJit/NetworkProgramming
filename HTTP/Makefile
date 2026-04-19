all: http_server.exe 

http_server.exe: http_server.o router.o defRoutes.o 
	g++ $(CPPFLAGS) $^ -o $@

http_server.o: http_server.cpp
	g++ -c http_server.cpp -o http_server.o

defRouter.o: defRouter.cpp
	g++ -c defRouter.cpp -o defRouter.o

router.o: router.cpp
	g++ -c router.cpp -o router.o

test: 
	cp http_server.exe ./testfolder/http_server.exe
