#!/bin/sh

case $1 in
distclean)
	if [ -e CMakeCache.txt ]; then
		rm CMakeCache.txt
	fi
	if [ -e cmake_install.cmake ]; then
			rm cmake_install.cmake
	fi
	if [ -e CTestTestfile.cmake ]; then
		rm CTestTestfile.cmake
	fi
	if [ -e DartConfiguration.tcl ]; then
		rm DartConfiguration.tcl
	fi
	if [ -e CMakeFiles ]; then
		rm -rf CMakeFiles/
	fi
	if [ -e Testing ]; then
		rm -rf Testing/
	fi
	;;

create)
	if [ -e build ]; then
		rm -rf build
	fi
	mkdir -p build/Debug
	cd build/Debug
	cmake -DCMAKE_BUILD_TYPE=Debug ../..
	cd ../..
	
	mkdir -p build/Release
	cd build/Release
	cmake -DCMAKE_BUILD_TYPE=Release ../..
	cd ../..
	;;
*)
	usage 
	;;
esac
