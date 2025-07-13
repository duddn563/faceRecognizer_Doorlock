#pragma

#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <errno.h>

class Utile {

public:
		static int checkFileExist(const char* fileName, int mode);

		static int createFile(const char* fileName, int mode);		
};



