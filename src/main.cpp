#include <QApplication>
#include <QLoggingCategory>
#include <QDebug>
#include <exception>
#include "MainWindow.hpp"
#include "fsm/fsm_logging.hpp"

int main(int argc, char *argv[]) 
{
		try {	
				QApplication app(argc, argv);

				qSetMessagePattern(QStringLiteral("%{time hh:mm:ss.zzz} %{type} %{category} - %{message}"));
				QLoggingCategory::setFilterRules(
						"fsm.state.debug=false\n"
						"fsm.guard.debug=false\n"
						"fsm.ctx.debug=false\n"
						"fsm.warn.debug=false\n"
				);

				MainWindow w;
				w.show();
				return app.exec();
		} catch (const std::exception& e) {
				qCritical() << "[" << __func__ << "] Fatal exception: " << e.what();
		} catch (...) {
				qCritical() << "[" << __func__ << "] Unknown fatal exception!";
		}

		return -1;
}


