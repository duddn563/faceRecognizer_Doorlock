#include <QApplication>
#include "MainWindow.hpp"

int main(int argc, char *argv[]) 
{
		try {	
				QApplication app(argc, argv);
				MainWindow w;
				w.show();
				return app.exec();
		} catch (const std::exception& e) {
				qCritical() << "Fatal exception: " << e.what();
		} catch (...) {
				qCritical() << "Unknown fatal exception!";
		}

		return -1;
}

