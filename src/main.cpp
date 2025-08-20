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
						"fsm.state.debug=true\n"
						"fsm.guard.debug=true\n"
						"fsm.ctx.debug=true\n"
						"fsm.warn.debug=true\n"
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


