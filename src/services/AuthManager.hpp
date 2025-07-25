#pragma once
#include <QElapsedTimer>
#include <QDateTime>

class AuthManager {
public:
		enum class AuthState { Idle, Authenticating, Success, Failure };

		AuthManager();

		void handleAuthSuccess();
		void handleAuthFailure(); 
		bool isAuthValid() const;
		void resetAuth();
		bool shouldAllowEntry() const;

		int getAuthCount() const;
		AuthState getState() const;

private:
		int authCount;
		QDateTime firstAuthTime;
		QElapsedTimer timer;								
		AuthState state;

		const int maxAuthDurationMs = 30000;		// 30 second limit
		const int requiredSuccessCount = 3;			//  Authentication success count
};


