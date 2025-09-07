#include <QApplication>
#include <QLoggingCategory>
#include <QDebug>
#include <exception>
#include "MainWindow.hpp"
#include "fsm/fsm_logging.hpp"
#include "services/QSqliteService.hpp"
#include "log/SystemLogger.hpp"

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

                  // DB 준비
                QSqliteService svc;
                if (!svc.initializeDatabase()) {
                    qCritical() << "데이터베이스 초기화 실패";
                    return -1;
                }

                // 시스템로거 준비
                SystemLogger::init();
                SystemLogger::info("APP", "Logger initialized");

				    QObject::connect(&app, &QCoreApplication::aboutToQuit, []{
        				SystemLogger::info("APP", "aboutToQuit");
        				SystemLogger::shutdown();
    				});

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


