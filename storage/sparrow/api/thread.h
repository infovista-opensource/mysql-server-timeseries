#ifndef _spw_api_thread_h_
#define _spw_api_thread_h_

#include "cond.h"
#include "mysql/psi/mysql_thread.h"
//#include "include/my_pthread.h"

namespace Sparrow {
    
//////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread
//////////////////////////////////////////////////////////////////////////////////////////////////////

class Thread {
public:

	// Lock used by all start/stop condition variables.
	static Lock lock_;

private:

	char*	m_name_{nullptr};
	my_thread_handle thread_;
	volatile bool running_;
	volatile bool stop_;		// To signal a stop command to the thread
	Cond startCond_;
	Cond stopCond_;

protected:
	my_thread_t threadId_;

public:

	// TODO: understand why the version with (Str() + Str()).c_str() does not compile
	//Thread(const char* name) : running_(false), stop_(false), startCond_(false, lock_, (Str(name) + Str("::startCond_")).c_str()),
	//	stopCond_(false, lock_, (Str(name) + Str("::stopCond_")).c_str()) {
	Thread(const char* name) : running_(false), stop_(false), startCond_(false, lock_, "::startCond_"),
		stopCond_(false, lock_, "::stopCond_"), threadId_(0) {
		if (name != nullptr) {
			m_name_ = my_strdup(name, MYF(MY_FAE));
		}
	}

	virtual ~Thread() {
		if (m_name_ != nullptr) {
			my_free(const_cast<char*>(m_name_));
		}
	}

	bool start() {
		my_thread_attr_t attr;
		my_thread_attr_init(&attr);
		//my_thread_attr_setdetachstate(&attr, MY_THREAD_CREATE_DETACHED);
		my_thread_attr_setstacksize(&attr, 262144);
		Guard guard(lock_);
		PRINT_DBUG("[thread] Starting %s", m_name_ != nullptr ? m_name_ : "unknown");
		if (my_thread_create(&thread_, &attr, reinterpret_cast<void*(*)(void*)>(handler), static_cast<void*>(this)) != 0 ) {
			PRINT_DBUG("[thread] Failed to start %s", m_name_ != nullptr ? m_name_ : "unknown");
			return false;
		}
		startCond_.wait(true);
		PRINT_DBUG("[thread] Started %s", m_name_ != nullptr ? m_name_ : "unknown");
		return true;
	}

	void stop() {
		PRINT_DBUG("[thread] Stopping %s: running %s", m_name_ != nullptr ? m_name_ : "unknown", running_ ? "true" : "false");
		if ( running_ ) {
			Guard guard(lock_);
			stop_ = true;
			PRINT_DBUG("[thread] Notifying stop for %s", m_name_ != nullptr ? m_name_ : "unknown");
			notifyStop();
			join();
			PRINT_DBUG("[thread] Stopped %s", m_name_ != nullptr ? m_name_ : "unknown");
			stop_ = false;
		} 
	}

	void join() {
		my_thread_join(&thread_, nullptr);
	}

	bool isRunning() const { return running_; }

protected:

	void stopping() { running_ = false; }

	virtual bool process() = 0;

	virtual void notifyStop() = 0;

	virtual bool deleteAfterExit() = 0;

private:

	static void* handler(void *p) {
		Thread* thread = (Thread*)p;
		thread->running_ = true;
		thread->threadId_ = my_thread_self();
		PRINT_DBUG("[thread-hdlr %llu] Started %s", thread->threadId_, thread->m_name_ != nullptr ? thread->m_name_ : "unknown");
		thread->startCond_.signal();
		while (!thread->stop_) {
			if (!thread->process()) {
				break;
			}
		}
		thread->running_ = false;

		PRINT_DBUG("[thread-hdlr %llu] Stopped %s", thread->threadId_, thread->m_name_ != nullptr ? thread->m_name_ : "unknown");
		thread->stopCond_.signal();
		/*if (thread->stop_) {
			thread->stopCond_.signal();
		} else {*/
			if (thread->deleteAfterExit()) {
				PRINT_DBUG("[thread-hdlr %llu] Stopped %s", thread->threadId_, thread->m_name_ != nullptr ? thread->m_name_ : "unknown");
				delete thread;
			}
		//}
		return 0;
	}
};

}

#endif /* #ifndef _spw_api_thread_h_ */
