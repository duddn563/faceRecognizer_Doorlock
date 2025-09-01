#include "AuthManager.hpp"
#include <QDebug>

AuthManager::AuthManager() 
		: authCount(0),
			state(AuthState::Idle)
{
		qDebug() << "[Auth Manager] The constructor was called";
		qDebug() << "-QElapsed Timer stop";
		timer.invalidate();
}

void AuthManager::handleAuthSuccess() 
{
		if (authCount == 0) {
				firstAuthTime = QDateTime::currentDateTime();
				timer.restart();
				qDebug() << "[Auth Manager] Authentication Count Start and timer restart!";
		}

		authCount++;
		state = AuthState::Success;

		qDebug() << "[AuthManager] Auth Success #" << authCount << "| time: " << QDateTime::currentDateTime().toString("hh:mm:ss");
}

void AuthManager::handleAuthFailure() 
{
		state = AuthState::Failure;
		qDebug() << "[AuthManager] Authentication failure.";
}

bool AuthManager::isAuthValid() const 
{
		int rc = 0;

		if (!timer.isValid() || state != AuthState::Success) {
				qDebug() << "[AuthManager] isAuthValid# Timer is off or the authentication status is not successful.";
				return false;
		}

		rc = timer.elapsed() <= maxAuthDurationMs;
		if (rc == 0) { 
				qDebug() << "[AuthManager] isAuthValid# The authentication duration has elapsed since the first success.";	
		}

		return rc;
}

bool AuthManager::shouldAllowEntry() const
{
		int rc = 0;
		rc = isAuthValid() && authCount >= requiredSuccessCount;
		if (rc == 0) {
				qDebug() << "[AuthManager] shouldAllowEntry# The number of authentication attempts is insufficient";
		}

		return rc; 
}

void AuthManager::resetAuth()
{
		qDebug() << "[AuthManager] #resetAuth# initialization Authenticate state";
		authCount = 0;
		state = AuthState::Idle;
		timer.invalidate();
}

int AuthManager::getAuthCount() const
{
		qDebug() << "[AuthManger] getAuthCount# The number of authentication requests has been requested.";
		return authCount;
}

AuthManager::AuthState AuthManager::getState() const
{
		qDebug() << "[AuthManager] getState# Authentication state requests has been requested.";
		return state;
}
