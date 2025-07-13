#include "util.hpp"
#include "logger.hpp"

int Utile::checkFileExist(const char* fileName, int mode)
{
		int rc = access(fileName, mode);

		if (rc) {
				const char* modeMsgTable[8] = {
						"%s exists check failed!!\n",                          // 0 (F_OK)
						"%s can't be execute!!\n",														 // 1 (X_OK) 
						"%s can't be write!!\n",                               // 2 (W_OK)
						"%s can't be write and execute!!\n",                   // 3 (W_OK + X_OK)
						"%s can't be read!!\n",                                // 4 (R_OK)
						"%s can't be read and execute!!\n",                    // 5 (R_OK + X_OK)
						"%s can't be read and write!!\n",                      // 6 (R_OK + W_OK)
						"%s can't be read and write and execute!!\n"           // 7 (R_OK + W_OK + X_OK)
				};

				if (mode == F_OK) {
						Logger::writef("[%s] %s is not exist!!\n", __func__, fileName);
				} else {
						Logger::writef("[%s] ", __func__);
						Logger::writef(modeMsgTable[mode], fileName);
				}
				return -1;
		}


		return 0;
}

int Utile::createFile(const char* fileName, int mode = 0)
{
		int rc = 0;

		std::cout << "fileName: " << fileName << "  mode: " << mode << std::endl;

		std::ofstream file(fileName);
		if (file.is_open()) {
				file.close();
		}
		else {
				Logger::writef("[%s] Failed to create file(name: %s)!!\n" , __func__, fileName);
				return -1;
		}

		if (mode) {
				switch (mode) {
						case 1:
								rc = chmod(fileName, 0111);
								break;
						case 2:
								rc = chmod(fileName, 0222);
								break;
						case 3:
								rc = chmod(fileName, 0333);
								break;
						case 4:
								rc = chmod(fileName, 0444);
								break;
						case 5:
								rc = chmod(fileName, 0555);
								break;
						case 6:
								rc = chmod(fileName, 0666);
								break;
						case 7:
								rc = chmod(fileName, 0777);
								break;
						default:
								Logger::writef("[%s] Unknow mode (mode: %d)\n",__func__, mode);  
								return -1;
				}
		}

		if (rc == -1) {
				Logger::writef("[%s] Permission errno: %d\n", __func__, errno);
				return -1;
		}
		
		return 0;
}
		

