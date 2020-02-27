main: main.cpp
	mkdir -p bin
	g++ -o bin/main main.cpp --std=c++17 -g -I/usr/include -lamqpcpp -luv 
